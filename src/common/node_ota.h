#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_MSG_FIRMWARE         0x5
#define CAN_MSG_FIRMWARE_STATUS  0x9

#define FW_CMD_BEGIN  0x01
#define FW_CMD_DATA   0x02
#define FW_CMD_END    0x03
#define FW_CMD_ABORT  0x04

#define FW_BEGIN_PART0  0
#define FW_BEGIN_PART1  1

#define FW_STATUS_IDLE     0
#define FW_STATUS_READY    1
#define FW_STATUS_ACK      2
#define FW_STATUS_NACK     3
#define FW_STATUS_COMPLETE 4
#define FW_STATUS_ERROR    5
#define FW_STATUS_BUSY     6

#define FW_DATA_BYTES_PER_FRAME  5
#define FW_OTA_IDLE_TIMEOUT_MS   30000

#ifndef FW_VERSION
#define FW_VERSION 0x0100
#endif

#ifndef OTA_CAPABLE
#define OTA_CAPABLE 0
#endif

#if defined(NODE_ROLE_MIN)

void node_ota_set_node_id(uint8_t node_id);
bool node_ota_is_active(void);
void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc);
void node_ota_service(unsigned long now_ms);

#else

static inline void node_ota_set_node_id(uint8_t node_id) { (void)node_id; }
static inline bool node_ota_is_active(void) { return false; }
static inline void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc) {
  (void)data; (void)dlc;
}
static inline void node_ota_service(unsigned long now_ms) { (void)now_ms; }

#endif

#ifdef __cplusplus
}
#endif
