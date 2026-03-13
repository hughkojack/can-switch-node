#include "ui_helper.h"
#include "common/can.h"
#include "common/config_store.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// ----- Fixed 4-card 2x2 main screen + status line at bottom -----

static node_config_t s_ui_cfg;
#define MAIN_SCREEN_BUTTONS_MAX 4
static uint8_t s_feedback_brightness[MAIN_SCREEN_BUTTONS_MAX] = { 255, 255, 255, 255 };  // 255 = no binding

static lv_obj_t* s_btn_labels[MAIN_SCREEN_BUTTONS_MAX] = { NULL };
static lv_obj_t* s_cards[MAIN_SCREEN_BUTTONS_MAX] = { NULL };
static lv_obj_t* s_card_sliders[MAIN_SCREEN_BUTTONS_MAX] = { NULL };
static lv_obj_t* s_power_icons[MAIN_SCREEN_BUTTONS_MAX] = { NULL };
static lv_obj_t* s_pct_labels[MAIN_SCREEN_BUTTONS_MAX] = { NULL };
static int s_selected_btn = 0;
static bool s_slider_dragging[MAIN_SCREEN_BUTTONS_MAX] = { false };
// After sending dim 0, ignore non-zero feedback briefly so node stays off until hub catches up
static unsigned long s_sent_off_at_ms[MAIN_SCREEN_BUTTONS_MAX] = { 0 };
static const unsigned long SENT_OFF_GRACE_MS = 500;
// Last value we actually drew (0-100); -1 = not yet set (force first update)
static int s_last_displayed_pct[MAIN_SCREEN_BUTTONS_MAX] = { -1, -1, -1, -1 };

// Status bar time label (right-aligned); updated by LVGL timer to avoid cross-task flicker
static lv_obj_t* s_time_label = NULL;
static lv_timer_t* s_status_time_timer = NULL;
// CAN indicator dot: green = connected, red = disconnected (flashing when disconnected)
static lv_obj_t* s_can_indicator_dot = NULL;
static lv_timer_t* s_can_flash_timer = NULL;
static volatile bool s_can_connected = false;
static bool s_can_flash_on = false;

// No longer used (replaced by dot); kept for compatibility
lv_obj_t* ui_CanStatusLabel = NULL;

static void update_selection_highlight(void) {
    (void)s_selected_btn;  // selection no longer changes border; all cards have same white matte border
    for (int i = 0; i < MAIN_SCREEN_BUTTONS_MAX; i++) {
        if (s_cards[i]) {
            lv_obj_set_style_border_width(s_cards[i], 2, 0);
            lv_obj_set_style_border_color(s_cards[i], lv_color_hex(0xEEEEEE), 0);
        }
    }
}

// Update power icon opacity and brightness % digit for one card (only when value changed to avoid flicker)
static void update_card_brightness_ui(int btn_index, int value) {
    if (btn_index < 0 || btn_index >= MAIN_SCREEN_BUTTONS_MAX) return;
    if (s_last_displayed_pct[btn_index] == value) return;
    s_last_displayed_pct[btn_index] = value;

    if (s_pct_labels[btn_index]) {
        lv_label_set_text_fmt(s_pct_labels[btn_index], "%d%%", value);
    }
    if (s_power_icons[btn_index]) {
        lv_obj_t* icon = s_power_icons[btn_index];
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
        if (value == 0) {
            lv_obj_set_style_bg_color(icon, lv_color_hex(0x606060), 0);
            lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(icon, lv_color_hex(0xC00000), 0);
            lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
        }
    }
}

// Card tap: send CLICK, set as selected, update highlight
static void button_click_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    uint8_t btn_index = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (btn_index >= s_ui_cfg.input_count) return;
    uint8_t input_id = (uint8_t)(btn_index + 1);
    can_send_click(s_ui_cfg.node_id, input_id);
    s_selected_btn = (int)btn_index;
    update_selection_highlight();
}

// Power icon click: toggle between off (0) and on at 50%
static void power_icon_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_event_stop_bubbling(e);
    uint8_t btn_index = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (btn_index >= s_ui_cfg.input_count) return;
    uint8_t input_id = (uint8_t)(btn_index + 1);
    int cur = (s_feedback_brightness[btn_index] <= 100) ? (int)s_feedback_brightness[btn_index] : 0;
    int next = (cur > 0) ? 0 : 50;
    s_feedback_brightness[btn_index] = (uint8_t)next;
    if (next == 0)
        s_sent_off_at_ms[btn_index] = millis();
    can_send_dim(s_ui_cfg.node_id, input_id, (uint8_t)next);
    if (s_card_sliders[btn_index])
        lv_slider_set_value(s_card_sliders[btn_index], next, LV_ANIM_ON);
    update_card_brightness_ui((int)btn_index, next);
}

// Per-card horizontal slider: change brightness for this card only
static void card_slider_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t btn_index = (uint8_t)(intptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_PRESSED) {
        if (btn_index < MAIN_SCREEN_BUTTONS_MAX)
            s_slider_dragging[btn_index] = true;
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        if (btn_index < MAIN_SCREEN_BUTTONS_MAX)
            s_slider_dragging[btn_index] = false;
    }

    if (code != LV_EVENT_RELEASED && code != LV_EVENT_VALUE_CHANGED) return;
    if (btn_index >= s_ui_cfg.input_count) return;
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    s_feedback_brightness[btn_index] = (uint8_t)v;
    if (v == 0)
        s_sent_off_at_ms[btn_index] = millis();
    can_send_dim(s_ui_cfg.node_id, (uint8_t)(btn_index + 1), (uint8_t)v);
    update_card_brightness_ui((int)btn_index, (int)v);
}

static const char* fallback_label(int idx) {
    static const char* defaults[] = { "Input 1", "Input 2", "Input 3", "Input 4" };
    if (idx >= 0 && idx < MAIN_SCREEN_BUTTONS_MAX) return defaults[idx];
    return "Input";
}

static void update_button_label(lv_obj_t* label_obj, int idx) {
    if (!label_obj || idx < 0 || idx >= MAIN_SCREEN_BUTTONS_MAX) return;
    const char* name = (s_ui_cfg.input_labels[idx][0] != '\0')
        ? s_ui_cfg.input_labels[idx]
        : fallback_label(idx);
    lv_label_set_text(label_obj, name);
}

// Create one card: label (left), power icon + brightness % digit (right), horizontal slider (left to right, round knob)
static void create_card(lv_obj_t* parent, int i, lv_coord_t card_w, lv_coord_t card_h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x383838), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xEEEEEE), 0);  // white matte border for all cards
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_top(card, 2, 0);       // minimum above power icon to leave room for slider
    lv_obj_set_style_pad_bottom(card, 44, 0);  // room for slider track (36) + knob overflow (48/2 - 36/2) so knob isn't clipped
    lv_obj_set_style_pad_row(card, 2, 0);      // minimal gap between top row and spacer
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_cards[i] = card;

    // Top row: label (left) | power icon + brightness % digit (right)
    lv_obj_t* top_row = lv_obj_create(card);
    lv_obj_set_width(top_row, LV_PCT(100));
    lv_obj_set_height(top_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(top_row, 0, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_style_pad_column(top_row, 4, 0);

    lv_obj_t* lbl = lv_label_create(top_row);
    lv_label_set_text(lbl, "");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xEEEEEE), 0);  // white matte
    update_button_label(lbl, i);
    s_btn_labels[i] = lbl;

    lv_obj_t* right_block = lv_obj_create(top_row);
    lv_obj_set_size(right_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(right_block, 0, 0);
    lv_obj_set_style_border_width(right_block, 0, 0);

    lv_obj_t* power_lbl = lv_label_create(right_block);
    lv_label_set_text(power_lbl, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(power_lbl, &lv_font_montserrat_48, 0);
    lv_obj_set_style_pad_all(power_lbl, 2, 0);
    lv_obj_set_style_radius(power_lbl, 4, 0);
    lv_obj_add_flag(power_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(power_lbl, power_icon_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    s_power_icons[i] = power_lbl;

    lv_obj_t* pct_lbl = lv_label_create(right_block);
    int v = (s_feedback_brightness[i] <= 100) ? (int)s_feedback_brightness[i] : 0;
    lv_label_set_text_fmt(pct_lbl, "%d%%", v);
    lv_obj_set_style_text_font(pct_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pct_lbl, lv_color_hex(0xEEEEEE), 0);  // white matte
    s_pct_labels[i] = pct_lbl;

    update_card_brightness_ui(i, v);

    // Spacer: takes remaining height so slider sits at bottom of card
    lv_obj_t* spacer = lv_obj_create(card);
    lv_obj_set_size(spacer, LV_PCT(100), 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, 0, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    // Horizontal slider: left to right, stops before power/digit area; large round knob
    lv_obj_t* slider = lv_slider_create(card);
    lv_obj_set_width(slider, LV_PCT(78));  // leave right space for power icon + brightness %
    lv_obj_set_height(slider, 14);         // thin track
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, v, LV_ANIM_OFF);
    lv_obj_set_style_radius(slider, 6, 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x7e7e7e), 0);  // fixed color right of knob
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4ade80), LV_PART_INDICATOR);  // light green fill left of knob
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_width(slider, 48, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 48, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 24, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x909090), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, card_slider_cb, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(slider, card_slider_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(slider, card_slider_cb, LV_EVENT_PRESSED, (void*)(intptr_t)i);
    s_card_sliders[i] = slider;

    // Card tap for CLICK (use card as clickable)
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, button_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    // Cards with index >= input_count are guarded in callbacks (no CAN send)
}

// Called every 1s from LVGL task; only updates label when string changes (stable, no flicker)
// Shows date/time from hub when set (CMD_SET_DATETIME), otherwise uptime.
static void on_status_time_update(lv_timer_t* timer) {
    (void)timer;
    if (s_time_label == NULL) return;
    char buf[24];
    time_t now = time(nullptr);
    // Use wall clock if hub has set it (reasonable Unix time, e.g. after 2020)
    if (now > 1600000000) {
        struct tm* t = localtime(&now);
        if (t)
            strftime(buf, sizeof(buf), "%a %H:%M", t);
        else
            snprintf(buf, sizeof(buf), "--:--");
    } else {
        unsigned long sec = millis() / 1000;
        unsigned long h = sec / 3600;
        unsigned long m = (sec % 3600) / 60;
        unsigned long s = sec % 60;
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", h, m, s);
    }
    static char last[24] = { '\0' };
    if (strcmp(buf, last) != 0) {
        lv_label_set_text(s_time_label, buf);
        strncpy(last, buf, sizeof(last) - 1);
        last[sizeof(last) - 1] = '\0';
    }
}

// Called every 500ms from LVGL task; flashes red dot when CAN disconnected
static void on_can_flash_timer(lv_timer_t* timer) {
    (void)timer;
    if (s_can_indicator_dot == NULL) return;
    if (s_can_connected) {
        lv_obj_set_style_bg_color(s_can_indicator_dot, lv_color_hex(0x00C000), 0);
        lv_obj_set_style_bg_opa(s_can_indicator_dot, LV_OPA_COVER, 0);
        return;
    }
    s_can_flash_on = !s_can_flash_on;
    lv_obj_set_style_bg_color(s_can_indicator_dot, lv_color_hex(0xC00000), 0);
    lv_obj_set_style_bg_opa(s_can_indicator_dot, s_can_flash_on ? LV_OPA_COVER : LV_OPA_30, 0);
}

void ui_set_can_connected(bool connected) {
    s_can_connected = connected;
    // Dot is updated by on_can_flash_timer (LVGL task) so we don't touch LVGL from CAN task
}

void setup_wall_switch_ui(void) {
    config_load(&s_ui_cfg);
    int N = (int)s_ui_cfg.input_count;
    if (N < 1) N = 1;
    if (N > MAIN_SCREEN_BUTTONS_MAX) N = MAIN_SCREEN_BUTTONS_MAX;
    s_ui_cfg.input_count = (uint8_t)N;
    for (int i = 0; i < MAIN_SCREEN_BUTTONS_MAX; i++)
        s_feedback_brightness[i] = 255;

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    lv_coord_t lcd_w = lv_disp_get_hor_res(NULL);
    lv_coord_t lcd_h = lv_disp_get_ver_res(NULL);
    const int status_bar_h = 56;  // reserve enough so cards 2 & 3 (bottom row) aren't clipped
    const int pad = 12;
    const int row_gap = 4;  // gap between 1st and 2nd row

    lv_obj_t* cont = lv_obj_create(scr);
    lv_obj_set_size(cont, lcd_w, lcd_h - status_bar_h);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_top(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, row_gap, 0);
    lv_obj_set_style_pad_column(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Fixed 4 cards in 2x2 (leave margin so bottom row isn't clipped by status bar)
    const int grid_h = lcd_h - status_bar_h;
    const int bottom_margin = 4;
    const int row_h = (grid_h - row_gap - bottom_margin) / 2;
    const int card_w = (lcd_w - 3 * pad) / 2;
    const int card_h = row_h - 8;

    // Row 1: cards 0, 1
    lv_obj_t* row1 = lv_obj_create(cont);
    lv_obj_set_size(row1, lcd_w, row_h);
    lv_obj_set_style_bg_opa(row1, 0, 0);
    lv_obj_set_style_border_width(row1, 0, 0);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, pad, 0);
    lv_obj_set_style_pad_row(row1, 0, 0);
    lv_obj_set_scrollbar_mode(row1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    create_card(row1, 0, card_w, card_h);
    create_card(row1, 1, card_w, card_h);

    // Row 2: cards 2, 3
    lv_obj_t* row2 = lv_obj_create(cont);
    lv_obj_set_size(row2, lcd_w, row_h);
    lv_obj_set_style_bg_opa(row2, 0, 0);
    lv_obj_set_style_border_width(row2, 0, 0);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, pad, 0);
    lv_obj_set_style_pad_row(row2, 0, 0);
    lv_obj_set_scrollbar_mode(row2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    create_card(row2, 2, card_w, card_h);
    create_card(row2, 3, card_w, card_h);

    // Status line at bottom: CAN indicator dot (green = connected, red flashing = disconnected) + time on right
    s_can_indicator_dot = lv_obj_create(scr);
    lv_obj_set_size(s_can_indicator_dot, 14, 14);
    lv_obj_align(s_can_indicator_dot, LV_ALIGN_BOTTOM_LEFT, 10, -9);
    lv_obj_set_style_radius(s_can_indicator_dot, 7, 0);
    lv_obj_set_style_border_width(s_can_indicator_dot, 0, 0);
    lv_obj_set_style_bg_color(s_can_indicator_dot, lv_color_hex(0xC00000), 0);
    lv_obj_set_style_bg_opa(s_can_indicator_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_can_indicator_dot, LV_OBJ_FLAG_CLICKABLE);
    s_can_flash_timer = lv_timer_create(on_can_flash_timer, 500, NULL);

    // Time label (right-aligned); updated by LVGL timer at 1s so no cross-task flicker
    s_time_label = lv_label_create(scr);
    lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_time_label, lv_palette_main(LV_PALETTE_AMBER), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_time_label, "0:00:00");
    s_status_time_timer = lv_timer_create(on_status_time_update, 1000, NULL);

    s_selected_btn = 0;
    update_selection_highlight();
}

void ui_refresh_labels(void) {
    config_load(&s_ui_cfg);
    for (int i = 0; i < MAIN_SCREEN_BUTTONS_MAX && s_btn_labels[i]; i++)
        update_button_label(s_btn_labels[i], i);
}

void ui_set_feedback_brightness(uint8_t btn_index, uint8_t brightness_0_100_or_255) {
    if (btn_index >= MAIN_SCREEN_BUTTONS_MAX) return;
    // Don't overwrite slider while user is dragging it (stops feedback from snapping 1 and 4 back to zero)
    if (s_slider_dragging[btn_index]) return;
    // After we sent off (dim 0), ignore non-zero feedback for a short time so node stays off until hub catches up
    if (brightness_0_100_or_255 > 0 && (unsigned long)(millis() - s_sent_off_at_ms[btn_index]) < SENT_OFF_GRACE_MS)
        return;
    s_feedback_brightness[btn_index] = brightness_0_100_or_255;
    int v = (brightness_0_100_or_255 <= 100) ? (int)brightness_0_100_or_255 : 0;
    if (s_last_displayed_pct[btn_index] == v) return;  // no change, skip LVGL updates to avoid flicker
    if (s_card_sliders[btn_index]) {
        lv_slider_set_value(s_card_sliders[btn_index], v, LV_ANIM_ON);
    }
    update_card_brightness_ui((int)btn_index, v);
}
