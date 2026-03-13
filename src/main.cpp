#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "common/can.h"
#include "common/input_engine.h"
#include "common/config_store.h"

// Node ID comes from config (default NODE_ID_UNCONFIGURED = 127 until hub assigns one)

enum CanCommandType : uint8_t {
  SET_BRIGHTNESS = 0x10,
  SET_STATE      = 0x11
};

// CAN message types for hub/lighting (same numeric values as in can.h where applicable)
enum CanMessageType : uint8_t {
  LIGHTING_COMMAND = 0x1,
  SENSOR_DATA      = 0x2,
  NODE_CONFIG      = CAN_MSG_NODE_CONFIG,  // 0x3 hub -> node config
  HEARTBEAT        = CAN_MSG_NODE_ANNOUNCE // 0x8 node -> hub announce
};

/**
 * The example demonstrates how to port LVGL.
 *
 * ## How to Use
 *
 * To use this example, please firstly install `ESP32_Display_Panel` (including its dependent libraries) and
 * `lvgl` (v8.3.x) libraries, then follow the steps to configure them:
 *
 * 1. [Configure ESP32_Display_Panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel#configure-esp32_display_panel)
 * 2. [Configure LVGL](https://github.com/esp-arduino-libs/ESP32_Display_Panel#configure-lvgl)
 * 3. [Configure Board](https://github.com/esp-arduino-libs/ESP32_Display_Panel#configure-board)
 *
*/

#if defined(NODE_ROLE_LCD)
  #include "ui/ui_helper.h"
#endif

void start_ui()
{
#if defined(NODE_ROLE_LCD)
  setup_wall_switch_ui();
#endif
}

#include "common/can_driver.h" // provides CAN (TWAI or MCP2515)

#if defined(NODE_ROLE_LCD)
#include <lvgl.h>
#include <ESP_Panel_Library.h>
#endif
#include <ESP_IOExpander_Library.h>


// Extend IO Pin define
#define TP_RST 1
#define LCD_BL 2
#define LCD_RST 3
#define SD_CS 4
#define USB_SEL 5

// I2C Pin define
#define I2C_MASTER_NUM 0
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_SCL_IO 9

//CAN Pin define
#define CAN_GPIO_RX GPIO_NUM_19
#define CAN_GPIO_TX GPIO_NUM_20

#if defined(NODE_ROLE_MIN)
// Mechanical node: Seeed XIAO ESP32-S3 + XIAO CAN Bus Expansion Board (MCP2515).
// Expansion board CS is connected to D7 pad; on XIAO_ESP32S3 variant D7 = GPIO 44 (see pins_arduino.h).
#ifndef CAN_CS_GPIO
#define CAN_CS_GPIO 44
#endif
#endif

#if defined(NODE_ROLE_LCD)
/* LVGL porting configurations */
#define LVGL_TASK_MAX_DELAY_MS  (500)
#define LVGL_TASK_MIN_DELAY_MS  (1)
#define LVGL_TASK_STACK_SIZE    (4 * 1024)
#define LVGL_TASK_PRIORITY      (2)
#define LVGL_BUF_SIZE           (ESP_PANEL_LCD_H_RES * 100)

ESP_Panel *panel = NULL;
SemaphoreHandle_t lvgl_mux = NULL;                  // LVGL mutex
#endif


//---- helpers to send commands to the LED controller hub ----

static node_config_t cfg;

// For find-me: I/O index = ESP32-S3 chip I/O (GPIO number), independent of input count (1-6).
static ESP_IOExpander* g_expander = nullptr;
static unsigned long find_me_until = 0;   // millis() when to turn find-me output off
static uint8_t find_me_output_index = 0;  // cached from NVS: GPIO number (0-48)
static input_timing_t s_timing;            // timing from NVS, passed to input_engine

// CAN status: dot indicator (green / red flashing) driven by s_last_can_rx_ms
static unsigned long s_last_can_rx_ms = 0;
#define CAN_STATUS_IDLE_MS 2500

// Find Me I/O index = ESP32-S3 GPIO number (0-48). Use a GPIO that is free on your board (not used by CAN, I2C, etc.).
#define FIND_ME_GPIO_MAX 48

static inline uint16_t createCanId(CanMessageType msgType, uint8_t nodeId) {
  return ((uint16_t)msgType << 7) | (nodeId & 0x7F);
}
static inline CanMessageType getMessageType(uint32_t canId) {
  return (CanMessageType)((canId >> 7) & 0x0F);
}
static inline uint8_t getNodeId(uint32_t canId) {
  return (uint8_t)(canId & 0x7F);
}

static void can_poll_task(void* arg);  // forward decl for setup()

// ---- helpers to send commands to the LED controller hub ----


#if defined(NODE_ROLE_LCD)
// This function will be called by the panel when it's ready to flush the next frame (after the previous one is done)
/* Display flushing */
void lvgl_port_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    panel->getLcd()->drawBitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(disp);
}

#if ESP_PANEL_USE_LCD_TOUCH
/* Read the touchpad */
void lvgl_port_tp_read(lv_indev_drv_t * indev, lv_indev_data_t * data)
{
    panel->getLcdTouch()->readData();

    bool touched = panel->getLcdTouch()->getTouchState();
    if(!touched) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        TouchPoint point = panel->getLcdTouch()->getPoint();

        data->state = LV_INDEV_STATE_PR;
        /*Set the coordinates*/
        data->point.x = point.x;
        data->point.y = point.y;

        //Serial.printf("Touch point: x %d, y %d\n", point.x, point.y);
    }
}
#endif

void lvgl_port_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks);
}

// Returns true if lock was acquired, false if timeout. Caller must only call lvgl_port_unlock() if true.
bool lvgl_port_lock_with_timeout(int timeout_ms)
{
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(lvgl_mux);
}

void lvgl_port_task(void *arg)
{
    Serial.println("Starting LVGL task");

    uint32_t task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        lvgl_port_lock(-1);
        task_delay_ms = lv_timer_handler();
        // Release the mutex
        lvgl_port_unlock();
        if (task_delay_ms > LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}
#endif // NODE_ROLE_LCD


void setup()
{
    Serial.begin(115200); /* prepare for possible serial debug */
    while (!Serial && millis() < 5000) { delay(10); }  // wait a bit for monitor
    Serial.println("CDC alive");
    
    config_load(&cfg);
    
    Serial.printf("CFG node=%u count=%u\n", cfg.node_id, cfg.input_count);
    for (int i=0; i<cfg.input_count; i++) {
        Serial.printf("CFG[%d] id=%u mode=%u\n", i, cfg.inputs[i].input_id, cfg.inputs[i].mode);
    }

#if defined(NODE_ROLE_LCD)
    String LVGL_Arduino = "Hello LVGL! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.println(LVGL_Arduino);
    Serial.println("I am ESP32_Display_Panel");

    panel = new ESP_Panel();

    /* Initialize LVGL core */
    lv_init();

    /* Initialize LVGL buffers */
    static lv_disp_draw_buf_t draw_buf;
    /* Double buffer in PSRAM (100 lines × 800 × 2 bytes × 2 buffers = 320KB; internal SRAM is too small) */
    uint8_t *buf1 = (uint8_t *)heap_caps_calloc(1, LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    uint8_t *buf2 = (uint8_t *)heap_caps_calloc(1, LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        /* Fallback: single buffer, 40 lines, in internal RAM if PSRAM unavailable */
        buf1 = (uint8_t *)heap_caps_calloc(1, (ESP_PANEL_LCD_H_RES * 40) * sizeof(lv_color_t), MALLOC_CAP_INTERNAL);
        buf2 = NULL;
        lv_disp_draw_buf_init(&draw_buf, buf1, NULL, ESP_PANEL_LCD_H_RES * 40);
    } else {
        lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LVGL_BUF_SIZE);
    }
    assert(buf1);

    /* Initialize the display device */
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    /* Change the following line to your display resolution */
    disp_drv.hor_res = ESP_PANEL_LCD_H_RES;
    disp_drv.ver_res = ESP_PANEL_LCD_V_RES;
    disp_drv.flush_cb = lvgl_port_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

#if ESP_PANEL_USE_LCD_TOUCH
    /* Initialize the input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_port_tp_read;
    lv_indev_drv_register(&indev_drv);
#endif
    /* Initialize bus and device of panel */
    panel->init();
#if ESP_PANEL_LCD_BUS_TYPE != ESP_PANEL_BUS_TYPE_RGB
    /* Register a function to notify LVGL when the panel is ready to flush */
    /* This is useful for refreshing the screen using DMA transfers */
    panel->getLcd()->setCallback(notify_lvgl_flush_ready, &disp_drv);
#endif

    /**
     * These development boards require the use of an IO expander to configure the screen,
     * so it needs to be initialized in advance and registered with the panel for use.
     *
     */
    Serial.println("Initialize IO expander");
    /* Initialize IO expander */
    // ESP_IOExpander *expander = new ESP_IOExpander_CH422G(I2C_MASTER_NUM, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS_000, I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO);
    ESP_IOExpander *expander = new ESP_IOExpander_CH422G(I2C_MASTER_NUM, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS_000);
    expander->init();
    expander->begin();
    expander->multiPinMode(TP_RST | LCD_BL | LCD_RST | SD_CS | USB_SEL, OUTPUT);
    expander->multiDigitalWrite(TP_RST | LCD_BL | LCD_RST | SD_CS, HIGH);

    // Turn off backlight
      expander->digitalWrite(USB_SEL, LOW);
    /* Add into panel */
    panel->addIOExpander(expander);
    g_expander = expander;

    /* Start panel */
    panel->begin();

    /* Create a task to run the LVGL task periodically */
    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    /* Lock the mutex due to the LVGL APIs are not thread-safe */
    lvgl_port_lock(-1);

    setup_wall_switch_ui();

    /* Release the mutex */
    lvgl_port_unlock();

    Serial.println("LCD Setup done");
    //Enable CAN Transceiver path
    expander->digitalWrite(USB_SEL, HIGH);
#else
    /* Mechanical node: XIAO ESP32-S3 + XIAO CAN Bus Expansion Board (no I2C expander) */
    Serial.println("Node (mechanical, XIAO + CAN expansion)");
#endif

    delay(500); // Short delay to ensure the transceiver is ready

#if defined(NODE_ROLE_LCD)
    CAN.begin(CAN_GPIO_RX, CAN_GPIO_TX, can::Baudrate::BAUD_500KBPS);
#elif defined(NODE_ROLE_MIN)
    if (CAN.begin(CAN_CS_GPIO) != ESP_OK) {
        Serial.printf("MCP2515 CAN init failed (CS=%d)\n", CAN_CS_GPIO);
    }
#endif

    auto st = CAN.getStatus();
    if (st.state != 1) {
        Serial.printf("CAN not running (state=%d)\n", st.state);
        // handle error / retry / recovery here
    } else {
        Serial.println("CAN running");
    }
        
    // init input engine with loaded config
    config_get_timing(&s_timing);
    input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);

    // Announce to hub (node_id, type, input_count) so hub can detect new/unconfigured nodes
#if defined(NODE_ROLE_LCD)
    can_send_node_announce(cfg.node_id, NODE_TYPE_LCD, cfg.input_count);
#elif defined(NODE_ROLE_MIN)
    can_send_node_announce(cfg.node_id, NODE_TYPE_MECHANICAL, cfg.input_count);
#else
    can_send_node_announce(cfg.node_id, NODE_TYPE_LCD, cfg.input_count);
#endif

    xTaskCreate(can_poll_task, "can_poll", 12288, NULL, 1, NULL);
}

void handle_can_messages() {
    twai_message_t message;
    twai_status_info_t status = CAN.getStatus();

    if (status.msgs_to_rx > 0) {
        if (CAN.read(&message) == ESP_OK) {
            // Handle hub -> node config (addressed to this node or unconfigured)
            uint8_t msg_type = (uint8_t)((message.identifier >> 7) & 0x0F);
            uint8_t target_id = (uint8_t)(message.identifier & 0x7F);
            if (msg_type == CAN_MSG_STATE_FEEDBACK && target_id == cfg.node_id && message.data_length_code >= 4) {
#if defined(NODE_ROLE_LCD)
                if (lvgl_port_lock_with_timeout(50)) {
                    for (int i = 0; i < 4; i++)
                        ui_set_feedback_brightness((uint8_t)i, message.data[i]);
                    lvgl_port_unlock();
                }
#endif
            } else if (msg_type == CAN_MSG_NODE_CONFIG && target_id == cfg.node_id && message.data_length_code >= 1) {
                uint8_t cmd = message.data[0];
                if (cmd == CMD_SET_NODE_ID && message.data_length_code >= 2) {
                    uint8_t new_id = message.data[1];
                    if (new_id != 0 && new_id <= 126) {
                        cfg.node_id = new_id;
                        config_save(&cfg);
                        config_get_timing(&s_timing);
                        input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                        Serial.printf("CONFIG: node_id set to %u\n", (unsigned)cfg.node_id);
                    }
                } else if (cmd == CMD_SET_INPUT_CFG && message.data_length_code >= 4) {
                    uint8_t idx = message.data[1];
                    if (idx < MAX_INPUTS_PER_NODE) {
                        cfg.inputs[idx].input_id = message.data[2];
                        cfg.inputs[idx].mode = (input_mode_t)(message.data[3] & 1);
                        if (message.data_length_code >= 5)
                            cfg.input_gpio[idx] = message.data[4];
                        config_save(&cfg);
                        config_get_timing(&s_timing);
                        input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                        Serial.printf("CONFIG: input[%u] -> id=%u mode=%u gpio=%u\n", (unsigned)idx, (unsigned)cfg.inputs[idx].input_id, (unsigned)cfg.inputs[idx].mode, (unsigned)cfg.input_gpio[idx]);
                    }
                } else if (cmd == CMD_SET_INPUT_COUNT && message.data_length_code >= 2) {
                    uint8_t n = message.data[1];
                    if (n >= 1 && n <= 6) {
                        cfg.input_count = n;
                        config_save(&cfg);
                        config_get_timing(&s_timing);
                        input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                        Serial.printf("CONFIG: input_count set to %u\n", (unsigned)cfg.input_count);
                    }
                } else if (cmd == CMD_SET_TIMING && message.data_length_code >= 8) {
                    // CAN frame max 8 bytes: cmd + 7 data (click_max, gap, hold: 2B each; long_hold: 1B)
                    s_timing.click_max_ms       = (uint16_t)message.data[1] | ((uint16_t)message.data[2] << 8);
                    s_timing.double_click_gap_ms = (uint16_t)message.data[3] | ((uint16_t)message.data[4] << 8);
                    s_timing.hold_min_ms        = (uint16_t)message.data[5] | ((uint16_t)message.data[6] << 8);
                    s_timing.long_hold_min_ms    = (message.data_length_code >= 9)
                        ? ((uint16_t)message.data[7] | ((uint16_t)message.data[8] << 8))
                        : (uint16_t)message.data[7];
                    config_set_timing(&s_timing);
                    input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                    Serial.printf("CONFIG: timing updated\n");
                } else if (cmd == CMD_FIND_ME) {
                    uint8_t duration_min = (message.data_length_code >= 2) ? message.data[1] : 5;
                    if (duration_min == 0) duration_min = 5;
                    config_get_find_me_output(&find_me_output_index);
                    if (find_me_output_index <= FIND_ME_GPIO_MAX) {
                        uint8_t gpio = find_me_output_index;
                        pinMode(gpio, OUTPUT);
                        if (gpio == 0) {
                            // Index 0 = solid on for duration
                            digitalWrite(gpio, HIGH);
                        }
                        // Non-zero: poll task will blink
                    }
                    find_me_until = millis() + (unsigned long)duration_min * 60 * 1000;
                    Serial.printf("CONFIG: find-me on for %u min (GPIO=%u)\n", (unsigned)duration_min, (unsigned)find_me_output_index);
                } else if (cmd == CMD_SET_FIND_ME_OUTPUT && message.data_length_code >= 2) {
                    config_set_find_me_output(message.data[1]);
                    config_get_find_me_output(&find_me_output_index);
                    Serial.printf("CONFIG: find-me output index set to %u\n", (unsigned)find_me_output_index);
                } else if (cmd == CMD_SET_DATETIME && message.data_length_code >= 5) {
                    // Hub sends Unix timestamp every hour; set node RTC so status bar shows date/time
                    uint32_t ts = (uint32_t)message.data[1] | ((uint32_t)message.data[2] << 8)
                        | ((uint32_t)message.data[3] << 16) | ((uint32_t)message.data[4] << 24);
                    if (ts > 0) {
                        struct timeval tv = { .tv_sec = (time_t)ts, .tv_usec = 0 };
                        if (settimeofday(&tv, NULL) == 0)
                            Serial.printf("CONFIG: datetime set from hub (ts=%u)\n", (unsigned)ts);
                    }
                } else if (cmd == CMD_REBOOT) {
                    Serial.println("CONFIG: reboot requested from hub");
                    ESP.restart();
                } else if (cmd == CMD_SET_INPUT_LABEL && message.data_length_code >= 3) {
                    uint8_t idx = message.data[1];
                    uint8_t seg = message.data[2];
                    if (idx < MAX_INPUTS_PER_NODE) {
                        static uint8_t label_len[MAX_INPUTS_PER_NODE];
                        static size_t label_pos[MAX_INPUTS_PER_NODE];
                        if (seg == 0) {
                            cfg.input_labels[idx][0] = '\0';
                            config_save(&cfg);
#if defined(NODE_ROLE_LCD)
                            if (lvgl_port_lock_with_timeout(50)) {
                                ui_refresh_labels();
                                lvgl_port_unlock();
                            }
#endif
                        } else {
                            if (seg != 0xFF) {
                                label_len[idx] = seg <= MAX_INPUT_LABEL_LEN ? seg : MAX_INPUT_LABEL_LEN;
                                label_pos[idx] = 0;
                                memset(cfg.input_labels[idx], 0, sizeof(cfg.input_labels[idx]));
                            }
                            size_t avail = (message.data_length_code >= 3) ? (size_t)(message.data_length_code - 3) : 0;
                            size_t to_copy = (avail < (label_len[idx] - label_pos[idx])) ? avail : (label_len[idx] - label_pos[idx]);
                            size_t space_left = (size_t)(MAX_INPUT_LABEL_LEN + 1) - label_pos[idx];
                            if (to_copy > space_left) to_copy = space_left;
                            for (size_t i = 0; i < to_copy; i++) {
                                cfg.input_labels[idx][label_pos[idx] + i] = (char)message.data[3 + i];
                            }
                            label_pos[idx] += to_copy;
                            cfg.input_labels[idx][label_pos[idx] <= MAX_INPUT_LABEL_LEN ? label_pos[idx] : MAX_INPUT_LABEL_LEN] = '\0';
                            if (label_pos[idx] >= label_len[idx]) {
                                config_save(&cfg);
                                Serial.printf("CONFIG: label[%u] = '%s'\n", (unsigned)idx, cfg.input_labels[idx]);
#if defined(NODE_ROLE_LCD)
                                if (lvgl_port_lock_with_timeout(50)) {
                                    ui_refresh_labels();
                                    lvgl_port_unlock();
                                }
#endif
                            }
                        }
                    }
                }
                // Continue to update display with this message if desired, or return
            }

            // Update CAN indicator: green dot when connected (poll task sets disconnected after idle)
            s_last_can_rx_ms = millis();
#if defined(NODE_ROLE_LCD)
            ui_set_can_connected(true);
#endif
        }
    }
}

// Dedicated task to poll CAN and input engine so it runs even if Arduino loop() is starved by LVGL
static void can_poll_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(500));  // Let system stabilize after boot
    
    // Track last HEARTBEAT send time for periodic announcements
    static unsigned long last_heartbeat_ms = 0;
    // Find-me blink: last toggle time (0 = not started this run) and current state
    static unsigned long find_me_last_toggle_ms = 0;
    static bool find_me_blink_high = false;
    
    const unsigned long FIND_ME_BLINK_INTERVAL_MS = 150;
    
    // Determine node type (same logic as setup())
    uint8_t node_type;
#if defined(NODE_ROLE_LCD)
    node_type = NODE_TYPE_LCD;
#elif defined(NODE_ROLE_MIN)
    node_type = NODE_TYPE_MECHANICAL;
#else
    node_type = NODE_TYPE_LCD;  // default
#endif
    
    for (;;) {
        handle_can_messages();
        input_engine_update();

#if defined(NODE_ROLE_MIN)
        // Mechanical node: read GPIOs and feed levels into input engine (button-to-GND = active when LOW)
        for (uint8_t i = 0; i < cfg.input_count && i < MAX_INPUTS_PER_NODE; i++) {
            if (cfg.input_gpio[i] != 0xFF) {
                pinMode(cfg.input_gpio[i], INPUT_PULLUP);
                bool active = (digitalRead(cfg.input_gpio[i]) == LOW);
                input_engine_process_level(cfg.inputs[i].input_id, active);
            }
        }
#endif

        // Send periodic HEARTBEAT every 15 seconds
        unsigned long now_ms = millis();
        if (now_ms - last_heartbeat_ms >= 15000) {
            can_send_node_announce(cfg.node_id, node_type, cfg.input_count);
            last_heartbeat_ms = now_ms;
        }
        
        // Find-me: turn off after duration, or blink for non-zero I/O index (ESP32-S3 GPIO)
        if (find_me_until && now_ms >= find_me_until) {
            if (find_me_output_index <= FIND_ME_GPIO_MAX)
                digitalWrite(find_me_output_index, LOW);
            find_me_until = 0;
            find_me_last_toggle_ms = 0;
        } else if (find_me_until && find_me_output_index != 0 && find_me_output_index <= FIND_ME_GPIO_MAX) {
            if (find_me_last_toggle_ms == 0 || (now_ms - find_me_last_toggle_ms) >= FIND_ME_BLINK_INTERVAL_MS) {
                find_me_blink_high = !find_me_blink_high;
                digitalWrite(find_me_output_index, find_me_blink_high ? HIGH : LOW);
                find_me_last_toggle_ms = now_ms;
            }
        }

#if defined(NODE_ROLE_LCD)
        // CAN indicator dot: green when connected, red flashing when disconnected
        bool connected = (now_ms - s_last_can_rx_ms <= CAN_STATUS_IDLE_MS);
        ui_set_can_connected(connected);
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void loop() {
    // CAN polling and input engine run in can_poll_task
    delay(100);
}
