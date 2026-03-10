#ifndef UI_AUDIO_MIXER_H
#define UI_AUDIO_MIXER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

LV_IMG_DECLARE(ui_img_pot_ver_line_png);
LV_IMG_DECLARE(ui_img_pot_ver_knob_png);
LV_IMG_DECLARE(ui_img_indicator_ver_png);
LV_IMG_DECLARE(ui_img_btn_1_inact_png);
LV_IMG_DECLARE(ui_img_btn_1_act_png);
LV_FONT_DECLARE(ui_font_mos_semibold_16);

void ui_audio_mixer_screen_init(void);
void ui_audio_mixer_screen_destroy(void);
extern lv_obj_t* ui_Audio_Player;
extern void (*ui_audio_mixer_go_back)(void);

#ifdef __cplusplus
}
#endif

#endif
