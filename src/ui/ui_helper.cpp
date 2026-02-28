#include "ui_helper.h"
#include <Arduino.h>
#include "common/input_engine.h"


// ----- private helpers + config -----


// Core “single code” that applies config and sends CAN


// Universal LVGL callback: detects click, double-click, and hold events
static void ui_input_cb(lv_event_t * e) {
    uint8_t input_id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);

    // Handle press/release events for proper timing detection
    if (code == LV_EVENT_PRESSED) {
        input_engine_process_level(input_id, true);
        // Visual feedback on press
        lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_AMBER), 0);
    } else if (code == LV_EVENT_RELEASED) {
        input_engine_process_level(input_id, false);
        // Visual feedback on release (will be updated by value changed if toggle)
        lv_obj_set_style_bg_color(obj, lv_palette_main(LV_PALETTE_GREY), 0);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        // Update visual state for toggle buttons
        bool is_on = lv_obj_has_state(obj, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(obj,
            is_on ? lv_palette_main(LV_PALETTE_AMBER) : lv_palette_main(LV_PALETTE_GREY),
            0
        );
    }
}

// ----- public function implementation -----

void setup_wall_switch_ui() {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0); // Black background

    // Create a container to hold the buttons in a grid
    lv_obj_t * cont = lv_obj_create(scr);
    lv_obj_set_size(cont, 760, 400);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, 0, 0); // Transparent background
    lv_obj_set_style_border_width(cont, 0, 0);

    // Set layout to "Flex" (Grid-style)
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const char * names[] = {"Living Room", "Kitchen", "Bedroom", "Bathroom"};

    for(int i = 0; i < 4; i++) {
        lv_obj_t * btn = lv_btn_create(cont);
        lv_obj_set_size(btn, 350, 170); // Large buttons for easy touching
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE); // Toggle mode
        
        // This line links the index 'i' to the button
        uint8_t input_id = (uint8_t)(i + 1);   // 1..4
        // Register for press, release, and value changed events
        lv_obj_add_event_cb(btn, ui_input_cb, LV_EVENT_PRESSED, (void*)(uintptr_t)input_id);
        lv_obj_add_event_cb(btn, ui_input_cb, LV_EVENT_RELEASED, (void*)(uintptr_t)input_id);
        lv_obj_add_event_cb(btn, ui_input_cb, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)input_id);
        
        // Default style (OFF)
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_radius(btn, 20, 0);

        lv_obj_t * lbl = lv_label_create(btn);
        lv_label_set_text(lbl, names[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0); // Large text
    }
    // --- NEW: Add the CAN Monitoring Label at the bottom ---
    ui_CanStatusLabel = lv_label_create(scr);
    lv_obj_set_width(ui_CanStatusLabel, 700);
    lv_obj_align(ui_CanStatusLabel, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_align(ui_CanStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui_CanStatusLabel, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_text_font(ui_CanStatusLabel, &lv_font_montserrat_28, 0);
    lv_label_set_text(ui_CanStatusLabel, "CAN Bus: Waiting for data...");
}
