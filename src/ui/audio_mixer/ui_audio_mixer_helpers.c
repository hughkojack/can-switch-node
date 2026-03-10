#include "ui_audio_mixer_helpers.h"

void _ui_flag_modify(lv_obj_t* target, int32_t flag, int value)
{
    if (value == _UI_MODIFY_FLAG_TOGGLE) {
        if (lv_obj_has_flag(target, flag))
            lv_obj_clear_flag(target, flag);
        else
            lv_obj_add_flag(target, flag);
    } else if (value == _UI_MODIFY_FLAG_ADD) {
        lv_obj_add_flag(target, flag);
    } else {
        lv_obj_clear_flag(target, flag);
    }
}
