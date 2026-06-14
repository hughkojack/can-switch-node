#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call at the very start of setup() after Serial.begin.
// Marks running OTA image valid; if running slot is corrupt but the other slot
// has a valid image, switches otadata and reboots into it.
void boot_ota_guard(void);

#ifdef __cplusplus
}
#endif
