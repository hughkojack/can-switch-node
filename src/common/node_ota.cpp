#include "node_ota.h"
#include "can.h"
#include "platform_time.h"

#if defined(NODE_ROLE_MIN)

#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(WS2812_ENABLE) && WS2812_ENABLE
#include "ws2812_strip.h"
#endif

static const char* TAG = "node_ota";

#define OTA_LOG_INTERVAL_BYTES 4096U

static bool s_active = false;
static uint32_t s_image_size = 0;
static uint32_t s_expected_crc = 0;
static volatile uint32_t s_bytes_written = 0;
static uint32_t s_running_crc = 0xFFFFFFFF;
static uint32_t s_last_activity_ms = 0;
static uint8_t s_node_id = 0;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t* s_update_part = nullptr;
static volatile bool s_pending_write = false;
static volatile bool s_flash_erase_in_progress = false;
static uint8_t s_pending_data[OTA_SEGMENT_BYTES];
static uint8_t s_pending_n = 0;
static uint32_t s_session_start_ms = 0;
static TaskHandle_t s_flash_task = nullptr;
static TaskHandle_t s_poll_task = nullptr;
static volatile bool s_post_abort = false;
static volatile uint8_t s_post_abort_reason = 0;
static SemaphoreHandle_t s_mutex = nullptr;
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool s_pending_reboot = false;

/* Block transfer (FLASH_INIT meta byte 3) */
static uint8_t s_block_bytes = OTA_SEGMENT_BYTES;
static uint8_t s_block_buf[OTA_BLOCK_BUF_MAX];
static uint8_t s_block_seg_mask = 0;

/* Pipeline window=2 for legacy 4-byte segments */
static uint8_t s_queue_data[OTA_SEGMENT_BYTES];
static uint8_t s_queue_n = 0;
static volatile bool s_queue_pending = false;

static uint32_t s_log_last_offset = 0;

static bool ota_process_pending_write(void);
static bool ota_execute_pending_write(void);
static void ota_promote_queue_to_pending(void);

static void ota_flash_worker(void* arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (ota_process_pending_write())
      ;
  }
}

static void ota_ensure_mutex(void) {
  if (!s_mutex) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

static void ota_ensure_flash_task(void) {
  if (!s_flash_task) {
    xTaskCreate(ota_flash_worker, "ota_flash", 8192, NULL, 5, &s_flash_task);
  }
}

static void ota_kick_flash_write(void) {
  ota_ensure_flash_task();
  if (s_flash_task)
    xTaskNotifyGive(s_flash_task);
}

static void ota_signal_poll_task(void) {
  if (s_poll_task)
    xTaskNotifyGive(s_poll_task);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xEDB88320U & (~(crc & 1U) + 1U));
  }
  return crc;
}

static uint32_t crc32_finalize(uint32_t crc) {
  return ~crc;
}

static void ota_write_offset_be(uint8_t* data, uint32_t offset) {
  data[4] = (uint8_t)((offset >> 24) & 0xFF);
  data[5] = (uint8_t)((offset >> 16) & 0xFF);
  data[6] = (uint8_t)((offset >> 8) & 0xFF);
  data[7] = (uint8_t)(offset & 0xFF);
}

static void ota_send_node(uint8_t opcode, uint32_t offset_or_reason) {
  if (s_node_id == 0)
    return;
  const uint32_t now = platform_millis();
  s_last_activity_ms = now;
  uint8_t f[8] = {};
  f[0] = 0;
  f[1] = s_node_id;
  f[2] = opcode;
  if (opcode == OTA_FLASH_ERROR)
    f[4] = (uint8_t)(offset_or_reason & 0xFF);
  else
    ota_write_offset_be(f, offset_or_reason);
  can_send_ota_node(s_node_id, f);
}

static void ota_send_data_err(uint32_t expected_offset) {
  ESP_LOGW(TAG, "DATA_ERR expected offset=%lu", (unsigned long)expected_offset);
  ota_send_node(OTA_FLASH_DATA_ERR, expected_offset);
}

static uint8_t ota_parse_block_bytes(uint8_t meta) {
  if (meta == 0)
    return OTA_SEGMENT_BYTES;
  if (meta == 16 || meta == 32)
    return meta;
  return OTA_TRANSFER_BLOCK_BYTES;
}

static uint32_t ota_segment_bytes_at(uint32_t offset) {
  const uint32_t remain = s_image_size - offset;
  return remain < (uint32_t)OTA_SEGMENT_BYTES ? remain : (uint32_t)OTA_SEGMENT_BYTES;
}

static uint32_t ota_block_target_at(uint32_t base_offset) {
  uint32_t target = (uint32_t)s_block_bytes;
  const uint32_t remain = s_image_size - base_offset;
  if (target > remain)
    target = remain;
  return target;
}

static uint8_t ota_block_segment_count(uint32_t block_start) {
  const uint32_t target = ota_block_target_at(block_start);
  return (uint8_t)((target + 3U) / 4U);
}

static uint32_t ota_block_first_missing(uint32_t block_start) {
  const uint8_t n_segs = ota_block_segment_count(block_start);
  for (uint8_t i = 0; i < n_segs; i++) {
    if ((s_block_seg_mask & (1U << i)) == 0)
      return block_start + (uint32_t)i * 4U;
  }
  return block_start;
}

static void ota_log_progress(uint32_t offset) {
  if (offset < s_log_last_offset + OTA_LOG_INTERVAL_BYTES && offset != s_image_size)
    return;
  s_log_last_offset = offset - (offset % OTA_LOG_INTERVAL_BYTES);
  ESP_LOGI(TAG, "OTA progress %lu / %lu (%lu%%)",
           (unsigned long)offset, (unsigned long)s_image_size,
           (unsigned long)(s_image_size ? (offset * 100UL) / s_image_size : 0UL));
}

static void ota_reset_transfer_state(void) {
  s_pending_write = false;
  s_flash_erase_in_progress = false;
  s_pending_n = 0;
  s_block_seg_mask = 0;
  s_queue_pending = false;
  s_queue_n = 0;
  s_log_last_offset = 0;
}

static void ota_abort(uint8_t reason) {
  if (s_ota_handle) {
    esp_ota_abort(s_ota_handle);
    s_ota_handle = 0;
  }
  s_update_part = nullptr;
  s_active = false;
  s_pending_reboot = false;
  s_bytes_written = 0;
  s_running_crc = 0xFFFFFFFF;
  ota_reset_transfer_state();
  ota_send_node(OTA_FLASH_ERROR, reason);
  ESP_LOGW(TAG, "abort reason=%u", (unsigned)reason);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
  ws2812_request_ota_failed();
#endif
}

static void ota_drain_flash_notifications(void) {
  if (s_post_abort) {
    const uint8_t reason = s_post_abort_reason;
    uint32_t written = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      written = s_bytes_written;
      xSemaphoreGive(s_mutex);
    } else {
      written = s_bytes_written;
    }
    s_post_abort = false;
    ESP_LOGW(TAG, "abort reason=%u bytes_written=%lu", (unsigned)reason, (unsigned long)written);
    ota_abort(reason);
  }
}

static bool ota_write_bytes(const uint8_t* data, uint8_t n, uint32_t current_offset) {
  portENTER_CRITICAL(&s_spinlock);
  s_flash_erase_in_progress = true;
  portEXIT_CRITICAL(&s_spinlock);

  esp_err_t werr = esp_ota_write(s_ota_handle, data, n);
  if (werr != ESP_OK) {
    ESP_LOGW(TAG, "esp_ota_write failed at %lu: %s",
             (unsigned long)current_offset, esp_err_to_name(werr));
    portENTER_CRITICAL(&s_spinlock);
    s_pending_write = false;
    s_flash_erase_in_progress = false;
    portEXIT_CRITICAL(&s_spinlock);
    s_pending_n = 0;
    s_post_abort_reason = 5;
    s_post_abort = true;
    ota_signal_poll_task();
    return false;
  }

  s_running_crc = crc32_update(s_running_crc, data, n);
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_bytes_written += (uint32_t)n;
    xSemaphoreGive(s_mutex);
  } else {
    s_bytes_written += (uint32_t)n;
  }
  s_last_activity_ms = platform_millis();

  portENTER_CRITICAL(&s_spinlock);
  s_pending_write = false;
  s_flash_erase_in_progress = false;
  portEXIT_CRITICAL(&s_spinlock);
  s_pending_n = 0;

  uint32_t ready_off = s_bytes_written;
  ota_send_node(OTA_FLASH_READY, ready_off);
  ota_log_progress(ready_off);
  return true;
}

static void ota_promote_queue_to_pending(void) {
  if (!s_queue_pending)
    return;
  memcpy(s_pending_data, s_queue_data, s_queue_n);
  s_pending_n = s_queue_n;
  s_queue_pending = false;
  s_queue_n = 0;
  portENTER_CRITICAL(&s_spinlock);
  s_pending_write = true;
  portEXIT_CRITICAL(&s_spinlock);
}

static bool ota_execute_pending_write(void) {
  bool pending = false;
  portENTER_CRITICAL(&s_spinlock);
  pending = s_pending_write;
  portEXIT_CRITICAL(&s_spinlock);
  if (!pending || !s_active || !s_ota_handle)
    return true;

  uint32_t current_offset = s_bytes_written;
  const uint8_t n = s_pending_n;
  if (!ota_write_bytes(s_pending_data, n, current_offset))
    return false;

  if (s_queue_pending) {
    ota_promote_queue_to_pending();
    return true;
  }
  return true;
}

static bool ota_process_pending_write(void) {
  if (!ota_execute_pending_write())
    return false;
  bool pending = false;
  portENTER_CRITICAL(&s_spinlock);
  pending = s_pending_write;
  portEXIT_CRITICAL(&s_spinlock);
  return pending;
}

static bool ota_flush_block(void) {
  if (!s_active || !s_ota_handle)
    return true;

  const uint32_t block_start = s_bytes_written;
  const uint8_t n = (uint8_t)ota_block_target_at(block_start);
  if (n == 0)
    return true;

  if (!ota_write_bytes(s_block_buf, n, block_start))
    return false;
  s_block_seg_mask = 0;
  return true;
}

static bool ota_queue_segment_frame(const uint8_t* data, uint8_t n) {
  if (s_queue_pending)
    return false;
  memcpy(s_queue_data, data, n);
  s_queue_n = n;
  s_queue_pending = true;
  return true;
}

static bool ota_accept_data_frame(const uint8_t* data, uint8_t dlc) {
  const uint8_t n = (uint8_t)((data[3] >> 5) & 0x07U);
  const uint32_t offset_lo = (uint32_t)(data[3] & 0x1FU);
  uint32_t current_written = s_bytes_written;

  if (s_block_bytes > OTA_SEGMENT_BYTES) {
    const uint32_t block_start = current_written;
    const uint32_t target = ota_block_target_at(block_start);
    const int32_t lo_base = (int32_t)(block_start & 0x1FU);
    const int32_t lo_diff = (int32_t)offset_lo - lo_base;

    if (lo_diff < 0 || (uint32_t)lo_diff >= target) {
      /* Stale or pipelined-ahead frame; ignore without DATA_ERR. */
      return true;
    }
    if (((uint32_t)lo_diff & 3U) != 0U) {
      ota_send_data_err(ota_block_first_missing(block_start));
      return false;
    }

    const uint32_t data_offset = block_start + (uint32_t)lo_diff;
    const uint8_t seg_idx = (uint8_t)((uint32_t)lo_diff >> 2U);
    const uint32_t expect_n = ota_segment_bytes_at(data_offset);

    if (n == 0 || n != expect_n || dlc < (uint8_t)(4 + n)) {
      ESP_LOGW(TAG, "DATA len mismatch n=%u expect=%lu at %lu",
               (unsigned)n, (unsigned long)expect_n, (unsigned long)data_offset);
      ota_send_data_err(ota_block_first_missing(block_start));
      return false;
    }
    if (data_offset + n > s_image_size) {
      ota_send_data_err(ota_block_first_missing(block_start));
      return false;
    }
    if (s_block_seg_mask & (1U << seg_idx))
      return true;

    memcpy(s_block_buf + (size_t)lo_diff, &data[4], n);
    s_block_seg_mask |= (1U << seg_idx);

    const uint8_t n_segs = ota_block_segment_count(block_start);
    const uint8_t need_mask = (uint8_t)((1U << n_segs) - 1U);
    if ((s_block_seg_mask & need_mask) == need_mask)
      return ota_flush_block();
    return true;
  }

  /* Legacy 4-byte segment mode with optional pipeline window=2 */
  bool busy = false;
  portENTER_CRITICAL(&s_spinlock);
  busy = s_pending_write || s_flash_erase_in_progress;
  portEXIT_CRITICAL(&s_spinlock);

  uint32_t accept_offset = current_written;
  if (busy) {
    if (s_queue_pending)
      return true;
    accept_offset = s_bytes_written + (uint32_t)s_pending_n;
  }

  const uint32_t expect_n = ota_segment_bytes_at(accept_offset);
  if ((accept_offset & 0x1FU) != offset_lo) {
    ESP_LOGW(TAG, "DATA offset mismatch lo=%u expect=%lu",
             (unsigned)offset_lo, (unsigned long)accept_offset);
    ota_send_data_err(accept_offset);
    return false;
  }
  if (n == 0 || n != expect_n || dlc < (uint8_t)(4 + n)) {
    ESP_LOGW(TAG, "DATA len mismatch n=%u expect=%lu at %lu",
             (unsigned)n, (unsigned long)expect_n, (unsigned long)accept_offset);
    ota_send_data_err(accept_offset);
    return false;
  }
  if (accept_offset + n > s_image_size) {
    ota_send_data_err(accept_offset);
    return false;
  }

  if (busy)
    return ota_queue_segment_frame(&data[4], n);

#if defined(WS2812_ENABLE) && WS2812_ENABLE
  if (accept_offset == 0)
    ws2812_request_ota_transfer();
#endif

  memcpy(s_pending_data, &data[4], n);
  s_pending_n = n;
  portENTER_CRITICAL(&s_spinlock);
  s_pending_write = true;
  portEXIT_CRITICAL(&s_spinlock);

  while (ota_process_pending_write())
    ;
  return true;
}

static bool ota_begin_session(uint32_t size, uint8_t block_meta) {
  ota_ensure_mutex();
  ota_reset_transfer_state();
  s_post_abort = false;
  s_block_bytes = ota_parse_block_bytes(block_meta);
  if (size == 0 || size > 0x180000UL) {
    ota_send_node(OTA_FLASH_ERROR, 1);
    return false;
  }
  const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
  if (!part || size > part->size) {
    ota_send_node(OTA_FLASH_ERROR, 2);
    return false;
  }
  if (s_ota_handle) {
    esp_ota_abort(s_ota_handle);
    s_ota_handle = 0;
  }
  esp_err_t err = esp_ota_begin(part, size, &s_ota_handle);
  if (err != ESP_OK) {
    ota_send_node(OTA_FLASH_ERROR, 3);
    return false;
  }
  s_update_part = part;
  s_image_size = size;
  s_expected_crc = 0;
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_bytes_written = 0;
    xSemaphoreGive(s_mutex);
  } else {
    s_bytes_written = 0;
  }
  s_running_crc = 0xFFFFFFFF;
  s_active = true;
  s_session_start_ms = platform_millis();
  s_last_activity_ms = s_session_start_ms;
  ota_ensure_flash_task();
  ESP_LOGI(TAG, "OTA session ready size=%lu block=%u",
           (unsigned long)size, (unsigned)s_block_bytes);
  ota_send_node(OTA_FLASH_READY, 0);
  return true;
}

bool node_ota_is_active(void) {
  return s_active;
}

bool node_ota_pending_reboot(void) {
  return s_pending_reboot;
}

void node_ota_set_node_id(uint8_t node_id) {
  s_node_id = node_id;
}

void node_ota_set_poll_task(TaskHandle_t task) {
  s_poll_task = task;
}

bool node_ota_should_ignore_can(void) {
  bool ignore = false;
  portENTER_CRITICAL(&s_spinlock);
  ignore = s_flash_erase_in_progress;
  portEXIT_CRITICAL(&s_spinlock);
  return ignore;
}

void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc) {
  if (!data || dlc < 3)
    return;
  s_last_activity_ms = platform_millis();
  const uint8_t opcode = data[2];
  const uint16_t target_id = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
  if (target_id != 0 && target_id != s_node_id)
    return;

  if (opcode == OTA_FLASH_ABORT) {
    if (s_active)
      ota_abort(dlc >= 5 ? data[4] : 0);
    return;
  }

  if (opcode == OTA_FLASH_INIT) {
    if (dlc < 8)
      return;
    const uint32_t size = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                        | ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    const uint8_t block_meta = data[3];
    if (s_active && s_image_size == size) {
      s_block_seg_mask = 0;
      s_queue_pending = false;
      s_queue_n = 0;
      portENTER_CRITICAL(&s_spinlock);
      s_pending_write = false;
      s_pending_n = 0;
      portEXIT_CRITICAL(&s_spinlock);
      s_block_bytes = ota_parse_block_bytes(block_meta);
      uint32_t written = s_bytes_written;
      ESP_LOGI(TAG, "FLASH_INIT resend, READY offset=%lu block=%u",
               (unsigned long)written, (unsigned)s_block_bytes);
      ota_send_node(OTA_FLASH_READY, written);
      return;
    }
    s_pending_reboot = false;
#if defined(WS2812_ENABLE) && WS2812_ENABLE
    ws2812_request_ota_transfer();
#endif
    if (!ota_begin_session(size, block_meta)) {
#if defined(WS2812_ENABLE) && WS2812_ENABLE
      ws2812_request_ota_failed();
#endif
    }
    return;
  }

  if (!s_active)
    return;

  if (opcode == OTA_FLASH_DATA) {
    (void)ota_accept_data_frame(data, dlc);
    return;
  }

  if (opcode == OTA_FLASH_DONE) {
    bool pending = false;
    portENTER_CRITICAL(&s_spinlock);
    pending = s_pending_write || s_queue_pending;
    portEXIT_CRITICAL(&s_spinlock);
    if (pending) {
      ota_kick_flash_write();
      const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300000);
      portENTER_CRITICAL(&s_spinlock);
      pending = s_pending_write || s_queue_pending;
      portEXIT_CRITICAL(&s_spinlock);
      while (pending && (int)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        while (ota_process_pending_write())
          ;
        portENTER_CRITICAL(&s_spinlock);
        pending = s_pending_write || s_queue_pending;
        portEXIT_CRITICAL(&s_spinlock);
      }
    }
    if (s_block_seg_mask != 0) {
      ota_abort(6);
      return;
    }
    portENTER_CRITICAL(&s_spinlock);
    pending = s_pending_write || s_queue_pending;
    portEXIT_CRITICAL(&s_spinlock);
    if (pending) {
      ota_abort(6);
      return;
    }
    uint32_t final_written = s_bytes_written;
    if (final_written != s_image_size) {
      ota_abort(6);
      return;
    }
    if (dlc >= 8) {
      s_expected_crc = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16)
                     | ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    }
    if (s_expected_crc != 0) {
      uint32_t crc = crc32_finalize(s_running_crc);
      if (crc != s_expected_crc) {
        ESP_LOGW(TAG, "CRC mismatch got=0x%08lX exp=0x%08lX",
                 (unsigned long)crc, (unsigned long)s_expected_crc);
        ota_abort(7);
        return;
      }
    }
    esp_err_t err = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    if (err != ESP_OK) {
      ota_abort(8);
      return;
    }
    if (s_update_part && esp_ota_set_boot_partition(s_update_part) != ESP_OK) {
      ota_abort(8);
      return;
    }
    s_update_part = nullptr;
    s_active = false;
    ota_send_node(OTA_FLASH_COMPLETE, final_written);
    s_pending_reboot = true;
    ESP_LOGI(TAG, "complete, click or hub reboot to run new image");
#if defined(WS2812_ENABLE) && WS2812_ENABLE
    ws2812_request_ota_success();
#endif
  }
}

void node_ota_drain_notifications(void) {
  ota_drain_flash_notifications();
}

void node_ota_service(unsigned long now_ms) {
  ota_drain_flash_notifications();
  const uint32_t now = (uint32_t)now_ms;
  if (!s_active)
    return;
  uint32_t current_written = s_bytes_written;
  bool pending = false;
  portENTER_CRITICAL(&s_spinlock);
  pending = s_pending_write || s_queue_pending || s_block_seg_mask != 0;
  portEXIT_CRITICAL(&s_spinlock);
  if (!pending && s_last_activity_ms != 0 && current_written > 0) {
    if (now >= s_last_activity_ms) {
      const uint32_t idle_ms = now - s_last_activity_ms;
      if (idle_ms >= FW_OTA_IDLE_TIMEOUT_MS) {
        ESP_LOGW(TAG, "idle timeout %lu ms (last=%lu now=%lu)",
                 (unsigned long)idle_ms,
                 (unsigned long)s_last_activity_ms, (unsigned long)now);
        ota_abort(9);
      }
    }
  }
}

#endif /* NODE_ROLE_MIN */
