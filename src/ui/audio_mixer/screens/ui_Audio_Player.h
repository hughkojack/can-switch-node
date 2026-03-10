#ifndef UI_AUDIO_PLAYER_H
#define UI_AUDIO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

extern void ui_Audio_Player_screen_init(void);
extern void ui_Audio_Player_screen_destroy(void);
extern lv_obj_t* ui_Audio_Player;
extern void ui_event_Label_Cut_3(lv_event_t* e);

#ifdef __cplusplus
}
#endif

#endif
