#include "can.h"
#include "can_driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef FW_VERSION
#define FW_VERSION 0
#endif
#ifndef OTA_CAPABLE
#define OTA_CAPABLE 0
#endif

uint16_t can_id(uint8_t msg_type, uint8_t node_id) {
  return ((uint16_t)msg_type << 7) | (node_id & 0x7F);
}

static inline bool can_send_raw(uint16_t id, const uint8_t *data, uint8_t dlc) {
  esp_err_t r = CAN.write(can::FrameType::STD_FRAME, id, dlc, (uint8_t*)data);
#if defined(CAN_DEBUG_SERIAL)
  const uint8_t msg_type = (uint8_t)((id >> 7) & 0x0F);
  const uint8_t node_id = (uint8_t)(id & 0x7F);
  if (msg_type == CAN_MSG_NODE_ANNOUNCE) {
    Serial.printf("can_send HEARTBEAT node=%u id=0x%03X r=%d\n", (unsigned)node_id, id, (int)r);
  } else if (msg_type == CAN_MSG_INPUT_EVENT) {
    Serial.printf("can_send INPUT id=0x%03X node=%u in=%u evt=0x%02X r=%d\n",
                  id, (unsigned)node_id,
                  dlc >= 1 ? (unsigned)data[0] : 0U,
                  dlc >= 2 ? (unsigned)data[1] : 0U,
                  (int)r);
  } else {
    Serial.printf("can_send_raw id=0x%03X type=%u node=%u dlc=%u r=%d\n",
                    id, (unsigned)msg_type, (unsigned)node_id, (unsigned)dlc, (int)r);
  }
#endif
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
  return can_send_node_announce_ex(node_id, node_type, input_count, (uint16_t)FW_VERSION,
                                   (uint8_t)OTA_CAPABLE);
}

bool can_send_node_announce_ex(uint8_t node_id, uint8_t node_type, uint8_t input_count,
                               uint16_t fw_version, uint8_t ota_capable) {
  uint8_t d[5] = {
    node_type,
    input_count,
    (uint8_t)(fw_version & 0xFF),
    (uint8_t)((fw_version >> 8) & 0xFF),
    (uint8_t)(ota_capable ? 1u : 0u),
  };
  return can_send_raw(can_id(CAN_MSG_NODE_ANNOUNCE, node_id), d, 5);
}

bool can_send_firmware_status(uint8_t node_id, uint8_t status, uint16_t seq, uint32_t extra) {
  uint8_t d[7] = {
    status,
    (uint8_t)(seq & 0xFF),
    (uint8_t)((seq >> 8) & 0xFF),
    (uint8_t)(extra & 0xFF),
    (uint8_t)((extra >> 8) & 0xFF),
    (uint8_t)((extra >> 16) & 0xFF),
    (uint8_t)((extra >> 24) & 0xFF),
  };
  return can_send_raw(can_id(CAN_MSG_FIRMWARE_STATUS, node_id), d, 7);
}