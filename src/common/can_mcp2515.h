/**
 * MCP2515 CAN adapter for mechanical node (XIAO CAN Bus Expansion Board).
 */
#ifndef CAN_MCP2515_H
#define CAN_MCP2515_H

#include <driver/spi_master.h>
#include <esp_err.h>
#include <cstddef>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <driver/twai.h>
#pragma GCC diagnostic pop

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

  esp_err_t begin(spi_device_handle_t* spi_handle);

  esp_err_t stop();
  bool isInitialized() const { return m_initialized; }

  esp_err_t write(can::FrameType extd, uint32_t identifier, uint8_t length, uint8_t *buffer);
  esp_err_t read(twai_message_t *ptr_message);
  twai_status_info_t getStatus() const;

private:
  bool m_initialized;
  void *m_impl;
};

extern MCP2515CANAdapter mcp2515_can;

#endif
