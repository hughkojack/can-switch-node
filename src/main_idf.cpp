/**
 * Mechanical node firmware entry — ESP-IDF (XIAO ESP32-S3 / ESP32-C3 + MCP2515 CAN).
 */
#include "common/can.h"
#include "common/can_driver.h"
#include "common/input_engine.h"
#include "common/config_store.h"
#include "common/platform_time.h"
#include "common/platform_gpio.h"
#include "board/board_config.h"

#include "common/ws2812_strip.h"
#include "common/node_ota.h"
#include "common/boot_ota_guard.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_system.h"

static const char* TAG = "main";

#ifndef FIND_ME_GPIO_MAX
#define FIND_ME_GPIO_MAX 48
#endif

static node_config_t cfg;
#if !(defined(WS2812_ENABLE) && WS2812_ENABLE)
static uint32_t find_me_until = 0;
static uint8_t find_me_output_index = 0;
#endif
static uint8_t can_link_indicator_gpio = 0xFF;
static input_timing_t s_timing;

static uint32_t s_last_can_rx_ms = 0;
static uint32_t s_last_hub_to_node_rx_ms = 0;
#define HUB_LINK_THRESHOLD_MS 30000

static spi_device_handle_t s_can_spi_handle = nullptr;

static void on_input_click_flash(uint8_t input_id, uint8_t event_code) {
  (void)input_id;
  if (event_code == EVT_CLICK && node_ota_pending_reboot()) {
    ESP_LOGI(TAG, "OTA success: click reboot");
    esp_restart();
    return;
  }
  if (event_code == EVT_CLICK || event_code == EVT_DOUBLE_CLICK) {
#if defined(WS2812_ENABLE) && WS2812_ENABLE
    ws2812_request_trigger_input_effect();
#endif
  }
}

#if defined(WS2812_ENABLE) && WS2812_ENABLE
static void input_engine_reinit(void) {
  config_get_timing(&s_timing);
  input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
  input_engine_set_event_callback(on_input_click_flash);
}
#endif

static esp_err_t init_can_spi(void) {
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = CAN_SPI_MOSI_GPIO;
  buscfg.miso_io_num = CAN_SPI_MISO_GPIO;
  buscfg.sclk_io_num = CAN_SPI_SCK_GPIO;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = 4096;

  esp_err_t ret = spi_bus_initialize(CAN_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    return ret;

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = 10 * 1000 * 1000;
  devcfg.mode = 0;
  devcfg.spics_io_num = CAN_CS_GPIO;
  devcfg.queue_size = 7;

  ret = spi_bus_add_device(CAN_SPI_HOST, &devcfg, &s_can_spi_handle);
  return ret;
}

static void handle_can_messages(void) {
  twai_message_t message;

  for (;;) {
    twai_status_info_t status = CAN.getStatus();
    if (status.msgs_to_rx == 0)
      break;
    if (CAN.read(&message) != ESP_OK)
      break;

    uint8_t msg_type = (uint8_t)((message.identifier >> 7) & 0x0F);
    uint8_t target_id = (uint8_t)(message.identifier & 0x7F);

    if (target_id == cfg.node_id) {
      if (msg_type == CAN_MSG_SENSOR_DATA || msg_type == CAN_MSG_NODE_CONFIG
          || msg_type == CAN_MSG_STATE_FEEDBACK || msg_type == CAN_MSG_OTA_REMOTE)
        s_last_hub_to_node_rx_ms = platform_millis();
    }

    if (msg_type == CAN_MSG_OTA_REMOTE && target_id == cfg.node_id && message.data_length_code >= 3) {
      node_ota_on_can_frame(message.data, message.data_length_code);
    } else if (msg_type == CAN_MSG_NODE_CONFIG && target_id == cfg.node_id && message.data_length_code >= 1) {
      if (node_ota_is_active()) {
        if (message.data[0] != CMD_REBOOT)
          continue;
      }
      uint8_t cmd = message.data[0];
      if (cmd == CMD_SET_NODE_ID && message.data_length_code >= 2) {
        uint8_t new_id = message.data[1];
        if (new_id != 0 && new_id <= 126) {
          cfg.node_id = new_id;
          config_save(&cfg);
          node_ota_set_node_id(cfg.node_id);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
          input_engine_reinit();
#else
          config_get_timing(&s_timing);
          input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
#endif
          ESP_LOGI(TAG, "CONFIG: node_id set to %u", (unsigned)cfg.node_id);
        }
      } else if (cmd == CMD_SET_INPUT_CFG && message.data_length_code >= 4) {
        uint8_t idx = message.data[1];
        if (idx < MAX_INPUTS_PER_NODE) {
          cfg.inputs[idx].input_id = message.data[2];
          cfg.inputs[idx].mode = (input_mode_t)(message.data[3] & 1);
          if (message.data_length_code >= 5) {
#if defined(WS2812_ENABLE) && WS2812_ENABLE
            if (message.data[4] == WS2812_GPIO) {
              ESP_LOGW(TAG, "CONFIG: reject input GPIO %u (WS2812 data pin)", (unsigned)WS2812_GPIO);
            } else
#endif
            cfg.input_gpio[idx] = message.data[4];
          }
          if (message.data_length_code >= 6)
            cfg.input_active_high[idx] = message.data[5] & 1;
          config_save(&cfg);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
          input_engine_reinit();
#else
          config_get_timing(&s_timing);
          input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
#endif
          ESP_LOGI(TAG, "CONFIG: input[%u] id=%u gpio=%u", (unsigned)idx,
                   (unsigned)cfg.inputs[idx].input_id, (unsigned)cfg.input_gpio[idx]);
        }
      } else if (cmd == CMD_SET_INPUT_COUNT && message.data_length_code >= 2) {
        uint8_t n = message.data[1];
        if (n >= 1 && n <= 6) {
          cfg.input_count = n;
          config_save(&cfg);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
          input_engine_reinit();
#else
          config_get_timing(&s_timing);
          input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
#endif
          ESP_LOGI(TAG, "CONFIG: input_count set to %u", (unsigned)cfg.input_count);
        }
      } else if (cmd == CMD_SET_TIMING && message.data_length_code >= 6) {
        uint8_t part = message.data[1];
        config_get_timing(&s_timing);
        if (part == 0) {
          s_timing.click_max_ms = (uint16_t)message.data[2] | ((uint16_t)message.data[3] << 8);
          s_timing.double_click_gap_ms = (uint16_t)message.data[4] | ((uint16_t)message.data[5] << 8);
        } else if (part == 1) {
          s_timing.hold_min_ms = (uint16_t)message.data[2] | ((uint16_t)message.data[3] << 8);
          s_timing.long_hold_min_ms = (uint16_t)message.data[4] | ((uint16_t)message.data[5] << 8);
        }
        config_set_timing(&s_timing);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        input_engine_reinit();
#else
        input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
#endif
      } else if (cmd == CMD_FIND_ME) {
        uint8_t duration_min = (message.data_length_code >= 2) ? message.data[1] : 5;
        if (duration_min == 0) duration_min = 5;
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        ws2812_request_find_me(duration_min);
        ESP_LOGI(TAG, "CONFIG: find-me on for %u min (WS2812)", (unsigned)duration_min);
#else
        config_get_find_me_output(&find_me_output_index);
        if (find_me_output_index <= FIND_ME_GPIO_MAX) {
          platform_gpio_output(find_me_output_index);
          if (find_me_output_index == 0)
            platform_gpio_write(find_me_output_index, true);
        }
        find_me_until = platform_millis() + (uint32_t)duration_min * 60 * 1000;
#endif
      } else if (cmd == CMD_SET_FIND_ME_OUTPUT && message.data_length_code >= 2) {
#if !(defined(WS2812_ENABLE) && WS2812_ENABLE)
        config_set_find_me_output(message.data[1]);
        config_get_find_me_output(&find_me_output_index);
#endif
      } else if (cmd == CMD_SET_CAN_LINK_INDICATOR && message.data_length_code >= 2) {
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        if (message.data[1] == WS2812_GPIO) {
          ESP_LOGW(TAG, "CONFIG: reject CAN link GPIO %u (WS2812 pin)", (unsigned)WS2812_GPIO);
        } else
#endif
        config_set_can_link_indicator_gpio(message.data[1]);
        config_get_can_link_indicator_gpio(&can_link_indicator_gpio);
      } else if (cmd == CMD_SET_NIGHT_LIGHT && message.data_length_code >= 3) {
        uint8_t enabled = message.data[1] ? 1 : 0;
        uint8_t brightness = message.data[2];
        if (brightness > 100) brightness = 100;
        config_set_night_light(enabled != 0, brightness);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        ws2812_request_night_light(enabled != 0, brightness);
#endif
      } else if (cmd == CMD_SET_WS2812_CLICK_EFFECT && message.data_length_code >= 2) {
        uint8_t effect = message.data[1] ? WS2812_CLICK_EFFECT_CHASE : WS2812_CLICK_EFFECT_STROBE;
        config_set_ws2812_click_effect(effect);
        ws2812_post_set_click_effect(effect);
      } else if (cmd == CMD_SET_WS2812_EFFECT_PARAMS && message.data_length_code >= 7) {
        const uint8_t effect_id = message.data[1];
        const uint16_t timing_ms =
            (uint16_t)message.data[5] | ((uint16_t)message.data[6] << 8);
        config_set_ws2812_effect_params(effect_id, message.data[2], message.data[3],
                                        message.data[4], timing_ms);
        ws2812_post_set_effect_params(effect_id, message.data[2], message.data[3],
                                      message.data[4], timing_ms);
      } else if (cmd == CMD_REBOOT) {
        ESP_LOGI(TAG, "CONFIG: reboot requested from hub");
        esp_restart();
      }
    }

    s_last_can_rx_ms = platform_millis();
  }
  node_ota_drain_notifications();
}

static void can_poll_task(void* arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(500));
  node_ota_set_poll_task(xTaskGetCurrentTaskHandle());

  static uint32_t last_heartbeat_ms = 0;
#if !(defined(WS2812_ENABLE) && WS2812_ENABLE)
  static uint32_t find_me_last_toggle_ms = 0;
  static bool find_me_blink_high = false;
  const uint32_t FIND_ME_BLINK_INTERVAL_MS = 150;
#endif
  const uint8_t node_type = NODE_TYPE_MECHANICAL;

  can_send_node_announce(cfg.node_id, node_type, cfg.input_count);
  last_heartbeat_ms = platform_millis();

  for (;;) {
    const uint32_t now_ms = platform_millis();
    ulTaskNotifyTake(pdTRUE, 0);
    handle_can_messages();
    node_ota_service(now_ms);
    const bool ota_active = node_ota_is_active();

    if (!ota_active) {
      input_engine_update();

      for (uint8_t i = 0; i < cfg.input_count && i < MAX_INPUTS_PER_NODE; i++) {
        uint8_t gpio = cfg.input_gpio[i];
        if (gpio == 0xFF)
          gpio = (uint8_t)(i + 1);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        if (gpio == WS2812_GPIO)
          continue;
#endif
        if (gpio != 0xFF) {
          bool active;
          if (cfg.input_active_high[i]) {
            platform_gpio_input_pulldown(gpio);
            active = platform_gpio_read(gpio);
          } else {
            platform_gpio_input_pullup(gpio);
            active = !platform_gpio_read(gpio);
          }
          input_engine_process_level(cfg.inputs[i].input_id, active);
        }
      }
    }

    if (!ota_active) {
      if (last_heartbeat_ms == 0) {
        last_heartbeat_ms = now_ms;
      } else {
        int32_t elapsed = (int32_t)(now_ms - last_heartbeat_ms);
        if (elapsed < 0 || elapsed > 60000) {
          last_heartbeat_ms = now_ms;
        } else if (elapsed >= 15000) {
          can_send_node_announce(cfg.node_id, node_type, cfg.input_count);
          last_heartbeat_ms = now_ms;
        }
      }
    }

#if !(defined(WS2812_ENABLE) && WS2812_ENABLE)
    if (!ota_active && find_me_until && now_ms >= find_me_until) {
      if (find_me_output_index <= FIND_ME_GPIO_MAX)
        platform_gpio_write(find_me_output_index, false);
      find_me_until = 0;
      find_me_last_toggle_ms = 0;
    } else if (!ota_active && find_me_until && find_me_output_index != 0
               && find_me_output_index <= FIND_ME_GPIO_MAX) {
      if (find_me_last_toggle_ms == 0
          || (now_ms - find_me_last_toggle_ms) >= FIND_ME_BLINK_INTERVAL_MS) {
        find_me_blink_high = !find_me_blink_high;
        platform_gpio_write(find_me_output_index, find_me_blink_high);
        find_me_last_toggle_ms = now_ms;
      }
    }
#endif

    if (can_link_indicator_gpio != 0xFF && can_link_indicator_gpio <= FIND_ME_GPIO_MAX
#if defined(WS2812_ENABLE) && WS2812_ENABLE
        && can_link_indicator_gpio != WS2812_GPIO
#endif
        ) {
      static uint32_t can_link_last_toggle_ms = 0;
      static bool can_link_blink_high = false;
      const uint32_t CAN_LINK_BLINK_MS = 500;
      bool link_ok = (now_ms - s_last_hub_to_node_rx_ms <= HUB_LINK_THRESHOLD_MS);
      platform_gpio_output(can_link_indicator_gpio);
      if (link_ok) {
        platform_gpio_write(can_link_indicator_gpio, true);
        can_link_last_toggle_ms = 0;
      } else {
        if (can_link_last_toggle_ms == 0
            || (now_ms - can_link_last_toggle_ms) >= CAN_LINK_BLINK_MS) {
          can_link_blink_high = !can_link_blink_high;
          platform_gpio_write(can_link_indicator_gpio, can_link_blink_high);
          can_link_last_toggle_ms = now_ms;
        }
      }
    }

    vTaskDelay(ota_active ? 0 : pdMS_TO_TICKS(10));
  }
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "BOOT: app_main enter");
  boot_ota_guard();

  config_load(&cfg);
  ESP_LOGI(TAG, "CFG node=%u count=%u", cfg.node_id, cfg.input_count);

  ESP_ERROR_CHECK(init_can_spi());
  platform_delay_ms(500);

  if (CAN.begin(&s_can_spi_handle) != ESP_OK) {
    ESP_LOGE(TAG, "MCP2515 CAN init failed (CS=%d)", CAN_CS_GPIO);
  } else {
    ESP_LOGI(TAG, "CAN running");
  }

  config_get_timing(&s_timing);
  config_get_can_link_indicator_gpio(&can_link_indicator_gpio);
  input_engine_init(&cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);

  node_ota_set_node_id(cfg.node_id);
#if defined(WS2812_ENABLE) && WS2812_ENABLE
  ws2812_start_task();
#endif
  input_engine_set_event_callback(on_input_click_flash);

  can_send_node_announce(cfg.node_id, NODE_TYPE_MECHANICAL, cfg.input_count);
  xTaskCreate(can_poll_task, "can_poll", 12288, NULL, 5, NULL);
}
