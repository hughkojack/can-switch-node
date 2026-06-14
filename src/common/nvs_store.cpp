#include "nvs_store.h"

#ifndef ARDUINO

#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static nvs_handle_t s_nvs = 0;

bool nvs_store_open_read(const char* ns) {
  return nvs_open(ns, NVS_READONLY, &s_nvs) == ESP_OK;
}

bool nvs_store_open_write(const char* ns) {
  return nvs_open(ns, NVS_READWRITE, &s_nvs) == ESP_OK;
}

void nvs_store_close(void) {
  if (s_nvs) {
    nvs_close(s_nvs);
    s_nvs = 0;
  }
}

bool nvs_store_has_key(const char* key) {
  size_t len = 0;
  return nvs_get_blob(s_nvs, key, NULL, &len) == ESP_OK;
}

size_t nvs_store_get_blob(const char* key, void* out, size_t len) {
  size_t required = len;
  if (nvs_get_blob(s_nvs, key, out, &required) != ESP_OK)
    return 0;
  return required;
}

size_t nvs_store_put_blob(const char* key, const void* data, size_t len) {
  if (nvs_set_blob(s_nvs, key, data, len) != ESP_OK)
    return 0;
  if (nvs_commit(s_nvs) != ESP_OK)
    return 0;
  return len;
}

uint8_t nvs_store_get_u8(const char* key, uint8_t default_val) {
  uint8_t v = default_val;
  nvs_get_u8(s_nvs, key, &v);
  return v;
}

bool nvs_store_put_u8(const char* key, uint8_t val) {
  if (nvs_set_u8(s_nvs, key, val) != ESP_OK)
    return false;
  return nvs_commit(s_nvs) == ESP_OK;
}

bool nvs_store_get_bool(const char* key, bool default_val) {
  uint8_t v = default_val ? 1 : 0;
  nvs_get_u8(s_nvs, key, &v);
  return v != 0;
}

bool nvs_store_put_bool(const char* key, bool val) {
  return nvs_store_put_u8(key, val ? 1 : 0);
}

#endif
