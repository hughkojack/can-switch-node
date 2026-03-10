#ifndef UI_AUDIO_MIXER_HELPERS_H
#define UI_AUDIO_MIXER_HELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

#define _UI_MODIFY_FLAG_ADD 0
#define _UI_MODIFY_FLAG_REMOVE 1
#define _UI_MODIFY_FLAG_TOGGLE 2
void _ui_flag_modify(lv_obj_t* target, int32_t flag, int value);

#ifdef __cplusplus
}
#endif

#endif
