#pragma once

#include <stdint.h>
#include <stdbool.h>

#if defined(WS2812_ENABLE) && WS2812_ENABLE

void ws2812_task_start(void);

void ws2812_task_post_set_night_light(bool on, uint8_t brightness_0_100);
void ws2812_task_post_trigger_click(void);
void ws2812_task_post_find_me(uint8_t duration_min);
void ws2812_task_post_ota_transfer(void);
void ws2812_task_post_ota_failed(void);
void ws2812_task_post_ota_success(void);
void ws2812_task_post_set_click_effect(uint8_t effect);
void ws2812_task_post_set_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,
                                        uint16_t timing_ms);

#endif
