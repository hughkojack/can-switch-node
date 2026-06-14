#pragma once



#include <stdint.h>

#include <stdbool.h>



#ifdef __cplusplus

extern "C" {

#endif



#define WS2812_CLICK_EFFECT_STROBE 0

#define WS2812_CLICK_EFFECT_CHASE  1



#if defined(WS2812_ENABLE) && WS2812_ENABLE



void ws2812_start_task(void);



void ws2812_post_set_night_light(bool on, uint8_t brightness_0_100);

void ws2812_post_trigger_click(void);

void ws2812_post_find_me(uint8_t duration_min);

void ws2812_post_set_click_effect(uint8_t effect);

void ws2812_post_set_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,

                                   uint16_t timing_ms);



void ws2812_request_night_light(bool on, uint8_t brightness_0_100);

void ws2812_request_trigger_input_effect(void);

void ws2812_request_find_me(uint8_t duration_min);



#else



static inline void ws2812_start_task(void) {}

static inline void ws2812_post_set_night_light(bool on, uint8_t brightness_0_100) {

  (void)on;

  (void)brightness_0_100;

}

static inline void ws2812_post_trigger_click(void) {}

static inline void ws2812_post_find_me(uint8_t duration_min) { (void)duration_min; }

static inline void ws2812_post_set_click_effect(uint8_t effect) { (void)effect; }

static inline void ws2812_post_set_effect_params(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b,

                                                 uint16_t timing_ms) {

  (void)effect_id;

  (void)r;

  (void)g;

  (void)b;

  (void)timing_ms;

}

static inline void ws2812_request_night_light(bool on, uint8_t brightness_0_100) {

  (void)on;

  (void)brightness_0_100;

}

static inline void ws2812_request_trigger_input_effect(void) {}

static inline void ws2812_request_find_me(uint8_t duration_min) { (void)duration_min; }



#endif



#ifdef __cplusplus

}

#endif

