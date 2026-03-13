#ifndef UI_HELPER_H
#define UI_HELPER_H

#include <lvgl.h>
#include <stdint.h>


extern lv_obj_t * ui_CanStatusLabel;

// Set CAN indicator dot: green when connected, red flashing when disconnected. Call from CAN task with LVGL lock if needed.
void ui_set_can_connected(bool connected);

// Public API
void setup_wall_switch_ui(void);
// Call when node config labels have been updated (e.g. after CMD_SET_INPUT_LABEL) so the UI can refresh button labels.
void ui_refresh_labels(void);
// Set per-button brightness (0-100, or 255 = no binding). Call from CAN NODE_STATE_FEEDBACK handler. btn_index 0..3.
void ui_set_feedback_brightness(uint8_t btn_index, uint8_t brightness_0_100_or_255);

#endif
