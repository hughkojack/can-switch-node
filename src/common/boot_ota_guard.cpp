#include "boot_ota_guard.h"

#if defined(NODE_ROLE_MIN) && defined(OTA_CAPABLE)

#include "platform_time.h"
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_log.h>
#include <stdio.h>

static const char* TAG = "boot_ota";

static bool partition_has_valid_app(const esp_partition_t* part) {
  if (!part || part->type != ESP_PARTITION_TYPE_APP)
    return false;
  esp_partition_pos_t pos = {
    .offset = part->address,
    .size = part->size,
  };
  esp_image_metadata_t meta;
  return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK;
}

void boot_ota_guard(void) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();
  if (!running) {
    ESP_LOGW(TAG, "no running partition");
    return;
  }

  ESP_LOGI(TAG, "running=%s @ 0x%lx boot=%s",
           running->label,
           (unsigned long)running->address,
           boot ? boot->label : "?");

  if (!partition_has_valid_app(running)) {
    ESP_LOGW(TAG, "running slot invalid — trying other OTA slot");
    const esp_partition_t* other = esp_ota_get_next_update_partition(running);
    if (other && partition_has_valid_app(other)) {
      ESP_LOGI(TAG, "switching to %s @ 0x%lx", other->label, (unsigned long)other->address);
      if (esp_ota_set_boot_partition(other) == ESP_OK) {
        ESP_LOGI(TAG, "rebooting into recovered image");
        platform_delay_ms(50);
        esp_restart();
      }
    }
    ESP_LOGE(TAG, "no valid OTA slot found (USB erase + upload required)");
    return;
  }

  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      ESP_LOGI(TAG, "mark app valid -> %d", (int)err);
    }
  }
}

#else

void boot_ota_guard(void) {}

#endif
