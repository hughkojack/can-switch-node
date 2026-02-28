#include <Arduino.h>
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

#include "ESP32TWAISingleton.hpp" // This provides the global 'CAN' object

#include <lvgl.h>
#include <ESP_Panel_Library.h>
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

/**
/* To use the built-in examples and demos of LVGL uncomment the includes below respectively.
 * You also need to copy `lvgl/examples` to `lvgl/src/examples`. Similarly for the demos `lvgl/demos` to `lvgl/src/demos`.
 */
// #include <demos/lv_demos.h>
// #include <examples/lv_examples.h>

/* LVGL porting configurations */
#define LVGL_TICK_PERIOD_MS     (2)
#define LVGL_TASK_MAX_DELAY_MS  (500)
#define LVGL_TASK_MIN_DELAY_MS  (1)
#define LVGL_TASK_STACK_SIZE    (4 * 1024)
#define LVGL_TASK_PRIORITY      (2)
#define LVGL_BUF_SIZE           (ESP_PANEL_LCD_H_RES * 20)

lv_obj_t * ui_CanStatusLabel = NULL; // Define the label pointer

ESP_Panel *panel = NULL;
SemaphoreHandle_t lvgl_mux = NULL;                  // LVGL mutex


//---- helpers to send commands to the LED controller hub ----

static node_config_t cfg;

// For find-me: expander set in setup so we can drive output by index (e.g. 0 = LCD_BL)
static ESP_IOExpander* g_expander = nullptr;
static unsigned long find_me_until = 0;   // millis() when to turn find-me output off
static uint8_t find_me_output_index = 0;  // cached from NVS
static input_timing_t s_timing;            // timing from NVS, passed to input_engine

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
static bool can_send_set_brightness(uint8_t output, uint8_t brightness_0_100, uint16_t fade_ms) {
  twai_message_t tx{};
  tx.identifier = createCanId(LIGHTING_COMMAND, cfg.node_id);
  tx.flags = 0;                 // standard 11-bit frame
  tx.data_length_code = 5;
  tx.data[0] = SET_BRIGHTNESS;
  tx.data[1] = output;          // 1..160
  tx.data[2] = brightness_0_100; // 0..100
  tx.data[3] = (uint8_t)(fade_ms & 0xFF);
  tx.data[4] = (uint8_t)(fade_ms >> 8);

  return twai_transmit(&tx, pdMS_TO_TICKS(50)) == ESP_OK;
}

// state: 0=OFF, 1=ON, 2=TOGGLE
static bool can_send_set_state(uint8_t output, uint8_t state, uint16_t fade_ms) {
    uint8_t data[5];
    data[0] = SET_STATE;
    data[1] = output;
    data[2] = state;
    data[3] = (uint8_t)(fade_ms & 0xFF);
    data[4] = (uint8_t)(fade_ms >> 8);

    uint32_t can_id = createCanId(LIGHTING_COMMAND, cfg.node_id);

    // 3. Use the library's write function via the Singleton
    // Parameters: FrameType, Identifier, Data Length, Data Buffer
    esp_err_t result = CAN.write(
        can::FrameType::STD_FRAME, 
        can_id, 
        5, 
        data

    );
    return (result == ESP_OK);
}  


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


void setup()
{
    Serial.begin(115200); /* prepare for possible serial debug */
    while (!Serial && millis() < 5000) { delay(10); }  // wait a bit for monitor
    Serial.println("CDC alive");
    
//    node_config_t cfg;
    config_load(&cfg);
    
    Serial.printf("CFG node=%u count=%u\n", cfg.node_id, cfg.input_count);
    for (int i=0; i<cfg.input_count; i++) {
    Serial.printf("CFG[%d] id=%u mode=%u\n", i, cfg.inputs[i].input_id, cfg.inputs[i].mode);
    }

    String LVGL_Arduino = "Hello LVGL! ";
    LVGL_Arduino += String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();

    Serial.println(LVGL_Arduino);
    Serial.println("I am ESP32_Display_Panel");

    panel = new ESP_Panel();

    /* Initialize LVGL core */
    lv_init();

    /* Initialize LVGL buffers */
    static lv_disp_draw_buf_t draw_buf;
    /* Using double buffers is more faster than single buffer */
    /* Using internal SRAM is more fast than PSRAM (Note: Memory allocated using `malloc` may be located in PSRAM.) */
    uint8_t *buf = (uint8_t *)heap_caps_calloc(1, LVGL_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL);
    assert(buf);
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, LVGL_BUF_SIZE);

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
    delay(500); // Short delay to ensure the transceiver is ready

    CAN.begin(CAN_GPIO_RX, CAN_GPIO_TX, can::Baudrate::BAUD_500KBPS);

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
            if (msg_type == CAN_MSG_NODE_CONFIG && target_id == cfg.node_id && message.data_length_code >= 1) {
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
                    if (idx < 16) {
                        cfg.inputs[idx].input_id = message.data[2];
                        cfg.inputs[idx].mode = (input_mode_t)(message.data[3] & 1);
                        config_save(&cfg);
                        config_get_timing(&s_timing);
                        input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                        Serial.printf("CONFIG: input[%u] -> id=%u mode=%u\n", (unsigned)idx, (unsigned)cfg.inputs[idx].input_id, (unsigned)cfg.inputs[idx].mode);
                    }
                } else if (cmd == CMD_SET_INPUT_COUNT && message.data_length_code >= 2) {
                    uint8_t n = message.data[1];
#if defined(NODE_ROLE_MIN)
                    if (n >= 1 && n <= 6)
#else
                    if (n >= 1 && n <= 16)
#endif
                    {
                        cfg.input_count = n;
                        config_save(&cfg);
                        config_get_timing(&s_timing);
                        input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                        Serial.printf("CONFIG: input_count set to %u\n", (unsigned)cfg.input_count);
                    }
                } else if (cmd == CMD_SET_TIMING && message.data_length_code >= 9) {
                    s_timing.click_max_ms       = (uint16_t)message.data[1] | ((uint16_t)message.data[2] << 8);
                    s_timing.double_click_gap_ms = (uint16_t)message.data[3] | ((uint16_t)message.data[4] << 8);
                    s_timing.hold_min_ms        = (uint16_t)message.data[5] | ((uint16_t)message.data[6] << 8);
                    s_timing.long_hold_min_ms   = (uint16_t)message.data[7] | ((uint16_t)message.data[8] << 8);
                    config_set_timing(&s_timing);
                    input_engine_init(cfg.node_id, cfg.inputs, cfg.input_count, &s_timing);
                    Serial.printf("CONFIG: timing updated\n");
                } else if (cmd == CMD_FIND_ME) {
                    uint8_t duration_sec = (message.data_length_code >= 2) ? message.data[1] : 5;
                    if (duration_sec == 0) duration_sec = 5;
                    config_get_find_me_output(&find_me_output_index);
                    if (g_expander) {
                        // Board-specific: index 0 = LCD_BL on expander
                        if (find_me_output_index == 0)
                            g_expander->digitalWrite(LCD_BL, HIGH);
                    }
                    find_me_until = millis() + (unsigned long)duration_sec * 1000;
                    Serial.printf("CONFIG: find-me on for %u s (output_index=%u)\n", (unsigned)duration_sec, (unsigned)find_me_output_index);
                } else if (cmd == CMD_SET_FIND_ME_OUTPUT && message.data_length_code >= 2) {
                    config_set_find_me_output(message.data[1]);
                    config_get_find_me_output(&find_me_output_index);
                    Serial.printf("CONFIG: find-me output index set to %u\n", (unsigned)find_me_output_index);
                }
                // Continue to update display with this message if desired, or return
            }

            // Create a buffer for the display string
            char buf[128];
            
            // Format the ID and the first 2 bytes of data as an example
            // Adjust the %02X parts based on how much data you want to see
            snprintf(buf, sizeof(buf), "ID: 0x%03X Data: %02X %02X %02X", 
                     (unsigned int)message.identifier, 
                     message.data[0], 
                     message.data[1],
                     message.data[2]);

            
            // Thread-safe UI update (timeout to avoid blocking forever and triggering watchdog)
            if (lvgl_port_lock_with_timeout(50)) {
                if (ui_CanStatusLabel != NULL) {
                    lv_label_set_text(ui_CanStatusLabel, buf);
                }
                lvgl_port_unlock();
            }
        }
    }
}

// Dedicated task to poll CAN and input engine so it runs even if Arduino loop() is starved by LVGL
static void can_poll_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(500));  // Let system stabilize after boot
    for (;;) {
        handle_can_messages();
        input_engine_update();
        // Turn off find-me output after duration
        if (find_me_until && millis() >= find_me_until) {
            find_me_until = 0;
            if (g_expander)
                g_expander->digitalWrite(LCD_BL, LOW);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void loop() {
    // CAN polling and input engine run in can_poll_task
    delay(100);
}
