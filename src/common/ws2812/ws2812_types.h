#pragma once

#include <stdint.h>

#ifndef WS2812_GPIO
#define WS2812_GPIO 7
#endif
#ifndef WS2812_COUNT
#define WS2812_COUNT 12
#endif

struct Rgb8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

enum Ws2812EffectId : uint8_t {
  WS2812_EFFECT_STROBE = 0,
  WS2812_EFFECT_CHASE = 1,
  WS2812_EFFECT_FIND_ME = 2,
};

struct Ws2812EffectParams {
  Rgb8 rgb;
  uint16_t timing_ms;
};

struct Ws2812RuntimeConfig {
  Ws2812EffectParams strobe;
  Ws2812EffectParams chase;
  Ws2812EffectParams find_me;
  uint8_t click_effect;
  bool night_on;
  uint8_t night_brightness;
};

enum Ws2812CmdType : uint8_t {
  WS2812_CMD_SET_NIGHT_LIGHT = 0,
  WS2812_CMD_TRIGGER_CLICK,
  WS2812_CMD_START_FIND_ME,
  WS2812_CMD_SET_CLICK_EFFECT,
  WS2812_CMD_SET_EFFECT_PARAMS,
  WS2812_CMD_RUN_BOOT_TEST,
  WS2812_CMD_LOAD_NVS_BASELINE,
  WS2812_CMD_OTA_TRANSFER,
  WS2812_CMD_OTA_FAILED,
  WS2812_CMD_OTA_SUCCESS,
};

struct Ws2812NightLightCmd {
  bool on;
  uint8_t brightness;
};

struct Ws2812EffectParamsCmd {
  uint8_t effect_id;
  Rgb8 rgb;
  uint16_t timing_ms;
};

struct Ws2812Cmd {
  Ws2812CmdType type;
  union {
    Ws2812NightLightCmd night;
    uint8_t find_me_min;
    uint8_t click_effect;
    Ws2812EffectParamsCmd effect_params;
  } u;
};

void ws2812_config_set_defaults(Ws2812RuntimeConfig* cfg);
