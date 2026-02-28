#include "input_engine.h"
#include "can.h"
#include "config_store.h"
#include <Arduino.h>

// Compile-time defaults (overridden by config when timing_nvs provided)
#define CLICK_MAX_DURATION_MS     500
#define DOUBLE_CLICK_MAX_GAP_MS   400
#define HOLD_MIN_DURATION_MS      800
#define LONG_HOLD_MIN_DURATION_MS 2000

static uint8_t g_node_id = 0;
static const input_cfg_t* g_cfg = nullptr;
static uint8_t g_cfg_count = 0;

// Runtime timing (from NVS or defaults)
static uint16_t g_click_max_ms = CLICK_MAX_DURATION_MS;
static uint16_t g_double_click_gap_ms = DOUBLE_CLICK_MAX_GAP_MS;
static uint16_t g_hold_min_ms = HOLD_MIN_DURATION_MS;
static uint16_t g_long_hold_min_ms = LONG_HOLD_MIN_DURATION_MS;

// State tracking per input_id
typedef struct {
  bool last_active;
  unsigned long press_time;        // When button was pressed
  unsigned long release_time;      // When button was released
  unsigned long pending_click_time; // Time of pending click (for double-click detection)
  bool hold_sent;                  // Whether hold event was already sent
  bool long_hold_sent;              // Whether long hold event was already sent
  bool click_pending;               // Whether we have a pending click waiting for double-click window
} input_state_t;

static input_state_t g_state[256] = {0};

static const input_cfg_t* find_cfg(uint8_t input_id) {
  for (uint8_t i = 0; i < g_cfg_count; i++) {
    if (g_cfg[i].input_id == input_id) return &g_cfg[i];
  }
  return nullptr;
}

void input_engine_init(uint8_t node_id, const input_cfg_t* cfg, uint8_t cfg_count, const void* timing_nvs) {
  g_node_id = node_id;
  g_cfg = cfg;
  g_cfg_count = cfg_count;

  if (timing_nvs) {
    const input_timing_t* t = (const input_timing_t*)timing_nvs;
    g_click_max_ms = t->click_max_ms ? t->click_max_ms : CLICK_MAX_DURATION_MS;
    g_double_click_gap_ms = t->double_click_gap_ms ? t->double_click_gap_ms : DOUBLE_CLICK_MAX_GAP_MS;
    g_hold_min_ms = t->hold_min_ms ? t->hold_min_ms : HOLD_MIN_DURATION_MS;
    g_long_hold_min_ms = t->long_hold_min_ms ? t->long_hold_min_ms : LONG_HOLD_MIN_DURATION_MS;
  } else {
    g_click_max_ms = CLICK_MAX_DURATION_MS;
    g_double_click_gap_ms = DOUBLE_CLICK_MAX_GAP_MS;
    g_hold_min_ms = HOLD_MIN_DURATION_MS;
    g_long_hold_min_ms = LONG_HOLD_MIN_DURATION_MS;
  }

  for (int i = 0; i < 256; i++) {
    g_state[i].last_active = false;
    g_state[i].press_time = 0;
    g_state[i].release_time = 0;
    g_state[i].pending_click_time = 0;
    g_state[i].hold_sent = false;
    g_state[i].long_hold_sent = false;
    g_state[i].click_pending = false;
  }
}

void input_engine_process_level(uint8_t input_id, bool active_now) {
  
  const input_cfg_t* c = find_cfg(input_id);
  if (!c) {
    Serial.println("ENGINE: no cfg match -> NOT SENDING");
    return;
  }

  input_state_t* state = &g_state[input_id];
  unsigned long now = millis();

  // Handle state changes
  if (state->last_active == active_now) {
    // State hasn't changed, but check for hold events while pressed
    if (active_now && !state->hold_sent) {
      unsigned long press_duration = now - state->press_time;
      if (press_duration >= (unsigned long)g_long_hold_min_ms && !state->long_hold_sent) {
        can_send_long_hold(g_node_id, input_id);
        state->long_hold_sent = true;
        Serial.printf("ENGINE: input_id=%u LONG_HOLD\n", input_id);
        // Cancel any pending click
        state->click_pending = false;
      } else if (press_duration >= (unsigned long)g_hold_min_ms) {
        can_send_hold(g_node_id, input_id);
        state->hold_sent = true;
        Serial.printf("ENGINE: input_id=%u HOLD\n", input_id);
        // Cancel any pending click
        state->click_pending = false;
      }
    }
    return;
  }

  // State changed
  if (active_now) {
    // Button pressed
    // Check if this is a second press within double-click window
    if (state->click_pending && (now - state->pending_click_time) <= (unsigned long)g_double_click_gap_ms) {
      // Double click detected!
      can_send_double_click(g_node_id, input_id);
      Serial.printf("ENGINE: input_id=%u DOUBLE_CLICK\n", input_id);
      state->click_pending = false;
      state->pending_click_time = 0;
    }
    
    state->press_time = now;
    state->hold_sent = false;
    state->long_hold_sent = false;
    state->last_active = true;
  } else {
    // Button released
    state->release_time = now;
    unsigned long press_duration = state->release_time - state->press_time;
    state->last_active = false;

    // Check if this was a hold (too long for a click)
    if (press_duration >= (unsigned long)g_hold_min_ms) {
      // Hold was already sent during press, or send it now if missed
      if (!state->hold_sent) {
        can_send_hold(g_node_id, input_id);
        Serial.printf("ENGINE: input_id=%u HOLD (on release)\n", input_id);
      }
      // Reset click tracking
      state->click_pending = false;
      state->pending_click_time = 0;
    } else {
      // This was a short press - mark as pending click
      state->click_pending = true;
      state->pending_click_time = now;
      // Will be sent as CLICK if no second press within DOUBLE_CLICK_MAX_GAP_MS
    }
  }
}

void input_engine_update(void) {
  unsigned long now = millis();
  
  // Check all inputs for pending click timeouts
  for (uint8_t i = 0; i < 256; i++) {
    input_state_t* state = &g_state[i];
    if (state->click_pending && (now - state->pending_click_time) > (unsigned long)g_double_click_gap_ms) {
      // Find config to verify this input is valid
      const input_cfg_t* c = find_cfg(i);
      if (c) {
        // Timeout - send the single click
        can_send_click(g_node_id, i);
        Serial.printf("ENGINE: input_id=%u CLICK (timeout)\n", i);
        state->click_pending = false;
        state->pending_click_time = 0;
      }
    }
  }
}