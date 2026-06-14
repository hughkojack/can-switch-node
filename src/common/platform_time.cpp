#include "platform_time.h"

#ifdef ARDUINO
#include <Arduino.h>

uint32_t platform_millis(void) {
  return (uint32_t)millis();
}

void platform_delay_ms(uint32_t ms) {
  delay(ms);
}

#else
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint32_t platform_millis(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void platform_delay_ms(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

#endif
