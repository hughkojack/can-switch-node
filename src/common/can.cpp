#include "can.h"
#include "ESP32TWAISingleton.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino.h>

uint16_t can_id(uint8_t msg_type, uint8_t node_id) {
  return ((uint16_t)msg_type << 7) | (node_id & 0x7F);
}

static inline bool can_send_raw(uint16_t id, const uint8_t *data, uint8_t dlc) {
  esp_err_t r = CAN.write(can::FrameType::STD_FRAME, id, dlc, (uint8_t*)data);
  Serial.printf("can_send_raw id=0x%03X dlc=%u r=%d\n", id, dlc, (int)r);
  return r == ESP_OK;
}  



// ---- Convenience sends (event codes match HUB CanInputEventCode) ----
bool can_send_click(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_CLICK };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_double_click(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_DOUBLE_CLICK };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_triple_click(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_TRIPLE_CLICK };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_hold(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_HOLD };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_long_hold(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_LONG_HOLD };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_hold_repeat(uint8_t node_id, uint8_t input_id) {
  uint8_t d[2] = { input_id, (uint8_t)EVT_HOLD_REPEAT };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 2);
}

bool can_send_dim(uint8_t node_id, uint8_t input_id, uint8_t brightness_0_100) {
  uint8_t d[3] = { input_id, (uint8_t)EVT_DIM, brightness_0_100 };
  return can_send_raw(can_id(CAN_MSG_INPUT_EVENT, node_id), d, 3);
}

bool can_send_node_announce(uint8_t node_id, uint8_t node_type, uint8_t input_count) {
  uint8_t d[2] = { node_type, input_count };
  return can_send_raw(can_id(CAN_MSG_NODE_ANNOUNCE, node_id), d, 2);
}