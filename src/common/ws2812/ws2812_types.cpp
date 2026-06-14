#include "ws2812_types.h"

void ws2812_config_set_defaults(Ws2812RuntimeConfig* cfg) {
  if (!cfg) return;
  cfg->strobe = {{220, 180, 80}, 45};
  cfg->chase = {{80, 60, 24}, 50};
  cfg->find_me = {{180, 120, 20}, 150};
  cfg->click_effect = 0;
  cfg->night_on = false;
  cfg->night_brightness = 0;
}
