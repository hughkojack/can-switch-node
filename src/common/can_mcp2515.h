/**
 * MCP2515 CAN adapter for mechanical node (XIAO CAN Bus Expansion Board).
 * Exposes the same interface as ESP32 TWAI (write, read, getStatus) so
 * common/can.cpp and main.cpp can use CAN unchanged.
 */
#ifndef CAN_MCP2515_H
#define CAN_MCP2515_H

#include <driver/twai.h>
#include <esp_err.h>
#include <cstddef>

namespace can {
enum class FrameType {
  STD_FRAME  = 0,
  EXTD_FRAME = 1
};
}

class MCP2515CANAdapter {
public:
  MCP2515CANAdapter();
  ~MCP2515CANAdapter();

  /**
   * Initialize MCP2515 on SPI with given CS pin.
   * Uses default SPI bus. Bitrate: 500 kbps (matches hub).
   */
  esp_err_t begin(int cs_pin);

  esp_err_t stop();

  esp_err_t write(can::FrameType extd, uint32_t identifier, uint8_t length, uint8_t *buffer);
  esp_err_t read(twai_message_t *ptr_message);
  twai_status_info_t getStatus() const;

private:
  bool m_initialized;
  void *m_impl;  // MCP2515* opaque to avoid including mcp2515.h here (different can_frame)
};

extern MCP2515CANAdapter mcp2515_can;

#endif
