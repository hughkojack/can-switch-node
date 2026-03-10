#include "../ui_audio_mixer.h"
#include "../ui_audio_mixer_helpers.h"
#include "ui_Audio_Player.h"

lv_obj_t* ui_Audio_Player = NULL;
void (*ui_audio_mixer_go_back)(void) = NULL;
static lv_obj_t* ui_Background = NULL;
static lv_obj_t* ui_Content_group = NULL;
static lv_obj_t* ui_Right_group = NULL;
static lv_obj_t* ui_Slider_group_right = NULL;
static lv_obj_t* ui_Max1 = NULL;
static lv_obj_t* ui_Min2 = NULL;
static lv_obj_t* ui_Slider3 = NULL;
static lv_obj_t* ui_Center_group = NULL;
static lv_obj_t* ui_Indicator_group = NULL;
static lv_obj_t* ui_Indicator_Right = NULL;
static lv_obj_t* ui_BTN_Cut_Left1 = NULL;
static lv_obj_t* ui_Label_Cut_3 = NULL;

void ui_event_Label_Cut_3(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_audio_mixer_go_back) ui_audio_mixer_go_back();
}

void ui_Audio_Player_screen_init(void)
{
    ui_Audio_Player = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Audio_Player, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Audio_Player, lv_color_hex(0x2D2F36), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Audio_Player, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Background = lv_obj_create(ui_Audio_Player);
    lv_obj_set_width(ui_Background, LV_PCT(100));
    lv_obj_set_height(ui_Background, LV_PCT(100));
    lv_obj_set_align(ui_Background, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Background, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Background, lv_color_hex(0x4C4E5B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Background, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_Background, lv_color_hex(0x393B46), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Background, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Background, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui_Background, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Background, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_Background, lv_color_hex(0x191B20), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Background, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Background, 140, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Background, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_x(ui_Background, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_ofs_y(ui_Background, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Content_group = lv_obj_create(ui_Background);
    lv_obj_set_width(ui_Content_group, LV_PCT(100));
    lv_obj_set_height(ui_Content_group, LV_PCT(95));
    lv_obj_set_align(ui_Content_group, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_Content_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Content_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Content_group, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Content_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Right_group = lv_obj_create(ui_Content_group);
    lv_obj_set_width(ui_Right_group, LV_PCT(28));
    lv_obj_set_height(ui_Right_group, LV_PCT(108));
    lv_obj_set_x(ui_Right_group, -6);
    lv_obj_set_y(ui_Right_group, 20);
    lv_obj_set_align(ui_Right_group, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_clear_flag(ui_Right_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Right_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Right_group, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Right_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Slider_group_right = lv_obj_create(ui_Right_group);
    lv_obj_set_width(ui_Slider_group_right, LV_PCT(100));
    lv_obj_set_height(ui_Slider_group_right, LV_PCT(101));
    lv_obj_set_x(ui_Slider_group_right, 6);
    lv_obj_set_y(ui_Slider_group_right, 9);
    lv_obj_set_align(ui_Slider_group_right, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_Slider_group_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Slider_group_right, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider_group_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Slider_group_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Slider_group_right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Slider_group_right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Max1 = lv_label_create(ui_Slider_group_right);
    lv_obj_set_width(ui_Max1, 38);
    lv_obj_set_height(ui_Max1, 19);
    lv_obj_set_x(ui_Max1, 0);
    lv_obj_set_y(ui_Max1, -20);
    lv_obj_set_align(ui_Max1, LV_ALIGN_TOP_MID);
    lv_label_set_text(ui_Max1, "MAX");
    lv_obj_set_style_text_color(ui_Max1, lv_color_hex(0x9395A1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Max1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Max1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Max1, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Min2 = lv_label_create(ui_Slider_group_right);
    lv_obj_set_width(ui_Min2, 35);
    lv_obj_set_height(ui_Min2, 19);
    lv_obj_set_x(ui_Min2, 4);
    lv_obj_set_y(ui_Min2, -42);
    lv_obj_set_align(ui_Min2, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_Min2, "MIN");
    lv_obj_set_style_text_color(ui_Min2, lv_color_hex(0x9395A1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Min2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Min2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Min2, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Slider3 = lv_slider_create(ui_Slider_group_right);
    lv_slider_set_value(ui_Slider3, 80, LV_ANIM_OFF);
    if (lv_slider_get_mode(ui_Slider3) == LV_SLIDER_MODE_RANGE) lv_slider_set_left_value(ui_Slider3, 0, LV_ANIM_OFF);
    lv_obj_set_width(ui_Slider3, 65);
    lv_obj_set_height(ui_Slider3, 349);
    lv_obj_set_x(ui_Slider3, 12);
    lv_obj_set_y(ui_Slider3, -32);
    lv_obj_set_align(ui_Slider3, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_Slider3, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Slider3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Slider3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_Slider3, &ui_img_pot_ver_line_png, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Slider3, 28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Slider3, 28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Slider3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Slider3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Slider3, lv_color_hex(0x50FF7D), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider3, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Slider3, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Slider3, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui_Slider3, lv_color_hex(0x50FF7D), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(ui_Slider3, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Slider3, 50, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Slider3, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_Slider3, 12, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Slider3, lv_color_hex(0x50FF7D), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Slider3, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Slider3, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Slider3, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_Slider3, &ui_img_pot_ver_knob_png, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Slider3, 3, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Slider3, 2, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Slider3, 6, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Slider3, 6, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_Slider3, 12, LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ui_Slider3, lv_color_hex(0x50FF7D), LV_PART_KNOB | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ui_Slider3, 255, LV_PART_KNOB | LV_STATE_PRESSED);

    ui_Center_group = lv_obj_create(ui_Content_group);
    lv_obj_set_width(ui_Center_group, LV_PCT(20));
    lv_obj_set_height(ui_Center_group, LV_PCT(101));
    lv_obj_set_x(ui_Center_group, -148);
    lv_obj_set_y(ui_Center_group, -2);
    lv_obj_set_align(ui_Center_group, LV_ALIGN_BOTTOM_MID);
    lv_obj_clear_flag(ui_Center_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Center_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Center_group, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Center_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Indicator_group = lv_obj_create(ui_Center_group);
    lv_obj_set_height(ui_Indicator_group, 369);
    lv_obj_set_width(ui_Indicator_group, LV_PCT(100));
    lv_obj_set_x(ui_Indicator_group, -18);
    lv_obj_set_y(ui_Indicator_group, 34);
    lv_obj_set_align(ui_Indicator_group, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_Indicator_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_Indicator_group, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Indicator_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Indicator_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Indicator_group, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Indicator_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Indicator_group, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Indicator_group, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Indicator_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Indicator_group, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_Indicator_Right = lv_slider_create(ui_Indicator_group);
    lv_slider_set_range(ui_Indicator_Right, 0, 29);
    lv_slider_set_value(ui_Indicator_Right, 25, LV_ANIM_OFF);
    if (lv_slider_get_mode(ui_Indicator_Right) == LV_SLIDER_MODE_RANGE)
        lv_slider_set_left_value(ui_Indicator_Right, 0, LV_ANIM_OFF);
    lv_obj_set_width(ui_Indicator_Right, 33);
    lv_obj_set_height(ui_Indicator_Right, 289);
    lv_obj_set_x(ui_Indicator_Right, -32);
    lv_obj_set_y(ui_Indicator_Right, 1);
    lv_obj_set_align(ui_Indicator_Right, LV_ALIGN_RIGHT_MID);
    lv_obj_set_style_radius(ui_Indicator_Right, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Indicator_Right, lv_color_hex(0x272A33), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Indicator_Right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Indicator_Right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Indicator_Right, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Indicator_Right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui_Indicator_Right, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui_Indicator_Right, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui_Indicator_Right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui_Indicator_Right, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_Indicator_Right, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Indicator_Right, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Indicator_Right, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui_Indicator_Right, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui_Indicator_Right, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(ui_Indicator_Right, &ui_img_indicator_ver_png, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Indicator_Right, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Indicator_Right, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    ui_BTN_Cut_Left1 = lv_imgbtn_create(ui_Background);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_RELEASED, NULL, &ui_img_btn_1_inact_png, NULL);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_PRESSED, NULL, &ui_img_btn_1_act_png, NULL);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_DISABLED, NULL, &ui_img_btn_1_inact_png, NULL);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_CHECKED_PRESSED, NULL, &ui_img_btn_1_act_png, NULL);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_CHECKED_RELEASED, NULL, &ui_img_btn_1_act_png, NULL);
    lv_imgbtn_set_src(ui_BTN_Cut_Left1, LV_IMGBTN_STATE_CHECKED_DISABLED, NULL, &ui_img_btn_1_inact_png, NULL);
    lv_obj_set_width(ui_BTN_Cut_Left1, 127);
    lv_obj_set_height(ui_BTN_Cut_Left1, 99);
    lv_obj_set_x(ui_BTN_Cut_Left1, 33);
    lv_obj_set_y(ui_BTN_Cut_Left1, -295);
    lv_obj_set_align(ui_BTN_Cut_Left1, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(ui_BTN_Cut_Left1, LV_OBJ_FLAG_CHECKABLE);

    ui_Label_Cut_3 = lv_label_create(ui_BTN_Cut_Left1);
    lv_obj_set_width(ui_Label_Cut_3, 35);
    lv_obj_set_height(ui_Label_Cut_3, 19);
    lv_obj_set_align(ui_Label_Cut_3, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label_Cut_3, "CUT");
    lv_obj_set_style_text_color(ui_Label_Cut_3, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label_Cut_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_Label_Cut_3, LV_TEXT_ALIGN_AUTO, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label_Cut_3, &ui_font_mos_semibold_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui_Label_Cut_3, ui_event_Label_Cut_3, LV_EVENT_ALL, NULL);
}

void ui_Audio_Player_screen_destroy(void)
{
    if (ui_Audio_Player) lv_obj_del(ui_Audio_Player);
    ui_Audio_Player = NULL;
    ui_Background = NULL;
    ui_Content_group = NULL;
    ui_Right_group = NULL;
    ui_Slider_group_right = NULL;
    ui_Max1 = NULL;
    ui_Min2 = NULL;
    ui_Slider3 = NULL;
    ui_Center_group = NULL;
    ui_Indicator_group = NULL;
    ui_Indicator_Right = NULL;
    ui_BTN_Cut_Left1 = NULL;
    ui_Label_Cut_3 = NULL;
}

void ui_audio_mixer_screen_init(void) { ui_Audio_Player_screen_init(); }
void ui_audio_mixer_screen_destroy(void) { ui_Audio_Player_screen_destroy(); }
