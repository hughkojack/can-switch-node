#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool nvs_store_open_read(const char* ns);
bool nvs_store_open_write(const char* ns);
void nvs_store_close(void);
bool nvs_store_has_key(const char* key);
size_t nvs_store_get_blob(const char* key, void* out, size_t len);
size_t nvs_store_put_blob(const char* key, const void* data, size_t len);
uint8_t nvs_store_get_u8(const char* key, uint8_t default_val);
bool nvs_store_put_u8(const char* key, uint8_t val);
bool nvs_store_get_bool(const char* key, bool default_val);
bool nvs_store_put_bool(const char* key, bool val);

#ifdef __cplusplus
}
#endif
