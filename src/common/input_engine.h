#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  INPUT_MODE_MOMENTARY = 0,
  INPUT_MODE_TOGGLE    = 1
} input_mode_t;

typedef struct {
  uint8_t input_id;        // 1..255
  input_mode_t mode;       // momentary or toggle
} input_cfg_t;

// Initialize engine with node_id and a config table.
// timing_nvs: optional pointer to input_timing_t (from config_get_timing); if NULL use compile-time defaults.
void input_engine_init(uint8_t node_id, const input_cfg_t* cfg, uint8_t cfg_count, const void* timing_nvs);

// Feed normalized "active" level into engine (true=active, false=inactive)
// Engine will emit CAN messages according to mode.
void input_engine_process_level(uint8_t input_id, bool active_now);

// Periodic update - call this regularly to check for pending click timeouts
void input_engine_update(void);

#ifdef __cplusplus
}
#endif