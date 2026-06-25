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
static volatile bool s_post_ready = false;
static volatile uint32_t s_ready_offset = 0;
static volatile bool s_post_abort = false;
static volatile uint8_t s_post_abort_reason = 0;
static SemaphoreHandle_t s_mutex = nullptr;
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;
static bool s_pending_reboot = false;

static bool ota_process_pending_write(void);

static void ota_flash_worker(void* arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ota_process_pending_write();
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

static void ota_queue_ready(uint32_t ready_off) {
  s_ready_offset = ready_off;
  s_post_ready = true;
  ota_signal_poll_task();
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

static uint32_t ota_segment_bytes_at(uint32_t offset) {
  const uint32_t remain = s_image_size - offset;
  return remain < (uint32_t)OTA_SEGMENT_BYTES ? remain : (uint32_t)OTA_SEGMENT_BYTES;
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
  s_pending_write = false;
  s_pending_n = 0;
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
    return;
  }
  if (s_post_ready) {
    s_post_ready = false;
    const uint32_t ready_offset = s_ready_offset;
    ESP_LOGI(TAG, "sending READY offset=%lu", (unsigned long)ready_offset);
    ota_send_node(OTA_FLASH_READY, ready_offset);
  }
}

static bool ota_process_pending_write(void) {
  bool pending = false;
  portENTER_CRITICAL(&s_spinlock);
  pending = s_pending_write;
  portEXIT_CRITICAL(&s_spinlock);
  if (!pending || !s_active || !s_ota_handle)
    return true;
  uint32_t current_offset = 0;
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    current_offset = s_bytes_written;
    xSemaphoreGive(s_mutex);
  } else {
    current_offset = s_bytes_written;
  }
  portENTER_CRITICAL(&s_spinlock);
  s_flash_erase_in_progress = true;
  portEXIT_CRITICAL(&s_spinlock);
  const int64_t t0 = esp_timer_get_time();
  esp_err_t werr = esp_ota_write(s_ota_handle, s_pending_data, s_pending_n);
  const int64_t write_ms = (esp_timer_get_time() - t0) / 1000;
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
  const uint8_t written_n = s_pending_n;
  s_running_crc = crc32_update(s_running_crc, s_pending_data, written_n);
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    s_bytes_written += (uint32_t)written_n;
    xSemaphoreGive(s_mutex);
  } else {
    s_bytes_written += (uint32_t)written_n;
  }
  s_last_activity_ms = platform_millis();
  portENTER_CRITICAL(&s_spinlock);
  s_pending_write = false;
  s_flash_erase_in_progress = false;
  portEXIT_CRITICAL(&s_spinlock);
  s_pending_n = 0;
  uint32_t ready_off = 0;
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    ready_off = s_bytes_written;
    xSemaphoreGive(s_mutex);
  } else {
    ready_off = s_bytes_written;
  }
  ESP_LOGI(TAG, "flash write %u bytes at %lu took %lld ms",
           (unsigned)written_n, (unsigned long)current_offset, (long long)write_ms);
  ota_queue_ready(ready_off);
  return true;
}

static bool ota_begin_session(uint32_t size) {
  ota_ensure_mutex();
  s_pending_write = false;
  s_flash_erase_in_progress = false;
  s_pending_n = 0;
  s_post_ready = false;
  s_post_abort = false;
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
  ESP_LOGI(TAG, "OTA session ready size=%lu", (unsigned long)size);
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
    if (s_active && s_image_size == size) {
      uint32_t written = s_bytes_written;
      ESP_LOGI(TAG, "FLASH_INIT resend, READY offset=%lu", (unsigned long)written);
      ota_send_node(OTA_FLASH_READY, written);
      return;
    }
    s_pending_reboot = false;
#if defined(WS2812_ENABLE) && WS2812_ENABLE
    ws2812_request_ota_transfer();
#endif
    if (!ota_begin_session(size)) {
#if defined(WS2812_ENABLE) && WS2812_ENABLE
      ws2812_request_ota_failed();
#endif
    }
    return;
  }

  if (!s_active)
    return;

  if (opcode == OTA_FLASH_DATA) {
    bool busy = false;
    portENTER_CRITICAL(&s_spinlock);
    busy = s_pending_write || s_flash_erase_in_progress;
    portEXIT_CRITICAL(&s_spinlock);
    if (busy)
      return;

    const uint8_t n = (uint8_t)((data[3] >> 5) & 0x07U);
    const uint32_t offset_lo = (uint32_t)(data[3] & 0x1FU);
    uint32_t current_written = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      current_written = s_bytes_written;
      xSemaphoreGive(s_mutex);
    } else {
      current_written = s_bytes_written;
    }
    const uint32_t expect_n = ota_segment_bytes_at(current_written);
    if ((current_written & 0x1FU) != offset_lo) {
      ESP_LOGW(TAG, "DATA offset mismatch lo=%u have=%lu",
               (unsigned)offset_lo, (unsigned long)current_written);
      ota_send_data_err(current_written);
      return;
    }
    if (n == 0 || n != expect_n || dlc < (uint8_t)(4 + n)) {
      ESP_LOGW(TAG, "DATA len mismatch n=%u expect=%lu at %lu",
               (unsigned)n, (unsigned long)expect_n, (unsigned long)current_written);
      ota_send_data_err(current_written);
      return;
    }
    if (current_written + n > s_image_size) {
      ota_send_data_err(current_written);
      return;
    }
    memcpy(s_pending_data, &data[4], n);
    s_pending_n = n;
    portENTER_CRITICAL(&s_spinlock);
    s_pending_write = true;
    portEXIT_CRITICAL(&s_spinlock);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
    if (current_written == 0)
      ws2812_request_ota_transfer();
#endif
    ota_kick_flash_write();
    return;
  }

  if (opcode == OTA_FLASH_DONE) {
    bool pending = false;
    portENTER_CRITICAL(&s_spinlock);
    pending = s_pending_write;
    portEXIT_CRITICAL(&s_spinlock);
    if (pending) {
      ota_kick_flash_write();
      const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300000);
      portENTER_CRITICAL(&s_spinlock);
      pending = s_pending_write;
      portEXIT_CRITICAL(&s_spinlock);
      while (pending && (int)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        portENTER_CRITICAL(&s_spinlock);
        pending = s_pending_write;
        portEXIT_CRITICAL(&s_spinlock);
      }
    }
    portENTER_CRITICAL(&s_spinlock);
    pending = s_pending_write;
    portEXIT_CRITICAL(&s_spinlock);
    if (pending) {
      ota_abort(6);
      return;
    }
    uint32_t final_written = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      final_written = s_bytes_written;
      xSemaphoreGive(s_mutex);
    } else {
      final_written = s_bytes_written;
    }
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
  uint32_t current_written = 0;
  if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    current_written = s_bytes_written;
    xSemaphoreGive(s_mutex);
  } else {
    current_written = s_bytes_written;
  }
  bool pending = false;
  portENTER_CRITICAL(&s_spinlock);
  pending = s_pending_write;
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
