#pragma once

#include "ws2812_types.h"

static const unsigned long WS2812_BOOT_MS = 1500;
static const unsigned long WS2812_CHASE_MAX_MS = 2000;
static const uint8_t WS2812_NIGHT_RGB_CAP = 102;
static const uint8_t WS2812_NIGHT_MIN_BRIGHTNESS = 8;

Rgb8 ws2812_night_rgb(uint8_t brightness_0_100);
uint8_t ws2812_normalize_night_brightness(bool on, uint8_t brightness_0_100);
uint16_t ws2812_strobe_total_ms(uint16_t half_period_ms);
uint8_t ws2812_chase_total_steps(uint16_t pixel_count);
uint8_t ws2812_chase_index_for_step(uint8_t step, uint16_t pixel_count);
