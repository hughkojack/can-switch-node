#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "input_engine.h"
#include "can.h"  // NODE_ID_UNCONFIGURED

#ifdef __cplusplus
extern "C" {
#endif

// Match hub: max inputs per node 1..6
#define MAX_INPUTS_PER_NODE 6
#define MAX_INPUT_LABEL_LEN  24

typedef struct {
  uint8_t node_id;
  uint8_t input_count;   // 1..MAX_INPUTS_PER_NODE
  input_cfg_t inputs[MAX_INPUTS_PER_NODE];
  char input_labels[MAX_INPUTS_PER_NODE][MAX_INPUT_LABEL_LEN + 1];
  uint8_t input_gpio[MAX_INPUTS_PER_NODE];  // GPIO pin per input (mechanical node); 0xFF = not assigned
} node_config_t;

// Load config from NVS; if not found, provide defaults (node_id = NODE_ID_UNCONFIGURED, etc.)
bool config_load(node_config_t* out_cfg);

// Save config to NVS
bool config_save(const node_config_t* cfg);

// Find-me: I/O index = ESP32-S3 chip I/O (GPIO number 0-48). Independent of input count (1-6).
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