#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_XIAO_ESP32C3)
#define BOARD_XIAO_ESP32C3 1
#elif defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_XIAO_ESP32S3)
#define BOARD_XIAO_ESP32S3 1
#endif

#if defined(BOARD_XIAO_ESP32C3)
#define CAN_SPI_HOST       SPI2_HOST
#define CAN_SPI_MOSI_GPIO  GPIO_NUM_10
#define CAN_SPI_MISO_GPIO  GPIO_NUM_9
#define CAN_SPI_SCK_GPIO   GPIO_NUM_8
#ifndef CAN_CS_GPIO
#define CAN_CS_GPIO        20
#endif
#elif defined(BOARD_XIAO_ESP32S3)
#define CAN_SPI_HOST       SPI2_HOST
#define CAN_SPI_MOSI_GPIO  GPIO_NUM_9
#define CAN_SPI_MISO_GPIO  GPIO_NUM_8
#define CAN_SPI_SCK_GPIO   GPIO_NUM_7
#ifndef CAN_CS_GPIO
#define CAN_CS_GPIO        44
#endif
#else
#error "Unsupported board — define CAN_CS_GPIO and SPI pins for this target"
#endif
