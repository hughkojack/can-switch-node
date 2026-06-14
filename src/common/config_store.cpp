#include "config_store.h"
#include "ws2812/ws2812_types.h"
#include <string.h>

#ifdef ARDUINO
#include <Preferences.h>
#include <Arduino.h>
#else
#include "nvs_store.h"
#endif

#define NVS_NAMESPACE "node_cfg"
#define NVS_KEY_CFG "cfg"
#define NVS_KEY_FIND_ME "find_me"
#define NVS_KEY_CAN_LINK "can_link"
#define NVS_KEY_TIMING "timing"
#define NVS_KEY_NIGHT_ON "nl_on"
#define NVS_KEY_NIGHT_BR "nl_br"
#define NVS_KEY_WS2812_FX "ws_fx"
#define NVS_KEY_WS2812_EP "ws_ep"

typedef struct __attribute__((packed)) {
  uint8_t strobe_r;
  uint8_t strobe_g;
  uint8_t strobe_b;
  uint16_t strobe_ms;
  uint8_t chase_r;
  uint8_t chase_g;
  uint8_t chase_b;
  uint16_t chase_ms;
  uint8_t find_r;
  uint8_t find_g;
  uint8_t find_b;
  uint16_t find_ms;
} ws2812_effect_params_nvs_t;

static void ws2812_effect_params_defaults(ws2812_effect_params_nvs_t* p) {
  Ws2812RuntimeConfig cfg;
  ws2812_config_set_defaults(&cfg);
  p->strobe_r = cfg.strobe.rgb.r;
  p->strobe_g = cfg.strobe.rgb.g;
  p->strobe_b = cfg.strobe.rgb.b;
  p->strobe_ms = cfg.strobe.timing_ms;
  p->chase_r = cfg.chase.rgb.r;
  p->chase_g = cfg.chase.rgb.g;
  p->chase_b = cfg.chase.rgb.b;
  p->chase_ms = cfg.chase.timing_ms;
  p->find_r = cfg.find_me.rgb.r;
  p->find_g = cfg.find_me.rgb.g;
  p->find_b = cfg.find_me.rgb.b;
  p->find_ms = cfg.find_me.timing_ms;
}

static bool load_ws2812_effect_params_nvs(ws2812_effect_params_nvs_t* out) {
  ws2812_effect_params_defaults(out);
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true))
    return false;
  if (!prefs.isKey(NVS_KEY_WS2812_EP)) {
    prefs.end();
    return true;
  }
  ws2812_effect_params_nvs_t tmp = {};
  const size_t len = prefs.getBytes(NVS_KEY_WS2812_EP, &tmp, sizeof(tmp));
  prefs.end();
#else
  if (!nvs_store_open_read(NVS_NAMESPACE))
    return false;
  if (!nvs_store_has_key(NVS_KEY_WS2812_EP)) {
    nvs_store_close();
    return true;
  }
  ws2812_effect_params_nvs_t tmp = {};
  const size_t len = nvs_store_get_blob(NVS_KEY_WS2812_EP, &tmp, sizeof(tmp));
  nvs_store_close();
#endif
  if (len != sizeof(tmp))
    return true;
  *out = tmp;
  return true;
}

static bool save_ws2812_effect_params_nvs(const ws2812_effect_params_nvs_t* in) {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false))
    return false;
  const size_t written = prefs.putBytes(NVS_KEY_WS2812_EP, in, sizeof(*in));
  prefs.end();
  return written == sizeof(*in);
#else
  if (!nvs_store_open_write(NVS_NAMESPACE))
    return false;
  const size_t written = nvs_store_put_blob(NVS_KEY_WS2812_EP, in, sizeof(*in));
  nvs_store_close();
  return written == sizeof(*in);
#endif
}

// Default timing (match input_engine compile-time defaults)
static const uint16_t DEFAULT_CLICK_MAX_MS = 500;
static const uint16_t DEFAULT_DOUBLE_CLICK_GAP_MS = 400;
static const uint16_t DEFAULT_HOLD_MIN_MS = 800;
static const uint16_t DEFAULT_LONG_HOLD_MIN_MS = 2000;

static void apply_defaults(node_config_t* out_cfg) {
  out_cfg->node_id = NODE_ID_UNCONFIGURED;
  memset(out_cfg->input_labels, 0, sizeof(out_cfg->input_labels));
  memset(out_cfg->input_gpio, 0xFF, sizeof(out_cfg->input_gpio));
  memset(out_cfg->input_active_high, 0, sizeof(out_cfg->input_active_high));
#if defined(NODE_ROLE_MIN)
  out_cfg->input_count = 1;
  out_cfg->inputs[0] = { 1, INPUT_MODE_TOGGLE };
#else
  // LCD or default: 4 inputs
  out_cfg->input_count = 4;
  out_cfg->inputs[0] = { 1, INPUT_MODE_TOGGLE };
  out_cfg->inputs[1] = { 2, INPUT_MODE_TOGGLE };
  out_cfg->inputs[2] = { 3, INPUT_MODE_TOGGLE };
  out_cfg->inputs[3] = { 4, INPUT_MODE_TOGGLE };
#endif
}

// Size of node_config_t before input_labels was added (for NVS backward compatibility)
#define OLD_NODE_CONFIG_SIZE  (sizeof(uint8_t) * 2 + sizeof(input_cfg_t) * MAX_INPUTS_PER_NODE)
// Size before input_gpio was added (labels present, no GPIO array)
#define NODE_CONFIG_SIZE_WITH_LABELS  (sizeof(uint8_t) * 2 + sizeof(input_cfg_t) * MAX_INPUTS_PER_NODE + MAX_INPUTS_PER_NODE * (MAX_INPUT_LABEL_LEN + 1))
// Saved blob before input_active_high[] was appended (same as current struct minus that array)
#define NODE_CONFIG_SIZE_BEFORE_ACTIVE_HIGH (sizeof(node_config_t) - MAX_INPUTS_PER_NODE)

#ifdef ARDUINO
static bool nvs_ro_begin(Preferences* prefs) {
  return prefs->begin(NVS_NAMESPACE, true);
}
static bool nvs_rw_begin(Preferences* prefs) {
  return prefs->begin(NVS_NAMESPACE, false);
}
#else
static bool nvs_ro_begin(void) {
  return nvs_store_open_read(NVS_NAMESPACE);
}
static bool nvs_rw_begin(void) {
  return nvs_store_open_write(NVS_NAMESPACE);
}
static void nvs_end(void) {
  nvs_store_close();
}
#endif

bool config_load(node_config_t* out_cfg) {
  if (!out_cfg) return false;

#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    apply_defaults(out_cfg);
    return true;
  }
  size_t len = prefs.getBytes(NVS_KEY_CFG, out_cfg, sizeof(node_config_t));
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    apply_defaults(out_cfg);
    return true;
  }
  size_t len = nvs_store_get_blob(NVS_KEY_CFG, out_cfg, sizeof(node_config_t));
  nvs_end();
#endif

  if (len == OLD_NODE_CONFIG_SIZE) {
    // Old NVS layout: zero labels and use the rest
    memset(out_cfg->input_labels, 0, sizeof(out_cfg->input_labels));
    memset(out_cfg->input_gpio, 0xFF, sizeof(out_cfg->input_gpio));
    memset(out_cfg->input_active_high, 0, sizeof(out_cfg->input_active_high));
    if (out_cfg->input_count > MAX_INPUTS_PER_NODE) out_cfg->input_count = MAX_INPUTS_PER_NODE;
    if (out_cfg->input_count < 1) out_cfg->input_count = 1;
    return true;
  }
  if (len == NODE_CONFIG_SIZE_WITH_LABELS) {
    // Layout with labels but no input_gpio: set GPIOs to unassigned
    memset(out_cfg->input_gpio, 0xFF, sizeof(out_cfg->input_gpio));
    memset(out_cfg->input_active_high, 0, sizeof(out_cfg->input_active_high));
    if (out_cfg->input_count > MAX_INPUTS_PER_NODE) out_cfg->input_count = MAX_INPUTS_PER_NODE;
    if (out_cfg->input_count < 1) out_cfg->input_count = 1;
    return true;
  }
  if (len == NODE_CONFIG_SIZE_BEFORE_ACTIVE_HIGH) {
    memset(out_cfg->input_active_high, 0, sizeof(out_cfg->input_active_high));
    if (out_cfg->input_count > MAX_INPUTS_PER_NODE) out_cfg->input_count = MAX_INPUTS_PER_NODE;
    if (out_cfg->input_count < 1) out_cfg->input_count = 1;
    return true;
  }
  if (len != sizeof(node_config_t)) {
    apply_defaults(out_cfg);
    return true;
  }

  // Clamp input_count to hub range 1..6
  if (out_cfg->input_count > MAX_INPUTS_PER_NODE) out_cfg->input_count = MAX_INPUTS_PER_NODE;
  if (out_cfg->input_count < 1) out_cfg->input_count = 1;
  return true;
}

bool config_save(const node_config_t* cfg) {
  if (!cfg) return false;

#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  size_t written = prefs.putBytes(NVS_KEY_CFG, cfg, sizeof(node_config_t));
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  size_t written = nvs_store_put_blob(NVS_KEY_CFG, cfg, sizeof(node_config_t));
  nvs_end();
#endif
  return (written == sizeof(node_config_t));
}

bool config_get_find_me_output(uint8_t* out_index) {
  if (!out_index) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    *out_index = 0;
    return true;
  }
  *out_index = prefs.getUChar(NVS_KEY_FIND_ME, 0);
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    *out_index = 0;
    return true;
  }
  *out_index = nvs_store_get_u8(NVS_KEY_FIND_ME, 0);
  nvs_end();
#endif
  return true;
}

bool config_set_find_me_output(uint8_t index) {
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  prefs.putUChar(NVS_KEY_FIND_ME, index);
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  nvs_store_put_u8(NVS_KEY_FIND_ME, index);
  nvs_end();
#endif
  return true;
}

bool config_get_can_link_indicator_gpio(uint8_t* out_gpio) {
  if (!out_gpio) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    *out_gpio = 0xFF;
    return true;
  }
  *out_gpio = prefs.getUChar(NVS_KEY_CAN_LINK, 0xFF);
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    *out_gpio = 0xFF;
    return true;
  }
  *out_gpio = nvs_store_get_u8(NVS_KEY_CAN_LINK, 0xFF);
  nvs_end();
#endif
  return true;
}

bool config_set_can_link_indicator_gpio(uint8_t gpio) {
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  prefs.putUChar(NVS_KEY_CAN_LINK, gpio);
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  nvs_store_put_u8(NVS_KEY_CAN_LINK, gpio);
  nvs_end();
#endif
  return true;
}

bool config_get_timing(input_timing_t* out_timing) {
  if (!out_timing) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
    return true;
  }
  if (!prefs.isKey(NVS_KEY_TIMING)) {
    prefs.end();
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
    return true;
  }
  size_t len = prefs.getBytes(NVS_KEY_TIMING, out_timing, sizeof(input_timing_t));
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
    return true;
  }
  if (!nvs_store_has_key(NVS_KEY_TIMING)) {
    nvs_end();
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
    return true;
  }
  size_t len = nvs_store_get_blob(NVS_KEY_TIMING, out_timing, sizeof(input_timing_t));
  nvs_end();
#endif
  if (len != sizeof(input_timing_t)) {
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
  }
  return true;
}

bool config_set_timing(const input_timing_t* timing) {
  if (!timing) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  size_t written = prefs.putBytes(NVS_KEY_TIMING, timing, sizeof(input_timing_t));
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  size_t written = nvs_store_put_blob(NVS_KEY_TIMING, timing, sizeof(input_timing_t));
  nvs_end();
#endif
  return (written == sizeof(input_timing_t));
}

bool config_get_night_light(bool* out_on, uint8_t* out_brightness) {
  if (!out_on || !out_brightness) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    *out_on = false;
    *out_brightness = 0;
    return true;
  }
  *out_on = prefs.getBool(NVS_KEY_NIGHT_ON, false);
  *out_brightness = prefs.getUChar(NVS_KEY_NIGHT_BR, 0);
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    *out_on = false;
    *out_brightness = 0;
    return true;
  }
  *out_on = nvs_store_get_bool(NVS_KEY_NIGHT_ON, false);
  *out_brightness = nvs_store_get_u8(NVS_KEY_NIGHT_BR, 0);
  nvs_end();
#endif
  return true;
}

bool config_set_night_light(bool on, uint8_t brightness_0_100) {
  if (brightness_0_100 > 100) brightness_0_100 = 100;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  prefs.putBool(NVS_KEY_NIGHT_ON, on);
  prefs.putUChar(NVS_KEY_NIGHT_BR, brightness_0_100);
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  nvs_store_put_bool(NVS_KEY_NIGHT_ON, on);
  nvs_store_put_u8(NVS_KEY_NIGHT_BR, brightness_0_100);
  nvs_end();
#endif
  return true;
}

bool config_get_ws2812_click_effect(uint8_t* out_effect) {
  if (!out_effect) return false;
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_ro_begin(&prefs)) {
    *out_effect = 0;
    return true;
  }
  *out_effect = prefs.getUChar(NVS_KEY_WS2812_FX, 0) ? 1 : 0;
  prefs.end();
#else
  if (!nvs_ro_begin()) {
    *out_effect = 0;
    return true;
  }
  *out_effect = nvs_store_get_u8(NVS_KEY_WS2812_FX, 0) ? 1 : 0;
  nvs_end();
#endif
  return true;
}

bool config_set_ws2812_click_effect(uint8_t effect) {
#ifdef ARDUINO
  Preferences prefs;
  if (!nvs_rw_begin(&prefs))
    return false;
  prefs.putUChar(NVS_KEY_WS2812_FX, (effect != 0) ? 1 : 0);
  prefs.end();
#else
  if (!nvs_rw_begin())
    return false;
  nvs_store_put_u8(NVS_KEY_WS2812_FX, (effect != 0) ? 1 : 0);
  nvs_end();
#endif
  return true;
}

static uint16_t clamp_timing_ms(uint16_t timing_ms) {
  if (timing_ms < 10) return 10;
  if (timing_ms > 2000) return 2000;
  return timing_ms;
}

bool config_get_ws2812_effect_params(uint8_t effect_id, uint8_t* r, uint8_t* g, uint8_t* b,
                                     uint16_t* timing_ms) {
  if (!r || !g || !b || !timing_ms) return false;
  ws2812_effect_params_nvs_t p = {};
  load_ws2812_effect_params_nvs(&p);
  switch (effect_id) {
    case WS2812_EFFECT_STROBE:
      *r = p.strobe_r;
      *g = p.strobe_g;
      *b = p.strobe_b;
      *timing_ms = p.strobe_ms;
      break;
    case WS2812_EFFECT_CHASE:
      *r = p.chase_r;
      *g = p.chase_g;
      *b = p.chase_b;
      *timing_ms = p.chase_ms;
      break;
    case WS2812_EFFECT_FIND_ME:
      *r = p.find_r;
      *g = p.find_g;
      *b = p.find_b;
      *timing_ms = p.find_ms;
      break;
    default:
      return false;
  }
  return true;
}

bool config_set_ws2812_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,
                                     uint16_t timing_ms) {
  ws2812_effect_params_nvs_t p = {};
  load_ws2812_effect_params_nvs(&p);
  timing_ms = clamp_timing_ms(timing_ms);
  switch (effect_id) {
    case WS2812_EFFECT_STROBE:
      p.strobe_r = r;
      p.strobe_g = g;
      p.strobe_b = b;
      p.strobe_ms = timing_ms;
      break;
    case WS2812_EFFECT_CHASE:
      p.chase_r = r;
      p.chase_g = g;
      p.chase_b = b;
      p.chase_ms = timing_ms;
      break;
    case WS2812_EFFECT_FIND_ME:
      p.find_r = r;
      p.find_g = g;
      p.find_b = b;
      p.find_ms = timing_ms;
      break;
    default:
      return false;
  }
  return save_ws2812_effect_params_nvs(&p);
}
