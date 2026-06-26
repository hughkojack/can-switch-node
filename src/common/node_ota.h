#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_MSG_OTA_REMOTE    0xE
#define CAN_MSG_OTA_NODE      0xF

#define OTA_FLASH_READY      0x04
#define OTA_FLASH_INIT       0x06
#define OTA_FLASH_DATA       0x08
#define OTA_FLASH_DATA_ERR   0x0D
#define OTA_FLASH_DONE       0x10
#define OTA_FLASH_COMPLETE   0x14
#define OTA_FLASH_ERROR      0x15
#define OTA_FLASH_ABORT      0x18
#define OTA_DATA_BYTES_PER_FRAME  4
#define OTA_SEGMENT_BYTES         4
/* FLASH_INIT byte 3: 0 = legacy 4-byte blocks; 16 or 32 = multi-frame block ACK */
#define OTA_TRANSFER_BLOCK_BYTES  16
#define OTA_BLOCK_BUF_MAX         32
#define FW_OTA_IDLE_TIMEOUT_MS   (2U * 60U * 1000U)

#ifndef FW_VERSION
#define FW_VERSION 0x0100
#endif

#ifndef OTA_CAPABLE
#define OTA_CAPABLE 0
#endif

#if defined(NODE_ROLE_MIN)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void node_ota_set_node_id(uint8_t node_id);
void node_ota_set_poll_task(TaskHandle_t task);
bool node_ota_is_active(void);
bool node_ota_pending_reboot(void);
void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc);
void node_ota_drain_notifications(void);
void node_ota_service(unsigned long now_ms);

#else

static inline void node_ota_set_node_id(uint8_t node_id) { (void)node_id; }
static inline bool node_ota_is_active(void) { return false; }
static inline bool node_ota_pending_reboot(void) { return false; }
static inline void node_ota_on_can_frame(const uint8_t* data, uint8_t dlc) {
  (void)data; (void)dlc;
}
static inline void node_ota_service(unsigned long now_ms) { (void)now_ms; }

#endif

#ifdef __cplusplus
}
#endif
