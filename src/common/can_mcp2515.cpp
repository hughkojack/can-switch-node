/**
 * MCP2515 CAN adapter implementation (mechanical node / XIAO CAN board).
 * Uses autowp/arduino-mcp2515; converts to TWAI-style read/write/getStatus.
 */
#include "can_mcp2515.h"
#include <mcp2515.h>
#include <string.h>
#include <Arduino.h>

MCP2515CANAdapter::MCP2515CANAdapter() : m_initialized(false), m_impl(nullptr) {}

MCP2515CANAdapter::~MCP2515CANAdapter() {
  stop();
}

esp_err_t MCP2515CANAdapter::begin(int cs_pin) {
  if (m_initialized) return ESP_ERR_INVALID_STATE;
  MCP2515 *mcp = new MCP2515((uint8_t)cs_pin);
  m_impl = mcp;
  if (mcp->reset() != MCP2515::ERROR_OK) {
    delete mcp;
    m_impl = nullptr;
    return ESP_ERR_INVALID_STATE;
  }
  if (mcp->setBitrate(CAN_500KBPS) != MCP2515::ERROR_OK) {
    delete mcp;
    m_impl = nullptr;
    return ESP_ERR_INVALID_STATE;
  }
  if (mcp->setNormalMode() != MCP2515::ERROR_OK) {
    delete mcp;
    m_impl = nullptr;
    return ESP_ERR_INVALID_STATE;
  }
  m_initialized = true;
  return ESP_OK;
}

esp_err_t MCP2515CANAdapter::stop() {
  if (!m_initialized) return ESP_OK;
  MCP2515 *mcp = (MCP2515 *)m_impl;
  delete mcp;
  m_impl = nullptr;
  m_initialized = false;
  return ESP_OK;
}

esp_err_t MCP2515CANAdapter::write(can::FrameType extd, uint32_t identifier, uint8_t length, uint8_t *buffer) {
  if (!m_initialized || !m_impl) return ESP_ERR_INVALID_STATE;
  MCP2515 *mcp = (MCP2515 *)m_impl;
  struct can_frame f;
  memset(&f, 0, sizeof(f));
  f.can_id = identifier & 0x7FF;  // 11-bit standard
  if (extd == can::FrameType::EXTD_FRAME)
    f.can_id |= CAN_EFF_FLAG;
  f.can_dlc = length > 8 ? 8 : length;
  if (buffer && length)
    memcpy(f.data, buffer, (size_t)f.can_dlc);
  MCP2515::ERROR e = mcp->sendMessage(&f);
  if (e == MCP2515::ERROR_OK) return ESP_OK;
  if (e == MCP2515::ERROR_ALLTXBUSY) return ESP_ERR_TIMEOUT;
  return ESP_FAIL;
}

esp_err_t MCP2515CANAdapter::read(twai_message_t *ptr_message) {
  if (!m_initialized || !m_impl || !ptr_message) return ESP_ERR_INVALID_ARG;
  MCP2515 *mcp = (MCP2515 *)m_impl;
  if (!mcp->checkReceive()) return ESP_ERR_NOT_FOUND;
  struct can_frame f;
  memset(&f, 0, sizeof(f));
  if (mcp->readMessage(&f) != MCP2515::ERROR_OK) return ESP_FAIL;
  memset(ptr_message, 0, sizeof(twai_message_t));
  ptr_message->identifier = f.can_id & 0x1FFFFFFF;
  ptr_message->data_length_code = f.can_dlc > 8 ? 8 : f.can_dlc;
  memcpy(ptr_message->data, f.data, ptr_message->data_length_code);
  ptr_message->flags = 0;
  if (f.can_id & CAN_EFF_FLAG) ptr_message->flags |= TWAI_MSG_FLAG_EXTD;
  return ESP_OK;
}

twai_status_info_t MCP2515CANAdapter::getStatus() const {
  twai_status_info_t st = {};
  if (!m_initialized || !m_impl) {
    st.state = TWAI_STATE_STOPPED;
    st.msgs_to_rx = 0;
    return st;
  }
  st.state = TWAI_STATE_RUNNING;
  st.msgs_to_rx = ((MCP2515 *)m_impl)->checkReceive() ? 1 : 0;
  return st;
}

MCP2515CANAdapter mcp2515_can;
