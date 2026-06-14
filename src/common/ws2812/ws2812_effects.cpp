#include "ws2812_effects.h"

Rgb8 ws2812_night_rgb(uint8_t brightness_0_100) {
  const uint32_t scale = (uint32_t)brightness_0_100 * WS2812_NIGHT_RGB_CAP / 100;
  Rgb8 c;
  c.r = (uint8_t)((255UL * scale) / WS2812_NIGHT_RGB_CAP);
  c.g = (uint8_t)((180UL * scale) / WS2812_NIGHT_RGB_CAP);
  c.b = (uint8_t)((80UL * scale) / WS2812_NIGHT_RGB_CAP);
  return c;
}

uint8_t ws2812_normalize_night_brightness(bool on, uint8_t brightness_0_100) {
  if (!on) return 0;
  if (brightness_0_100 == 0) return WS2812_NIGHT_MIN_BRIGHTNESS;
  return brightness_0_100;
}

uint16_t ws2812_strobe_total_ms(uint16_t half_period_ms) {
  if (half_period_ms < 10) half_period_ms = 10;
  if (half_period_ms > 500) half_period_ms = 500;
  return (uint16_t)(half_period_ms * 2 + 17);
}

uint8_t ws2812_chase_total_steps(uint16_t pixel_count) {
  if (pixel_count <= 1) return 0;
  return (uint8_t)((2 * pixel_count) - 2);
}

uint8_t ws2812_chase_index_for_step(uint8_t step, uint16_t pixel_count) {
  const uint8_t last = (uint8_t)(pixel_count - 1);
  if (step <= last) return step;
  return (uint8_t)((2 * last) - step);
}
