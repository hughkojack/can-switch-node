#pragma once

#include "ws2812_types.h"
#include <stdint.h>
#include <stdbool.h>

bool ws2812_hal_init(void);
bool ws2812_hal_ready(void);
void ws2812_hal_deinit(void);

void ws2812_hal_set_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
void ws2812_hal_fill_rgb(uint8_t r, uint8_t g, uint8_t b);
bool ws2812_hal_refresh(void);
