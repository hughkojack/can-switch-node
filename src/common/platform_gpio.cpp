#include "platform_gpio.h"

#ifdef ARDUINO
#include <Arduino.h>

void platform_gpio_input_pullup(uint8_t gpio) {
  pinMode(gpio, INPUT_PULLUP);
}

void platform_gpio_input_pulldown(uint8_t gpio) {
  pinMode(gpio, INPUT_PULLDOWN);
}

void platform_gpio_output(uint8_t gpio) {
  pinMode(gpio, OUTPUT);
}

bool platform_gpio_read(uint8_t gpio) {
  return digitalRead(gpio) == HIGH;
}

void platform_gpio_write(uint8_t gpio, bool high) {
  digitalWrite(gpio, high ? HIGH : LOW);
}

#else
#include "driver/gpio.h"

void platform_gpio_input_pullup(uint8_t gpio) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&cfg);
}

void platform_gpio_input_pulldown(uint8_t gpio) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpio_config(&cfg);
}

void platform_gpio_output(uint8_t gpio) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << gpio;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&cfg);
}

bool platform_gpio_read(uint8_t gpio) {
  return gpio_get_level((gpio_num_t)gpio) != 0;
}

void platform_gpio_write(uint8_t gpio, bool high) {
  gpio_set_level((gpio_num_t)gpio, high ? 1 : 0);
}

#endif
