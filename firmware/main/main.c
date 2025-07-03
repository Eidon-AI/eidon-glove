/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_mac.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs_adv.h"
#else
#include "esp_bt_defs.h"
#if CONFIG_BT_BLE_ENABLED
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#endif
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#if CONFIG_BT_SDP_COMMON_ENABLED
#include "esp_sdp_api.h"
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif

#include "esp_hidd.h"
#include "esp_hid_gap.h"
#include "imu.h"
#include "descriptors/tracker.h"
#include "descriptors/glove.h"
#include "esp_efuse.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "led.h"
#include "button.h"
#include "storage.h"

static const char *TAG = "MAIN";

#define INPUT_REPORT_ID 1
#define FEATURE_REPORT_ID 3

// Function declarations

static void discover_feature_report_handle(void);
esp_err_t get_feature_report(uint8_t report_id, uint8_t *data, size_t *length);

// Firmware version information
#define FIRMWARE_VERSION_MAJOR   1
#define FIRMWARE_VERSION_MINOR   0
#define FIRMWARE_VERSION_PATCH   0
#define FIRMWARE_VERSION_STRING  "1.0.0"

// Device Information Service UUIDs
#define DIS_SERVICE_UUID    0x180A
#define DIS_CHAR_MANUFACTURER_NAME_UUID  0x2A29
#define DIS_CHAR_MODEL_NUMBER_UUID       0x2A24
#define DIS_CHAR_SERIAL_NUMBER_UUID      0x2A25
#define DIS_CHAR_FIRMWARE_REVISION_UUID  0x2A26

// DIS attribute values - DISABLED FOR NOW
/*
static const char dis_manufacturer[] = "Eidon AI";
#if CONFIG_HID_DEVICE_ROLE == 1
static const char dis_model[] = "Eidon Tracker";
#elif CONFIG_HID_DEVICE_ROLE == 2
static const char dis_model[] = "Eidon Glove";
#else
static const char dis_model[] = "Eidon Tracker";  // Default
#endif
static char dis_serial[32] = ""; // Will be set dynamically
static const char dis_firmware[] = FIRMWARE_VERSION_STRING;

// DIS handles
static uint16_t dis_service_handle = 0;
static uint16_t dis_char_handle_manufacturer = 0;
static uint16_t dis_char_handle_model = 0;
static uint16_t dis_char_handle_serial = 0;
static uint16_t dis_char_handle_firmware = 0;
*/

// DIS GATT server event handler - DISABLED FOR NOW
/*
static void dis_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_err_t ret;
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        // Create DIS service
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = DIS_SERVICE_UUID}
                }
            }
        };
        ret = esp_ble_gatts_create_service(gatts_if, &service_id, 8);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create DIS service: %s", esp_err_to_name(ret));
        }
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        dis_service_handle = param->create.service_handle;
        // Add Manufacturer Name
        esp_bt_uuid_t char_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = DIS_CHAR_MANUFACTURER_NAME_UUID}};
        esp_attr_value_t attr_val = {
            .attr_max_len = sizeof(dis_manufacturer),
            .attr_len = strlen(dis_manufacturer),
            .attr_value = (uint8_t*)dis_manufacturer
        };
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS manufacturer char: %s", esp_err_to_name(ret));
        // Add Model Number
        char_uuid.uuid.uuid16 = DIS_CHAR_MODEL_NUMBER_UUID;
        attr_val.attr_max_len = sizeof(dis_model);
        attr_val.attr_len = strlen(dis_model);
        attr_val.attr_value = (uint8_t*)dis_model;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS model char: %s", esp_err_to_name(ret));
        // Add Serial Number (set after creation)
        char_uuid.uuid.uuid16 = DIS_CHAR_SERIAL_NUMBER_UUID;
        attr_val.attr_max_len = sizeof(dis_serial);
        attr_val.attr_len = strlen(dis_serial);
        attr_val.attr_value = (uint8_t*)dis_serial;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS serial char: %s", esp_err_to_name(ret));
        // Add Firmware Revision
        char_uuid.uuid.uuid16 = DIS_CHAR_FIRMWARE_REVISION_UUID;
        attr_val.attr_max_len = sizeof(dis_firmware);
        attr_val.attr_len = strlen(dis_firmware);
        attr_val.attr_value = (uint8_t*)dis_firmware;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS firmware char: %s", esp_err_to_name(ret));
        // Start service
        esp_ble_gatts_start_service(dis_service_handle);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        // Save handles for later updates
        uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
        if (uuid == DIS_CHAR_MANUFACTURER_NAME_UUID) dis_char_handle_manufacturer = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_MODEL_NUMBER_UUID) dis_char_handle_model = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_SERIAL_NUMBER_UUID) dis_char_handle_serial = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_FIRMWARE_REVISION_UUID) dis_char_handle_firmware = param->add_char.attr_handle;
        break;
    }
    default:
        break;
    }
}
*/



// SHTP constants
#define SHTP_MAX_TRANSFER_SIZE 300

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

// Global feature report data for Report ID 3 (3 bytes: R, G, B)
static uint8_t current_feature_report[3] = {PREFERENCES_DEFAULT_SHELL_COLOR_R, PREFERENCES_DEFAULT_SHELL_COLOR_G, PREFERENCES_DEFAULT_SHELL_COLOR_B};

// Global body position for input reports (stored in first 4 button bits)
static uint8_t current_body_position = 0x00;  // Default position (0-3)

// Feature report GATT attribute handle (set during device initialization)
static uint16_t feature_report_handle = 0;



#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
static local_param_t s_ble_hid_param = {0};




static esp_hid_raw_report_map_t ble_report_maps[] = {
#if CONFIG_HID_DEVICE_ROLE == 1
    /* Eidon Tracker */
    {
        .data = tracker_report_map,
        .len = sizeof(tracker_report_map)
    }
#elif CONFIG_HID_DEVICE_ROLE == 2
    /* Eidon Glove */
    {
        .data = glove_report_map,
        .len = sizeof(glove_report_map)
    }
#else
    /* Default to Tracker for bluedroid */
    {
        .data = tracker_report_map,
        .len = sizeof(tracker_report_map)
    }
#endif
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id          = 0xE1D0,  // Eidon AI vendor ID
#if CONFIG_HID_DEVICE_ROLE == 1
    .product_id         = 0x0002,  // Eidon Tracker product ID
#elif CONFIG_HID_DEVICE_ROLE == 2
    .product_id         = 0x0001,  // Eidon Glove product ID
#else
    .product_id         = 0x0002,  // Default to Tracker
#endif
    .version            = 0x0100,
#if CONFIG_HID_DEVICE_ROLE == 1
    .device_name        = "Eidon Tracker",
#elif CONFIG_HID_DEVICE_ROLE == 2
    .device_name        = "Eidon Glove",
#else
    .device_name        = "Eidon Device",  // Default
#endif
    .manufacturer_name  = "Eidon AI",
    .serial_number      = NULL,  // Will be set dynamically
    .report_maps        = ble_report_maps,
    .report_maps_len    = 1
};



#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_HID_DEVICE_ROLE == 1

// Sensor task to send quaternion data via HID
void ble_hid_sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");
    
    // Wait for BNO085 to initialize
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    while (1) {
        if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev)) {
            ESP_LOGI(TAG, "HID device connected and ready for sensor data");
            break;
        } else {
            ESP_LOGI(TAG, "Waiting for HID connection...");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // The actual sensor data sending is handled by the bno085_task
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif  // #if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_HID_DEVICE_ROLE == 1

// HID report structure for sensor data (exactly 9 bytes)
#if CONFIG_HID_DEVICE_ROLE == 1
typedef tracker_hid_report_t sensor_hid_report_t;
#elif CONFIG_HID_DEVICE_ROLE == 2
typedef glove_hid_report_t sensor_hid_report_t;
#else
typedef tracker_hid_report_t sensor_hid_report_t;  // Default to tracker
#endif

// BNO085 task to read sensor data
static void bno085_task(void *pvParameters)
{
    ESP_LOGI(TAG, "BNO085 task started");
    
    // Initialize BNO085 with Adafruit-style wrapper
    esp_err_t ret = adafruit_bno08x_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BNO085");
        vTaskDelete(NULL);
        return;
    }
    
    // Enable game rotation vector at 50Hz (20ms = 10000us)
    ret = adafruit_bno08x_enable_game_rotation_vector(10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector");
        adafruit_bno08x_deinit();
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "BNO085 initialized successfully with Adafruit-style wrapper, starting sensor loop");
    
    sensor_hid_report_t report = {0};
    // report.report_id = 1;
    report.buttons = 0;  // No buttons pressed
    
    while (1) {
        // Service the sensor (handles SHTP communication)
        ret = adafruit_bno08x_service();
        if (ret == ESP_OK && adafruit_bno08x_has_new_quaternion()) {
            sh2_RotationVector_t quat;
            ret = adafruit_bno08x_get_quaternion(&quat);
            if (ret == ESP_OK) {
                // Apply coordinate system transformation to fix yaw/pitch swapping
                adafruit_bno08x_transform_coordinate_system(&quat);
                
                // Convert float quaternion to uint16_t for HID report
                // Scale from [-1, 1] to [0, 65535]
                report.quaternion[0] = (uint16_t)((quat.i + 1.0f) * 32767.5f);     // x
                report.quaternion[1] = (uint16_t)((quat.j + 1.0f) * 32767.5f);     // y
                report.quaternion[2] = (uint16_t)((quat.k + 1.0f) * 32767.5f);     // z
                report.quaternion[3] = (uint16_t)((quat.real + 1.0f) * 32767.5f);  // w
                
                // Set body position in first 4 button bits (buttons 1-4)
                // Clear the first 4 bits and set them to the current position
                report.buttons = (report.buttons & 0xF0) | (current_body_position & 0x0F);

                // Send HID report if connected (try both BOOT and REPORT modes)
                if (s_ble_hid_param.hid_dev) {
                    // Set LED to transmitting state (bright)
                    led_set_state(LED_STATE_TRANSMITTING);
                    
                    // Try to send in current mode, if it fails, try the other mode
                    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, (uint8_t*)&report, sizeof(report));
                    if (ret != ESP_OK) {
                        // If failed, try switching protocol mode and retry
                        ESP_LOGW(TAG, "HID report failed, trying alternative protocol mode");
                        // Try the opposite mode
                        ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, (uint8_t*)&report, sizeof(report));
                    }
                    
                    // ESP_LOGI(TAG, "HID Sensor Report sent: Quat: w=%.3f, x=%.3f, y=%.3f, z=%.3f mode=%d", 
                    //          quat.real, quat.i, quat.j, quat.k, s_ble_hid_param.protocol_mode);
                } else {
                    ESP_LOGW(TAG, "HID device not connected");
                    // Set LED back to paired state if not connected
                    led_set_state(LED_STATE_PAIRED);
                }
            }
        }
        
        // Small delay to prevent hogging CPU and reduce protocol mode pressure
        vTaskDelay(pdMS_TO_TICKS(20)); // Increased from 5ms to 20ms to reduce frequency
    }
}

void ble_hid_task_start_up(void)
{
    ESP_LOGI(TAG, "ble_hid_task_start_up called");
    
    if (s_ble_hid_param.task_hdl) {
        // Task already exists
        ESP_LOGI(TAG, "Task already exists");
        return;
    }
#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_HID_DEVICE_ROLE == 1
    /* Executed for bluedroid and nimble sensor mode */
    ESP_LOGI(TAG, "Creating sensor task");
    xTaskCreate(ble_hid_sensor_task, "ble_hid_sensor_task", 4 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);

#elif CONFIG_HID_DEVICE_ROLE == 2
    /* Nimble Specific */
    xTaskCreate(ble_hid_demo_task_kbd, "ble_hid_demo_task_kbd", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
#elif CONFIG_HID_DEVICE_ROLE == 3
    /* Nimble Specific */
    xTaskCreate(ble_hid_demo_task_mouse, "ble_hid_demo_task_mouse", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
#endif
}

void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl) {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    static const char *TAG = "BLE_HID";

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGI(TAG, "START");
        esp_hid_ble_gap_adv_start();
        // Set LED to strobe state (device on, not paired)
        led_set_state(LED_STATE_STROBE);
        
        // Find and store the feature report handle for proactive updates
        if (s_ble_hid_param.hid_dev) {
            ESP_LOGI(TAG, "*** Searching for feature report handle ***");
            // We'll need to get the handle from the ESP-IDF HID stack
            // For now, we'll set it to 0 and update it when we get the first feature event
            feature_report_handle = 0;
            ESP_LOGI(TAG, "Feature report handle initialized to 0 (will be set on first feature event)");
        }
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        ESP_LOGI(TAG, "CONNECT");
        s_ble_hid_param.protocol_mode = 0; // Initialize to BOOT mode, will be updated by protocol mode event
        ESP_LOGI(TAG, "HID device connected, protocol mode initialized to BOOT (0)");
        // Set LED to paired state (dim)
        led_set_state(LED_STATE_PAIRED);
        ble_hid_task_start_up();
        
        // Proactively update GATT attribute if handle is already known
        if (feature_report_handle != 0) {
            esp_err_t update_ret = esp_ble_gatts_set_attr_value(feature_report_handle, sizeof(current_feature_report), current_feature_report);
            if (update_ret == ESP_OK) {
                ESP_LOGI(TAG, "*** Proactively updated GATT attribute on connect ***");
                ESP_LOGI(TAG, "GATT attribute handle: %d, data: [0x%02X, 0x%02X, 0x%02X]", 
                         feature_report_handle, current_feature_report[0], current_feature_report[1], current_feature_report[2]);
            } else {
                ESP_LOGW(TAG, "Failed to update GATT attribute on connect: %s (0x%x)", esp_err_to_name(update_ret), update_ret);
            }
        } else {
            // Handle not known yet, set it to the known value from logs
            feature_report_handle = 71;
            ESP_LOGI(TAG, "*** Setting feature report handle to 71 on connect ***");
            
            // Try to update the GATT attribute immediately
            esp_err_t update_ret = esp_ble_gatts_set_attr_value(feature_report_handle, sizeof(current_feature_report), current_feature_report);
            if (update_ret == ESP_OK) {
                ESP_LOGI(TAG, "*** Proactively updated GATT attribute on connect (handle 71) ***");
                ESP_LOGI(TAG, "GATT attribute handle: %d, data: [0x%02X, 0x%02X, 0x%02X]", 
                         feature_report_handle, current_feature_report[0], current_feature_report[1], current_feature_report[2]);
            } else {
                ESP_LOGW(TAG, "Failed to update GATT attribute on connect (handle 71): %s (0x%x)", esp_err_to_name(update_ret), update_ret);
            }
        }
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index, param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        s_ble_hid_param.protocol_mode = param->protocol_mode.protocol_mode;
        ESP_LOGI(TAG, "Protocol mode updated to: %d", s_ble_hid_param.protocol_mode);
        
        // For NimBLE, we need to ensure the protocol mode is properly set
        // The error suggests the host is trying to write protocol mode but failing
        // Let's add a small delay to ensure the HID device is ready
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Log the current state for debugging
        ESP_LOGI(TAG, "Current protocol mode state: %d", s_ble_hid_param.protocol_mode);
        break;
    }
    case ESP_HIDD_CONTROL_EVENT: {
        ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index, param->control.control ? "EXIT_" : "");
        if (param->control.control)
        {
            // exit suspend
            // ble_hid_task_start_up(); // Already started on connect
        } else {
            // suspend
            ble_hid_task_shut_down();
        }
    break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index, esp_hid_usage_str(param->output.usage), param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        
        // Handle vendor-defined output report for IMU reset (Report ID 2)
        if (param->output.report_id == 2) { // Separate report ID for output
            if (param->output.length >= 1) {
                uint8_t command = param->output.data[0];
                ESP_LOGI(TAG, "Received vendor command: 0x%02X", command);
                
                // Command 0x01 triggers IMU reset
                if (command == 0x01) {
                    ESP_LOGI(TAG, "Triggering IMU reset via HID output command");
                    // Trigger LED reset sequence
                    led_trigger_reset_sequence();
                    // Reset IMU
                    adafruit_bno08x_reset();
                }
            }
        }
        break;
    }
    case ESP_HIDD_FEATURE_EVENT: {
        ESP_LOGI(TAG, "*** FEATURE EVENT RECEIVED ***");
        ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:", param->feature.map_index, esp_hid_usage_str(param->feature.usage), param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        
        // Discover feature report handle on first feature event
        discover_feature_report_handle();
        
        // Handle vendor-defined feature report for device shell color (Report ID 3)
        if (param->feature.report_id == 3) {
            if (param->feature.length == 0) {
                // Host is requesting to read the feature report (receiveFeatureReport)
                ESP_LOGI(TAG, "*** HOST REQUESTING TO READ FEATURE REPORT (receiveFeatureReport) ***");
                
                // Get current shell color from storage
                shell_color_t color;
                esp_err_t ret = storage_get_shell_color(&color);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Current color: R=0x%02X, G=0x%02X, B=0x%02X", 
                             color.r, color.g, color.b);
                } else {
                    ESP_LOGW(TAG, "Failed to get current shell color: %s", esp_err_to_name(ret));
                }
                
                // Note: The response is now handled by the GATT read handler
                ESP_LOGI(TAG, "Feature report read request logged - response handled by GATT handler");
            } else if (param->feature.length == 3) {
                // Host is setting the color (sendFeatureReport)
                uint8_t r = param->feature.data[0];
                uint8_t g = param->feature.data[1];
                uint8_t b = param->feature.data[2];
                ESP_LOGI(TAG, "*** HOST SETTING COLOR VIA FEATURE REPORT (sendFeatureReport) ***");
                ESP_LOGI(TAG, "Setting device shell color: R=0x%02X, G=0x%02X, B=0x%02X", r, g, b);
                
                // Save color using storage module
                esp_err_t ret = storage_save_shell_color(r, g, b);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to save shell color: %s", esp_err_to_name(ret));
                } else {
                    // Update feature report data
                    current_feature_report[0] = r;
                    current_feature_report[1] = g;
                    current_feature_report[2] = b;
                    
#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
                    // Proactively update the GATT attribute value so the next read returns the correct value
                    if (feature_report_handle != 0 && s_ble_hid_param.hid_dev) {
                        esp_err_t update_ret = esp_ble_gatts_set_attr_value(feature_report_handle, sizeof(current_feature_report), current_feature_report);
                        if (update_ret == ESP_OK) {
                            ESP_LOGI(TAG, "*** Proactively updated GATT attribute for feature report ***");
                            ESP_LOGI(TAG, "GATT attribute handle: %d, data: [0x%02X, 0x%02X, 0x%02X]", 
                                     feature_report_handle, current_feature_report[0], current_feature_report[1], current_feature_report[2]);
                        } else {
                            ESP_LOGW(TAG, "Failed to update GATT attribute: %s (0x%x)", esp_err_to_name(update_ret), update_ret);
                        }
                    } else {
                        ESP_LOGW(TAG, "Cannot update GATT attribute - handle: %d, device: %p", feature_report_handle, s_ble_hid_param.hid_dev);
                    }
#endif
                }
            } else {
                ESP_LOGW(TAG, "Unexpected feature report length: %d (expected 0 for read or 3 for write)", param->feature.length);
            }
        }
        break;
    }

    case ESP_HIDD_DISCONNECT_EVENT: {
        ESP_LOGI(TAG, "DISCONNECT: %s", esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        ble_hid_task_shut_down();
        // Set LED back to strobe state (device on, not paired)
        led_set_state(LED_STATE_STROBE);
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGI(TAG, "STOP");
        break;
    }
    default:
        break;
    }
    return;
}
#endif

#if CONFIG_BT_NIMBLE_ENABLED
static void ble_hid_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synced, initializing HID device");
    
    // Add a small delay to ensure NimBLE is fully ready
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize HID device after NimBLE is ready
    esp_err_t ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BLE HID device initialized successfully");
    
    // Add another delay to ensure HID service is fully initialized
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Start advertisement
    ESP_LOGI(TAG, "Starting HID advertisement");
    esp_hid_ble_gap_adv_start();
}

void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    
    /* Initialize the NimBLE host configuration */
    ble_hs_cfg.sync_cb = ble_hid_on_sync;
    
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}
void ble_store_config_init(void);
#endif





// Function to discover and store the feature report handle
static void discover_feature_report_handle(void)
{
    if (feature_report_handle != 0) {
        // Already discovered
        return;
    }
    
    // For now, we'll use a hardcoded handle based on the logs
    // From your logs, we saw "Attribute handle: 71" for the feature report
    // This is not ideal but will work for testing
    feature_report_handle = 71;
    ESP_LOGI(TAG, "*** Feature report handle discovered: %d ***", feature_report_handle);
    
#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
    // Proactively update the GATT attribute with current color data
    if (s_ble_hid_param.hid_dev) {
        esp_err_t update_ret = esp_ble_gatts_set_attr_value(feature_report_handle, sizeof(current_feature_report), current_feature_report);
        if (update_ret == ESP_OK) {
            ESP_LOGI(TAG, "*** Proactively updated GATT attribute on startup ***");
            ESP_LOGI(TAG, "GATT attribute handle: %d, data: [0x%02X, 0x%02X, 0x%02X]", 
                     feature_report_handle, current_feature_report[0], current_feature_report[1], current_feature_report[2]);
        } else {
            ESP_LOGW(TAG, "Failed to update GATT attribute on startup: %s (0x%x)", esp_err_to_name(update_ret), update_ret);
        }
    }
#endif
}

// Function to get feature report data
esp_err_t get_feature_report(uint8_t report_id, uint8_t *data, size_t *length)
{
    ESP_LOGI(TAG, "*** get_feature_report called ***");
    ESP_LOGI(TAG, "Requested report_id: %d, buffer size: %d", report_id, *length);
    
    if (report_id == 3) {
        // Get current shell color from storage
        shell_color_t color;
        esp_err_t ret = storage_get_shell_color(&color);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get shell color from storage: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Update feature report data with current color
        current_feature_report[0] = color.r;
        current_feature_report[1] = color.g;
        current_feature_report[2] = color.b;
        
        ESP_LOGI(TAG, "Current feature report data: [0x%02X, 0x%02X, 0x%02X]", 
                 current_feature_report[0], current_feature_report[1], current_feature_report[2]);
        ESP_LOGI(TAG, "Current shell color: [0x%02X, 0x%02X, 0x%02X]", 
                 color.r, color.g, color.b);
        
        if (*length >= sizeof(current_feature_report)) {
            memcpy(data, current_feature_report, sizeof(current_feature_report));
            *length = sizeof(current_feature_report);
            ESP_LOGI(TAG, "*** Feature report 3 data copied to buffer ***");
            ESP_LOGI(TAG, "Buffer contents after copy: [0x%02X, 0x%02X, 0x%02X]", 
                     data[0], data[1], data[2]);
            ESP_LOGI(TAG, "Returning length: %d", *length);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Buffer too small for feature report 3 (need %d, got %d)", 
                     sizeof(current_feature_report), *length);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    ESP_LOGW(TAG, "Unknown feature report ID: %d", report_id);
    return ESP_ERR_NOT_FOUND;
}



// Button callback function for IMU reset and position change
static void button_imu_reset_callback(void)
{
    ESP_LOGI(TAG, "Button pressed - triggering IMU reset and position change");
    
    // Change body position (cycle through 0-3 for now)
    current_body_position = (current_body_position + 1) % 4;
    ESP_LOGI(TAG, "Body position changed to: 0x%02X", current_body_position);
    
    // Trigger LED reset sequence
    led_trigger_reset_sequence();
    // Reset IMU
    adafruit_bno08x_reset();
}

// Function to generate unique serial number from MAC address
static void generate_unique_serial_number(char *serial_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // Use WiFi STA MAC address (unique per chip)
    
#if CONFIG_HID_DEVICE_ROLE == 1
    // Format as Eidon Tracker-XXXX where X is hex digit from MAC
    snprintf(serial_buffer, buffer_size, "Eidon Tracker-%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
#elif CONFIG_HID_DEVICE_ROLE == 2
    // Format as Eidon Glove-XXXX where X is hex digit from MAC
    snprintf(serial_buffer, buffer_size, "Eidon Glove-%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
#else
    // Default to Tracker
    snprintf(serial_buffer, buffer_size, "Eidon Device-%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
#endif
}

// Function to generate unique device name with MAC suffix
static void generate_unique_device_name(char *name_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
#if CONFIG_HID_DEVICE_ROLE == 1
    // Format as "Eidon Tracker-XXXX" where XXXX is last 4 hex digits of MAC
    snprintf(name_buffer, buffer_size, "Eidon Tracker-%02X%02X", 
             mac[4], mac[5]);
#elif CONFIG_HID_DEVICE_ROLE == 2
    // Format as "Eidon Glove-XXXX" where XXXX is last 4 hex digits of MAC
    snprintf(name_buffer, buffer_size, "Eidon Glove-%02X%02X", 
             mac[4], mac[5]);
#else
    // Default to Tracker
    snprintf(name_buffer, buffer_size, "Eidon Tracker-%02X%02X", 
             mac[4], mac[5]);
#endif
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main() started");
    
    esp_err_t ret;
#if HID_DEV_MODE == HIDD_IDLE_MODE
    ESP_LOGE(TAG, "Please turn on BT HID device or BLE!");
    return;
#endif
    
    ESP_LOGI(TAG, "Initializing NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    
    // Initialize LEDs
    ESP_LOGI(TAG, "Initializing LEDs...");
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED initialization failed, continuing without LED control");
    }
    
    // Initialize storage module
    ESP_LOGI(TAG, "*** Initializing storage module ***");
    ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Storage initialization failed: %s", esp_err_to_name(ret));
    }
    
    // Get current shell color and update feature report data
    shell_color_t color;
    ret = storage_get_shell_color(&color);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Device shell color loaded from storage ***");
        ESP_LOGI(TAG, "Loaded color: R=0x%02X, G=0x%02X, B=0x%02X", color.r, color.g, color.b);
        
        // Update feature report data
        current_feature_report[0] = color.r;
        current_feature_report[1] = color.g;
        current_feature_report[2] = color.b;
    } else {
        ESP_LOGI(TAG, "*** Using default device shell color on startup ***");
        ESP_LOGI(TAG, "Default color: R=0x%02X, G=0x%02X, B=0x%02X", 
                 current_feature_report[0], current_feature_report[1], current_feature_report[2]);
    }
    
    ESP_LOGI(TAG, "*** Current feature report data after startup ***");
    ESP_LOGI(TAG, "current_feature_report: [0x%02X, 0x%02X, 0x%02X]", 
             current_feature_report[0], current_feature_report[1], current_feature_report[2]);
    
    // Get current body position from storage
    body_position_t position;
    ret = storage_get_body_position(&position);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Body position loaded from storage ***");
        ESP_LOGI(TAG, "Loaded position: 0x%02X", position.position);
        current_body_position = position.position;
    } else {
        ESP_LOGI(TAG, "*** Using default body position on startup ***");
        ESP_LOGI(TAG, "Default position: 0x%02X", current_body_position);
    }
    
    // Set initial LED state to STROBE for testing (will be set properly when BLE starts)
    led_set_state(LED_STATE_STROBE);
    ESP_LOGI(TAG, "Set initial LED state to STROBE for testing");

    ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_ERROR_CHECK( ret );

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
    // Generate unique device name with MAC suffix
    static char unique_device_name[32];
    generate_unique_device_name(unique_device_name, sizeof(unique_device_name));
    ble_hid_config.device_name = unique_device_name;
    ESP_LOGI(TAG, "Generated unique device name: %s", unique_device_name);
    
#if CONFIG_HID_DEVICE_ROLE == 2
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, ble_hid_config.device_name);
#elif CONFIG_HID_DEVICE_ROLE == 3
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE, ble_hid_config.device_name);
#else
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, ble_hid_config.device_name);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hid_ble_gap_adv_init failed: %s", esp_err_to_name(ret));
        return;
    }
#if CONFIG_BT_BLE_ENABLED
    if ((ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler)) != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
        return;
    }
#endif
    // Generate unique serial number from MAC address
    static char unique_serial[32];
    generate_unique_serial_number(unique_serial, sizeof(unique_serial));
    ble_hid_config.serial_number = unique_serial;
    ESP_LOGI(TAG, "Generated unique serial number: %s", unique_serial);
    
    // Set DIS serial number for use in event handler - DISABLED FOR NOW
    /*
    strncpy(dis_serial, unique_serial, sizeof(dis_serial)-1);
    dis_serial[sizeof(dis_serial)-1] = '\0';
    */

    // Register the DIS GATT server - DISABLED FOR NOW
    /*
    ret = esp_ble_gatts_register_callback(dis_gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DIS GATT server: %s", esp_err_to_name(ret));
    }
    ret = esp_ble_gatts_app_register(0xA0A0); // Arbitrary app ID for DIS
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to app register DIS: %s", esp_err_to_name(ret));
    }
    */
    
    // For NimBLE, HID device initialization and advertisement will be started in the sync callback
    // For Bluedroid, we need to do it here
#if !CONFIG_BT_NIMBLE_ENABLED
    ESP_LOGI(TAG, "setting ble device");
    ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BLE HID device initialized successfully");
    esp_hid_ble_gap_adv_start();
#endif
    
    // Initialize and start button functionality
    ESP_LOGI(TAG, "Initializing button");
    ret = button_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Button initialization failed, continuing without button control");
    } else {
        // Set callback for IMU reset
        button_set_callback(button_imu_reset_callback);
        
        // Start button task
        ret = button_task_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Button task start failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Button functionality started successfully");
        }
    }
    
    // Start BNO085 test task
    ESP_LOGI(TAG, "Starting BNO085 test task");
    xTaskCreate(bno085_task, "bno085_task", 4096, NULL, 5, NULL);
#endif

#if CONFIG_BT_HID_DEVICE_ENABLED
    ESP_LOGI(TAG, "setting device name");
    esp_bt_gap_set_device_name(bt_hid_config.device_name);
    ESP_LOGI(TAG, "setting cod major, peripheral");
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_POINTING;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "setting bt device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&bt_hid_config, ESP_HID_TRANSPORT_BT, bt_hidd_event_callback, &s_bt_hid_param.hid_dev));
#if CONFIG_BT_SDP_COMMON_ENABLED
    ESP_ERROR_CHECK(esp_sdp_register_callback(esp_sdp_cb));
    ESP_ERROR_CHECK(esp_sdp_init());
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif /* CONFIG_BT_HID_DEVICE_ENABLED */
#if CONFIG_BT_NIMBLE_ENABLED
    /* XXX Need to have template for store */
    ble_store_config_init();

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    /* Starting nimble task after gatts is initialized*/
    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
        return;
    }
    
    ESP_LOGI(TAG, "NimBLE enabled, waiting for host sync callback...");
#endif
}
