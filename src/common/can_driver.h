/**
 * Single CAN backend for the application.
 * - LCD node: ESP32 TWAI (onboard transceiver, GPIO 19/20).
 * - Mechanical node: MCP2515 over SPI (XIAO CAN Bus Expansion Board).
 * Both expose the same CAN.write / CAN.read / CAN.getStatus interface.
 */
#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#if defined(NODE_ROLE_LCD)
  #include "ESP32TWAISingleton.hpp"
  #define CAN (can::ESP32TWAISingleton::getInstance())
#elif defined(NODE_ROLE_MIN)
  #include "can_mcp2515.h"
  #define CAN (mcp2515_can)
  // For mechanical, can::FrameType is provided by can_mcp2515.h
  // Baudrate is fixed 500k in MCP2515 adapter; begin(cs_pin) only.
#else
  #error "Define NODE_ROLE_LCD or NODE_ROLE_MIN"
#endif

#endif
