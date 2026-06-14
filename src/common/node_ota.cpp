#include "node_ota.h"
#include "can.h"
#include "platform_time.h"

#if defined(NODE_ROLE_MIN)

#include <esp_ota_ops.h>
#include <esp_log.h>
#include <string.h>

#if defined(WS2812_ENABLE) && WS2812_ENABLE
#include "ws2812_strip.h"
#endif

static const char* TAG = "node_ota";

static bool s_active = false;
static bool s_begin_part0 = false;
static bool s_begin_part1 = false;
static uint32_t s_image_size = 0;
static uint32_t s_expected_crc = 0;
static uint16_t s_expected_version = 0;
static uint16_t s_next_seq = 0;
static uint32_t s_bytes_written = 0;
static uint32_t s_running_crc = 0xFFFFFFFF;
static uint32_t s_last_activity_ms = 0;
static uint8_t s_node_id = 0;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t* s_update_part = nullptr;

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

static void ota_send_status(uint8_t status, uint16_t seq, uint32_t extra) {
  if (s_node_id != 0)
    can_send_firmware_status(s_node_id, status, seq, extra);
}

static void ota_abort(uint8_t reason) {
  if (s_ota_handle) {
    esp_ota_abort(s_ota_handle);
    s_ota_handle = 0;
  }
  s_update_part = nullptr;
  s_active = false;
  s_begin_part0 = false;
  s_begin_part1 = false;
  s_bytes_written = 0;
  s_next_seq = 0;
  s_running_crc = 0xFFFFFFFF;
  ota_send_status(FW_STATUS_ERROR, 0, reason);
  ESP_LOGW(TAG, "abort reason=%u", (unsigned)reason);
}

static bool ota_begin_session(uint32_t size, uint32_t crc, uint16_t version) {
  (void)version;
  if (size == 0 || size > 0x180000UL) {
    ota_send_status(FW_STATUS_ERROR, 0, 1);
    return false;
  }
  const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
  if (!part || size > part->size) {
    ota_send_status(FW_STATUS_ERROR, 0, 2);
    return false;
  }
  if (s_ota_handle) {
    esp_ota_abort(s_ota_handle);
    s_ota_handle = 0;
  }
  esp_err_t err = esp_ota_begin(part, size, &s_ota_handle);
  if (err != ESP_OK) {
    ota_send_status(FW_STATUS_ERROR, 0, 3);
    return false;
  }
  s_update_part = part;
  s_image_size = size;
  s_expected_crc = crc;
  s_expected_version = version;
  s_next_seq = 0;
  s_bytes_written = 0;
  s_running_crc = 0xFFFFFFFF;
  s_active = true;
  s_last_activity_ms = platform_millis();
  ota_send_status(FW_STATUS_READY, 0, size);
  ESP_LOGI(TAG, "ready size=%lu crc=0x%08lX ver=0x%04X",
           (unsigned long)size, (unsigned long)crc, (unsigned)version);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
  ws2812_request_find_me(1);
#endif
  return true;
}

bool node_ota_is_active(void) {
  return s_active;
}

void node_ota_set_node_id(uint8_t node_id) {
  s_node_id = node_id;
}

void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc) {
  if (!data || dlc < 1)
    return;
  s_last_activity_ms = platform_millis();
  const uint8_t cmd = data[0];

  if (cmd == FW_CMD_ABORT) {
    ota_abort(dlc >= 2 ? data[1] : 0);
    return;
  }

  if (cmd == FW_CMD_BEGIN) {
    if (dlc < 7)
      return;
    const uint8_t part = data[1];
    if (part == FW_BEGIN_PART0) {
      s_begin_part1 = false;
      s_begin_part0 = true;
      s_expected_crc = 0;
      s_image_size = (uint32_t)data[2] | ((uint32_t)data[3] << 8)
                   | ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
      s_expected_crc = (s_expected_crc & 0xFFFFFF00UL) | data[6];
    } else if (part == FW_BEGIN_PART1) {
      s_begin_part1 = true;
      s_expected_crc = (uint32_t)data[2] | ((uint32_t)data[3] << 8)
                     | ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
      s_expected_version = (uint16_t)data[6];
      if (dlc >= 8)
        s_expected_version |= (uint16_t)((uint16_t)data[7] << 8);
      if (s_begin_part0 && s_begin_part1) {
        s_begin_part0 = false;
        s_begin_part1 = false;
        ota_begin_session(s_image_size, s_expected_crc, s_expected_version);
      }
    }
    return;
  }

  if (!s_active)
    return;

  if (cmd == FW_CMD_DATA) {
    if (dlc < 3)
      return;
    const uint16_t seq = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    if (seq != s_next_seq) {
      ota_send_status(FW_STATUS_NACK, s_next_seq, seq);
      return;
    }
    const uint8_t n = (uint8_t)(dlc - 3);
    if (n == 0 || n > FW_DATA_BYTES_PER_FRAME)
      return;
    if (s_bytes_written + n > s_image_size) {
      ota_abort(4);
      return;
    }
    esp_err_t werr = esp_ota_write(s_ota_handle, &data[3], n);
    if (werr != ESP_OK) {
      ota_abort(5);
      return;
    }
    s_running_crc = crc32_update(s_running_crc, &data[3], n);
    s_bytes_written += (uint32_t)n;
    s_next_seq++;
    ota_send_status(FW_STATUS_ACK, seq, s_bytes_written);
    return;
  }

  if (cmd == FW_CMD_END) {
    if (s_bytes_written != s_image_size) {
      ota_abort(6);
      return;
    }
    uint32_t crc = crc32_finalize(s_running_crc);
    if (crc != s_expected_crc) {
      ESP_LOGW(TAG, "CRC mismatch got=0x%08lX exp=0x%08lX",
               (unsigned long)crc, (unsigned long)s_expected_crc);
      ota_abort(7);
      return;
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
    ota_send_status(FW_STATUS_COMPLETE, s_next_seq, s_bytes_written);
    ESP_LOGI(TAG, "complete, waiting for hub reboot cmd");
  }
}

void node_ota_service(unsigned long now_ms) {
  if (s_active && s_last_activity_ms != 0
      && (now_ms - s_last_activity_ms) >= FW_OTA_IDLE_TIMEOUT_MS) {
    ota_abort(9);
  }
}

#endif /* NODE_ROLE_MIN */
