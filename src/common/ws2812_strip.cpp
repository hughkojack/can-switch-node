#include "ws2812_strip.h"

#if defined(WS2812_ENABLE) && WS2812_ENABLE

#include "ws2812/ws2812_task.h"

void ws2812_start_task(void) {
  ws2812_task_start();
}

void ws2812_post_set_night_light(bool on, uint8_t brightness_0_100) {
  ws2812_task_post_set_night_light(on, brightness_0_100);
}

void ws2812_post_trigger_click(void) {
  ws2812_task_post_trigger_click();
}

void ws2812_post_find_me(uint8_t duration_min) {
  ws2812_task_post_find_me(duration_min);
}

void ws2812_post_set_click_effect(uint8_t effect) {
  ws2812_task_post_set_click_effect(effect);
}

void ws2812_post_set_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,
                                   uint16_t timing_ms) {
  ws2812_task_post_set_effect_params(effect_id, r, g, b, timing_ms);
}

void ws2812_request_night_light(bool on, uint8_t brightness_0_100) {
  ws2812_post_set_night_light(on, brightness_0_100);
}

void ws2812_request_trigger_input_effect(void) {
  ws2812_post_trigger_click();
}

void ws2812_request_find_me(uint8_t duration_min) {
  ws2812_post_find_me(duration_min);
}

#endif
