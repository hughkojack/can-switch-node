#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void platform_gpio_input_pullup(uint8_t gpio);
void platform_gpio_input_pulldown(uint8_t gpio);
void platform_gpio_output(uint8_t gpio);
bool platform_gpio_read(uint8_t gpio);
void platform_gpio_write(uint8_t gpio, bool high);

#ifdef __cplusplus
}
#endif
