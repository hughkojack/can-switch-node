#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t platform_millis(void);
void platform_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
