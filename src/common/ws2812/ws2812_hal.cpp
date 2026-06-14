#include "ws2812_hal.h"

#if defined(WS2812_ENABLE) && WS2812_ENABLE

#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include "led_strip_rmt_encoder.h"
#include "led_strip_types.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

static const char* TAG = "ws2812_hal";

static rmt_channel_handle_t s_rmt = nullptr;
static rmt_encoder_handle_t s_encoder = nullptr;
static uint8_t s_pixels[WS2812_COUNT * 3];
static bool s_inited = false;

static constexpr uint32_t kRmtResolutionHz = 10 * 1000 * 1000;

bool ws2812_hal_init(void) {
  if (s_inited)
    return true;

  gpio_reset_pin((gpio_num_t)WS2812_GPIO);

  rmt_tx_channel_config_t chan_cfg = {};
  chan_cfg.gpio_num = (gpio_num_t)WS2812_GPIO;
  chan_cfg.clk_src = RMT_CLK_SRC_DEFAULT;
  chan_cfg.resolution_hz = kRmtResolutionHz;
  chan_cfg.mem_block_symbols = 64;
  chan_cfg.trans_queue_depth = 4;

  esp_err_t err = rmt_new_tx_channel(&chan_cfg, &s_rmt);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_tx_channel failed: %d", (int)err);
    s_rmt = nullptr;
    return false;
  }

  const led_strip_encoder_config_t enc_cfg = {
    .resolution = kRmtResolutionHz,
    .led_model = LED_MODEL_WS2812,
  };
  err = rmt_new_led_strip_encoder(&enc_cfg, &s_encoder);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_new_led_strip_encoder failed: %d", (int)err);
    rmt_del_channel(s_rmt);
    s_rmt = nullptr;
    return false;
  }

  err = rmt_enable(s_rmt);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "rmt_enable failed: %d", (int)err);
    rmt_del_encoder(s_encoder);
    s_encoder = nullptr;
    rmt_del_channel(s_rmt);
    s_rmt = nullptr;
    return false;
  }

  memset(s_pixels, 0, sizeof(s_pixels));
  s_inited = true;
  ESP_LOGI(TAG, "init %u LEDs GPIO %d RMT", (unsigned)WS2812_COUNT, WS2812_GPIO);
  return true;
}

bool ws2812_hal_ready(void) {
  return s_inited && s_rmt && s_encoder;
}

void ws2812_hal_deinit(void) {
  if (!s_inited)
    return;

  if (s_rmt) {
    (void)rmt_tx_wait_all_done(s_rmt, portMAX_DELAY);
    (void)rmt_disable(s_rmt);
    rmt_del_channel(s_rmt);
    s_rmt = nullptr;
  }
  if (s_encoder) {
    rmt_del_encoder(s_encoder);
    s_encoder = nullptr;
  }
  gpio_reset_pin((gpio_num_t)WS2812_GPIO);
  s_inited = false;
}

void ws2812_hal_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= WS2812_COUNT)
    return;
  const size_t off = (size_t)index * 3;
  s_pixels[off] = g;
  s_pixels[off + 1] = r;
  s_pixels[off + 2] = b;
}

void ws2812_hal_fill_rgb(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < WS2812_COUNT; i++)
    ws2812_hal_set_pixel(i, r, g, b);
}

bool ws2812_hal_refresh(void) {
  if (!ws2812_hal_ready())
    return false;

  rmt_transmit_config_t tx_cfg = {};
  tx_cfg.loop_count = 0;

  esp_err_t err = rmt_transmit(s_rmt, s_encoder, s_pixels, sizeof(s_pixels), &tx_cfg);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "rmt_transmit failed: %d", (int)err);
    return false;
  }

  err = rmt_tx_wait_all_done(s_rmt, portMAX_DELAY);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "rmt_tx_wait_all_done failed: %d", (int)err);
    return false;
  }

  return true;
}

#else

bool ws2812_hal_init(void) { return false; }
bool ws2812_hal_ready(void) { return false; }
void ws2812_hal_deinit(void) {}
void ws2812_hal_set_pixel(uint16_t, uint8_t, uint8_t, uint8_t) {}
void ws2812_hal_fill_rgb(uint8_t, uint8_t, uint8_t) {}
bool ws2812_hal_refresh(void) { return false; }

#endif
