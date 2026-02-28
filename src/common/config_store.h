#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "input_engine.h"
#include "can.h"  // NODE_ID_UNCONFIGURED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t node_id;
  uint8_t input_count;
  input_cfg_t inputs[16];   // up to 16 inputs; gang 1..6 for mechanical
} node_config_t;

// Load config from NVS; if not found, provide defaults (node_id = NODE_ID_UNCONFIGURED, etc.)
bool config_load(node_config_t* out_cfg);

// Save config to NVS
bool config_save(const node_config_t* cfg);

// Find-me output index (stored in NVS; which GPIO/LED to drive for CMD_FIND_ME)
bool config_get_find_me_output(uint8_t* out_index);
bool config_set_find_me_output(uint8_t index);

// Debounce/timing (compile-time defaults; hub can override via CMD_SET_TIMING, stored in NVS)
typedef struct {
  uint16_t click_max_ms;
  uint16_t double_click_gap_ms;
  uint16_t hold_min_ms;
  uint16_t long_hold_min_ms;
} input_timing_t;

bool config_get_timing(input_timing_t* out_timing);
bool config_set_timing(const input_timing_t* timing);

#ifdef __cplusplus
}
#endif