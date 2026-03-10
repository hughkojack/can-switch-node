#include "config_store.h"
#include <Preferences.h>
#include <Arduino.h>

#define NVS_NAMESPACE "node_cfg"
#define NVS_KEY_CFG "cfg"
#define NVS_KEY_FIND_ME "find_me"
#define NVS_KEY_TIMING "timing"

// Default timing (match input_engine compile-time defaults)
static const uint16_t DEFAULT_CLICK_MAX_MS = 500;
static const uint16_t DEFAULT_DOUBLE_CLICK_GAP_MS = 400;
static const uint16_t DEFAULT_HOLD_MIN_MS = 800;
static const uint16_t DEFAULT_LONG_HOLD_MIN_MS = 2000;

static void apply_defaults(node_config_t* out_cfg) {
  out_cfg->node_id = NODE_ID_UNCONFIGURED;
  memset(out_cfg->input_labels, 0, sizeof(out_cfg->input_labels));
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

bool config_load(node_config_t* out_cfg) {
  if (!out_cfg) return false;

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {  // read-only
    apply_defaults(out_cfg);
    return true;
  }

  size_t len = prefs.getBytes(NVS_KEY_CFG, out_cfg, sizeof(node_config_t));
  prefs.end();

  if (len == OLD_NODE_CONFIG_SIZE) {
    // Old NVS layout: zero labels and use the rest
    memset(out_cfg->input_labels, 0, sizeof(out_cfg->input_labels));
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

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false))
    return false;

  size_t written = prefs.putBytes(NVS_KEY_CFG, cfg, sizeof(node_config_t));
  prefs.end();
  return (written == sizeof(node_config_t));
}

bool config_get_find_me_output(uint8_t* out_index) {
  if (!out_index) return false;
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    *out_index = 0;
    return true;
  }
  *out_index = prefs.getUChar(NVS_KEY_FIND_ME, 0);
  prefs.end();
  return true;
}

bool config_set_find_me_output(uint8_t index) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false))
    return false;
  prefs.putUChar(NVS_KEY_FIND_ME, index);
  prefs.end();
  return true;
}

bool config_get_timing(input_timing_t* out_timing) {
  if (!out_timing) return false;
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    out_timing->click_max_ms = DEFAULT_CLICK_MAX_MS;
    out_timing->double_click_gap_ms = DEFAULT_DOUBLE_CLICK_GAP_MS;
    out_timing->hold_min_ms = DEFAULT_HOLD_MIN_MS;
    out_timing->long_hold_min_ms = DEFAULT_LONG_HOLD_MIN_MS;
    return true;
  }
  size_t len = prefs.getBytes(NVS_KEY_TIMING, out_timing, sizeof(input_timing_t));
  prefs.end();
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
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false))
    return false;
  size_t written = prefs.putBytes(NVS_KEY_TIMING, timing, sizeof(input_timing_t));
  prefs.end();
  return (written == sizeof(input_timing_t));
}
