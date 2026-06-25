#include "ws2812_task.h"

#if defined(WS2812_ENABLE) && WS2812_ENABLE

#include "ws2812_effects.h"
#include "ws2812_hal.h"
#include "ws2812_types.h"
#include "../config_store.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static const char* TAG = "ws2812";

#define NOW_MS() ((unsigned long)pdTICKS_TO_MS(xTaskGetTickCount()))
#define OTA_BLINK_MS 250U

enum class State : uint8_t {
  BootTest,
  Baseline,
  Strobe,
  Chase,
  FindMe,
  OtaTransfer,
  OtaFailed,
  OtaSuccess,
};

static QueueHandle_t s_cmd_queue = nullptr;
static QueueHandle_t s_night_queue = nullptr;
static bool s_started = false;

static State s_state = State::BootTest;
static Ws2812RuntimeConfig s_cfg = {};

static unsigned long s_boot_until_ms = 0;
static unsigned long s_strobe_until_ms = 0;
static unsigned long s_strobe_last_toggle_ms = 0;
static bool s_strobe_on = false;
static unsigned long s_chase_start_ms = 0;
static unsigned long s_chase_last_step_ms = 0;
static uint8_t s_chase_step = 0;
static unsigned long s_find_me_until_ms = 0;
static unsigned long s_find_me_last_toggle_ms = 0;
static bool s_find_me_on = false;
static unsigned long s_ota_transfer_last_toggle_ms = 0;
static bool s_ota_transfer_on = false;

static bool in_ota_visual_state(void) {
  return s_state == State::OtaTransfer || s_state == State::OtaFailed
      || s_state == State::OtaSuccess;
}

static void load_effect_params_from_nvs(void) {
  ws2812_config_set_defaults(&s_cfg);
  config_get_night_light(&s_cfg.night_on, &s_cfg.night_brightness);
  config_get_ws2812_click_effect(&s_cfg.click_effect);
  config_get_ws2812_effect_params(WS2812_EFFECT_STROBE, &s_cfg.strobe.rgb.r, &s_cfg.strobe.rgb.g,
                                &s_cfg.strobe.rgb.b, &s_cfg.strobe.timing_ms);
  config_get_ws2812_effect_params(WS2812_EFFECT_CHASE, &s_cfg.chase.rgb.r, &s_cfg.chase.rgb.g,
                                  &s_cfg.chase.rgb.b, &s_cfg.chase.timing_ms);
  config_get_ws2812_effect_params(WS2812_EFFECT_FIND_ME, &s_cfg.find_me.rgb.r, &s_cfg.find_me.rgb.g,
                                  &s_cfg.find_me.rgb.b, &s_cfg.find_me.timing_ms);
}

static bool show_pixels(void) {
  if (!ws2812_hal_refresh()) {
    ESP_LOGW(TAG, "refresh failed");
    return false;
  }
  return true;
}

static void fill_rgb(uint8_t r, uint8_t g, uint8_t b) {
  ws2812_hal_fill_rgb(r, g, b);
  show_pixels();
}

static void apply_baseline(void) {
  if (s_cfg.night_on && s_cfg.night_brightness > 0) {
    const Rgb8 c = ws2812_night_rgb(s_cfg.night_brightness);
    fill_rgb(c.r, c.g, c.b);
  } else {
    fill_rgb(0, 0, 0);
  }
}

static void enter_baseline_state(void) {
  s_state = State::Baseline;
  s_ota_transfer_last_toggle_ms = 0;
  s_ota_transfer_on = false;
  apply_baseline();
}

static void cancel_transient(void) {
  s_strobe_until_ms = 0;
  s_chase_start_ms = 0;
  s_chase_last_step_ms = 0;
  s_chase_step = 0;
  s_find_me_until_ms = 0;
  s_find_me_last_toggle_ms = 0;
  s_find_me_on = false;
}

static void start_strobe(void) {
  cancel_transient();
  s_state = State::Strobe;
  const unsigned long now = NOW_MS();
  s_strobe_on = true;
  s_strobe_last_toggle_ms = now;
  s_strobe_until_ms = now + ws2812_strobe_total_ms(s_cfg.strobe.timing_ms);
  fill_rgb(s_cfg.strobe.rgb.r, s_cfg.strobe.rgb.g, s_cfg.strobe.rgb.b);
}

static void start_chase(void) {
  cancel_transient();
  s_state = State::Chase;
  const unsigned long now = NOW_MS();
  s_chase_start_ms = now;
  s_chase_last_step_ms = 0;
  s_chase_step = 0;
  ws2812_hal_fill_rgb(0, 0, 0);
  ws2812_hal_set_pixel(0, s_cfg.chase.rgb.r, s_cfg.chase.rgb.g, s_cfg.chase.rgb.b);
  show_pixels();
  s_chase_step = 1;
  s_chase_last_step_ms = now;
}

static void end_chase(void) {
  cancel_transient();
  enter_baseline_state();
}

static void trigger_click(void) {
  cancel_transient();
  if (s_cfg.click_effect != 0)
    start_chase();
  else
    start_strobe();
}

static void start_find_me(uint8_t duration_min) {
  if (duration_min == 0) duration_min = 5;
  if (duration_min > 30) duration_min = 30;
  cancel_transient();
  s_state = State::FindMe;
  const unsigned long now = NOW_MS();
  s_find_me_until_ms = now + (unsigned long)duration_min * 60UL * 1000UL;
  s_find_me_last_toggle_ms = 0;
  s_find_me_on = true;
  fill_rgb(s_cfg.find_me.rgb.r, s_cfg.find_me.rgb.g, s_cfg.find_me.rgb.b);
  ESP_LOGI(TAG, "find-me %u min", (unsigned)duration_min);
}

static void end_find_me(void) {
  cancel_transient();
  enter_baseline_state();
  ESP_LOGI(TAG, "find-me ended");
}

static void start_ota_transfer(void) {
  cancel_transient();
  s_state = State::OtaTransfer;
  const unsigned long now = NOW_MS();
  s_ota_transfer_last_toggle_ms = now;
  s_ota_transfer_on = true;
  fill_rgb(255, 0, 0);
  ESP_LOGI(TAG, "OTA transfer (flash red)");
}

static void start_ota_failed(void) {
  cancel_transient();
  s_state = State::OtaFailed;
  s_ota_transfer_last_toggle_ms = 0;
  s_ota_transfer_on = false;
  fill_rgb(255, 0, 0);
  ESP_LOGI(TAG, "OTA failed (solid red)");
}

static void start_ota_success(void) {
  cancel_transient();
  s_state = State::OtaSuccess;
  s_ota_transfer_last_toggle_ms = 0;
  s_ota_transfer_on = false;
  fill_rgb(0, 255, 0);
  ESP_LOGI(TAG, "OTA success (solid green)");
}

static void apply_night_light(bool on, uint8_t brightness_0_100) {
  if (in_ota_visual_state())
    return;
  if (on && brightness_0_100 == 0) {
    ESP_LOGI(TAG, "night light on brightness=0, using minimum %u%%",
             (unsigned)WS2812_NIGHT_MIN_BRIGHTNESS);
  }
  cancel_transient();
  s_cfg.night_on = on;
  s_cfg.night_brightness = ws2812_normalize_night_brightness(on, brightness_0_100);
  enter_baseline_state();
  ESP_LOGI(TAG, "applied night light %s brightness=%u", on ? "on" : "off",
           (unsigned)s_cfg.night_brightness);
}

static void handle_cmd(const Ws2812Cmd& cmd) {
  switch (cmd.type) {
    case WS2812_CMD_SET_NIGHT_LIGHT:
      apply_night_light(cmd.u.night.on, cmd.u.night.brightness);
      break;
    case WS2812_CMD_TRIGGER_CLICK:
      if (s_state == State::OtaFailed) {
        enter_baseline_state();
        ESP_LOGI(TAG, "OTA failed cleared by click");
      } else if (!in_ota_visual_state()) {
        trigger_click();
      }
      break;
    case WS2812_CMD_START_FIND_ME:
      if (!in_ota_visual_state())
        start_find_me(cmd.u.find_me_min);
      break;
    case WS2812_CMD_SET_CLICK_EFFECT:
      s_cfg.click_effect = (cmd.u.click_effect != 0) ? 1 : 0;
      break;
    case WS2812_CMD_SET_EFFECT_PARAMS: {
      const Ws2812EffectParamsCmd* p = &cmd.u.effect_params;
      config_set_ws2812_effect_params(p->effect_id, p->rgb.r, p->rgb.g, p->rgb.b, p->timing_ms);
      Ws2812EffectParams* dst = nullptr;
      if (p->effect_id == WS2812_EFFECT_STROBE) dst = &s_cfg.strobe;
      else if (p->effect_id == WS2812_EFFECT_CHASE) dst = &s_cfg.chase;
      else if (p->effect_id == WS2812_EFFECT_FIND_ME) dst = &s_cfg.find_me;
      if (dst) {
        dst->rgb = p->rgb;
        dst->timing_ms = p->timing_ms;
      }
      break;
    }
    case WS2812_CMD_RUN_BOOT_TEST:
      cancel_transient();
      s_state = State::BootTest;
      s_boot_until_ms = NOW_MS() + WS2812_BOOT_MS;
      fill_rgb(255, 0, 0);
      break;
    case WS2812_CMD_LOAD_NVS_BASELINE:
      load_effect_params_from_nvs();
      enter_baseline_state();
      break;
    case WS2812_CMD_OTA_TRANSFER:
      if (s_state != State::OtaTransfer)
        start_ota_transfer();
      break;
    case WS2812_CMD_OTA_FAILED:
      start_ota_failed();
      break;
    case WS2812_CMD_OTA_SUCCESS:
      start_ota_success();
      break;
    default:
      break;
  }
}

static void drain_commands(void) {
  Ws2812Cmd cmd = {};
  while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE)
    handle_cmd(cmd);

  Ws2812NightLightCmd night = {};
  if (xQueueReceive(s_night_queue, &night, 0) == pdTRUE) {
    Ws2812Cmd nc = {};
    nc.type = WS2812_CMD_SET_NIGHT_LIGHT;
    nc.u.night = night;
    handle_cmd(nc);
  }
}

static void tick_boot(unsigned long now) {
  if (s_boot_until_ms == 0 || now < s_boot_until_ms)
    return;
  s_boot_until_ms = 0;
  enter_baseline_state();
}

static void tick_strobe(unsigned long now) {
  if (now >= s_strobe_until_ms) {
    enter_baseline_state();
    return;
  }
  const uint16_t half = s_cfg.strobe.timing_ms;
  if (s_strobe_last_toggle_ms != 0 && (now - s_strobe_last_toggle_ms) < half)
    return;
  s_strobe_on = !s_strobe_on;
  s_strobe_last_toggle_ms = now;
  if (s_strobe_on)
    fill_rgb(s_cfg.strobe.rgb.r, s_cfg.strobe.rgb.g, s_cfg.strobe.rgb.b);
  else
    apply_baseline();
}

static void tick_chase(unsigned long now) {
  if (s_chase_start_ms != 0 && (now - s_chase_start_ms) >= WS2812_CHASE_MAX_MS) {
    end_chase();
    return;
  }
  const uint8_t total = ws2812_chase_total_steps(WS2812_COUNT);
  if (total == 0) {
    end_chase();
    return;
  }
  if (s_chase_last_step_ms != 0 && (now - s_chase_last_step_ms) < s_cfg.chase.timing_ms)
    return;

  const uint8_t idx = ws2812_chase_index_for_step(s_chase_step, WS2812_COUNT);
  ws2812_hal_fill_rgb(0, 0, 0);
  ws2812_hal_set_pixel(idx, s_cfg.chase.rgb.r, s_cfg.chase.rgb.g, s_cfg.chase.rgb.b);
  show_pixels();
  s_chase_last_step_ms = now;
  ++s_chase_step;
  if (s_chase_step >= total)
    end_chase();
}

static void tick_find_me(unsigned long now) {
  if (now >= s_find_me_until_ms) {
    end_find_me();
    return;
  }
  if (s_find_me_last_toggle_ms != 0 && (now - s_find_me_last_toggle_ms) < s_cfg.find_me.timing_ms)
    return;
  s_find_me_on = !s_find_me_on;
  s_find_me_last_toggle_ms = now;
  if (s_find_me_on)
    fill_rgb(s_cfg.find_me.rgb.r, s_cfg.find_me.rgb.g, s_cfg.find_me.rgb.b);
  else
    fill_rgb(0, 0, 0);
}

static void tick_ota_transfer(unsigned long now) {
  if (s_ota_transfer_last_toggle_ms != 0
      && (now - s_ota_transfer_last_toggle_ms) < OTA_BLINK_MS)
    return;
  s_ota_transfer_on = !s_ota_transfer_on;
  s_ota_transfer_last_toggle_ms = now;
  if (s_ota_transfer_on)
    fill_rgb(255, 0, 0);
  else
    fill_rgb(0, 0, 0);
}

static void tick_state(unsigned long now) {
  switch (s_state) {
    case State::BootTest:
      tick_boot(now);
      break;
    case State::Strobe:
      tick_strobe(now);
      break;
    case State::Chase:
      tick_chase(now);
      break;
    case State::FindMe:
      tick_find_me(now);
      break;
    case State::OtaTransfer:
      tick_ota_transfer(now);
      break;
    case State::Baseline:
    case State::OtaFailed:
    case State::OtaSuccess:
      break;
    default:
      break;
  }
}

static bool post_cmd(const Ws2812Cmd& cmd) {
  if (!s_cmd_queue) return false;
  if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
    ESP_LOGW(TAG, "cmd queue full");
    return false;
  }
  return true;
}

static void ws2812_task(void* arg) {
  (void)arg;
  load_effect_params_from_nvs();

  if (!ws2812_hal_init()) {
    ESP_LOGE(TAG, "driver init failed");
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(TAG, "driver init %u LEDs GPIO %d RMT", (unsigned)WS2812_COUNT, (int)WS2812_GPIO);
  s_boot_until_ms = NOW_MS() + WS2812_BOOT_MS;
  fill_rgb(255, 0, 0);

  for (;;) {
    drain_commands();
    tick_state(NOW_MS());
    vTaskDelay(1);
  }
}

void ws2812_task_start(void) {
  if (s_started) return;
  s_started = true;
  s_cmd_queue = xQueueCreate(12, sizeof(Ws2812Cmd));
  s_night_queue = xQueueCreate(1, sizeof(Ws2812NightLightCmd));
  if (!s_cmd_queue || !s_night_queue) {
    ESP_LOGE(TAG, "queue create failed");
    return;
  }
  xTaskCreate(ws2812_task, "ws2812", 12288, NULL, 6, NULL);
}

void ws2812_task_post_set_night_light(bool on, uint8_t brightness_0_100) {
  if (!s_night_queue) return;
  if (brightness_0_100 > 100) brightness_0_100 = 100;
  Ws2812NightLightCmd night = {on, ws2812_normalize_night_brightness(on, brightness_0_100)};
  xQueueOverwrite(s_night_queue, &night);
}

void ws2812_task_post_trigger_click(void) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_TRIGGER_CLICK;
  post_cmd(cmd);
}

void ws2812_task_post_find_me(uint8_t duration_min) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_START_FIND_ME;
  cmd.u.find_me_min = duration_min;
  post_cmd(cmd);
}

void ws2812_task_post_ota_transfer(void) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_OTA_TRANSFER;
  post_cmd(cmd);
}

void ws2812_task_post_ota_failed(void) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_OTA_FAILED;
  post_cmd(cmd);
}

void ws2812_task_post_ota_success(void) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_OTA_SUCCESS;
  post_cmd(cmd);
}

void ws2812_task_post_set_click_effect(uint8_t effect) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_SET_CLICK_EFFECT;
  cmd.u.click_effect = effect;
  post_cmd(cmd);
}

void ws2812_task_post_set_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,
                                        uint16_t timing_ms) {
  Ws2812Cmd cmd = {};
  cmd.type = WS2812_CMD_SET_EFFECT_PARAMS;
  cmd.u.effect_params.effect_id = effect_id;
  cmd.u.effect_params.rgb = {r, g, b};
  cmd.u.effect_params.timing_ms = timing_ms;
  post_cmd(cmd);
}

#endif
