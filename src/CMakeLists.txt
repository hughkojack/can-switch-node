set(SRCS
    "main_idf.cpp"
    "common/can.cpp"
    "common/can_mcp2515.cpp"
    "common/config_store.cpp"
    "common/input_engine.cpp"
    "common/boot_ota_guard.cpp"
    "common/node_ota.cpp"
    "common/platform_time.cpp"
    "common/platform_gpio.cpp"
    "common/nvs_store.cpp"
    "common/ws2812_strip.cpp"
    "common/ws2812/ws2812_effects.cpp"
    "common/ws2812/ws2812_hal.cpp"
    "common/ws2812/ws2812_task.cpp"
    "common/ws2812/ws2812_types.cpp"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "."
        "common"
        "common/ws2812"
        "board"
    PRIV_REQUIRES
        esp32-mcp2515
        esp_driver_spi
        esp_driver_gpio
        driver
        nvs_flash
        esp_timer
        freertos
        app_update
        esp_partition
        esp_driver_rmt
        led_strip
)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__led_strip/src")
  target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__led_strip/src")
endif()
