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
#include "config.h"

static const char *TAG = "MAIN";

#define INPUT_REPORT_ID 1
// #define OUTPUT_REPORT_ID 2
// #define FEATURE_REPORT_ID 3

// Function declarations
static void discover_feature_report_handle(void);
esp_err_t get_feature_report(uint8_t report_id, uint8_t *data, size_t *length);

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

// Global feature report data for Report ID 3 (3 bytes: R, G, B)
static uint8_t current_feature_report[3] = {PREFERENCES_DEFAULT_SHELL_COLOR_R, PREFERENCES_DEFAULT_SHELL_COLOR_G, PREFERENCES_DEFAULT_SHELL_COLOR_B};

// Global state variables for input reports
static uint8_t current_body_position = 0x00;  // Default position (0-15)

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
    .product_id         = 0x0000,  // Default to zero
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

// HID report structure for sensor data (exactly 9 bytes)
#if CONFIG_HID_DEVICE_ROLE == 1
typedef tracker_hid_report_t sensor_hid_report_t;
#elif CONFIG_HID_DEVICE_ROLE == 2
typedef glove_hid_report_t sensor_hid_report_t;
#else
typedef tracker_hid_report_t sensor_hid_report_t;  // Default to tracker
#endif

// Global sensor data state
static sensor_hid_report_t current_sensor_report = {0};
static sensor_hid_report_t last_sent_report = {0};
static bool last_sent_button_state = false;

// High-speed HID reporting task
static void hid_reporting_task(void *pvParameters)
{
    ESP_LOGI(TAG, "HID reporting task started at %dHz (delay: %dms)", HID_REPORT_FREQ_HZ, HID_REPORT_DELAY_MS);

    // High-speed reporting loop
    while (1) {
        if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev)) {
            // Get current button state
            bool current_button = button_is_pressed();
            // Create report with current state
            sensor_hid_report_t report = current_sensor_report;
            // Set body position in first 4 button bits
            report.buttons = (report.buttons & 0xF0) | (current_body_position & 0x0F);
            // Set button press state in bit 4
            if (current_button) {
                report.buttons |= 0x10;  // Set bit 4
            }
            // Check if anything has changed
            bool button_changed = (current_button != last_sent_button_state);
            bool position_changed = ((report.buttons & 0x0F) != (last_sent_report.buttons & 0x0F));
            
            // Check if quaternion changed significantly (more than 1 LSB in Q14 format)
            bool quaternion_changed = false;
            for (int i = 0; i < 4; i++) {
                int16_t diff = abs(report.quaternion[i] - last_sent_report.quaternion[i]);
                if (diff > 1) {  // More than 1 LSB change in Q14 format
                    quaternion_changed = true;
                    break;
                }
            }
            
            bool state_changed = button_changed || position_changed || quaternion_changed;
            // Send HID report if state changed or periodically (every 10th iteration = 5Hz minimum)
            static int report_counter = 0;
            if (state_changed || (++report_counter % 10) == 0) {
                esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, (uint8_t*)&report, sizeof(report));
                if (ret == ESP_OK) {
                    // Update last sent values
                    last_sent_report = report;
                    last_sent_button_state = current_button;
                    
                    if (state_changed) {
                        // Set LED to transmitting state only when state actually changed
                        led_set_state(LED_STATE_TRANSMITTING);
                        ESP_LOGI(TAG, "HID input report (change) - btn: %d, pos: 0x%02X, quat: [%.3f, %.3f, %.3f, %.3f]", 
                                current_button ? 1 : 0, current_body_position,
                                report.quaternion[0]/32767.5f-1.0f, report.quaternion[1]/32767.5f-1.0f, report.quaternion[2]/32767.5f-1.0f, report.quaternion[3]/32767.5f-1.0f);
                    }
                } else {
                    ESP_LOGW(TAG, "HID report failed: %s", esp_err_to_name(ret));
                }
            }
        } else {
            ESP_LOGW(TAG, "HID device not connected");
            led_set_state(LED_STATE_PAIRED);
        }
        // High-speed reporting interval
        vTaskDelay(pdMS_TO_TICKS(HID_REPORT_DELAY_MS));
    }
}

// IMU (BNO085) task to read sensor data
static void imu_task(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "IMU task started - Polling: %dHz, Sensor: %dHz", IMU_POLL_FREQ_HZ, IMU_SENSOR_FREQ_HZ);
    
    // Initialize IMU
    esp_err_t ret = bno08x_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BNO085");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "BNO085 initialized successfully, starting sensor loop");
    
    // Enable game rotation vector at configured frequency
    ESP_LOGI(TAG, "About to enable game rotation vector at %dHz", IMU_SENSOR_FREQ_HZ);
    ret = bno08x_enable_game_rotation_vector(IMU_SENSOR_PERIOD_US);  // Period in microseconds
    ESP_LOGI(TAG, "bno08x_enable_game_rotation_vector returned: %d", ret);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector");
        bno08x_deinit();
        vTaskDelete(NULL);
        return;
    }
    
    // Give the sensor time to process configuration and start sending data
    ESP_LOGI(TAG, "Waiting for sensor to start sending data...");
    vTaskDelay(pdMS_TO_TICKS(100));  // Reduced from 500ms to 100ms
    
    ESP_LOGI(TAG, "Starting sensor monitoring loop");
    
    // Tracking variables
    imu_quaternion_t quat;
    uint32_t no_data_count = 0;
    
    while (1) {
        // Try to get quaternion data
        ret = bno08x_get_quaternion(&quat);
        if (ret == ESP_OK) {
            // Apply coordinate transformation
            bno08x_transform_coordinate_system(&quat);
            
            // Update the global sensor report with new quaternion data
            current_sensor_report.quaternion[0] = (uint16_t)((quat.i + 1.0f) * 32767.5f);     // x
            current_sensor_report.quaternion[1] = (uint16_t)((quat.j + 1.0f) * 32767.5f);     // y
            current_sensor_report.quaternion[2] = (uint16_t)((quat.k + 1.0f) * 32767.5f);     // z
            current_sensor_report.quaternion[3] = (uint16_t)((quat.real + 1.0f) * 32767.5f);  // w
            
            no_data_count = 0;  // Reset counter on successful read
            // Short delay when actively receiving data
            vTaskDelay(pdMS_TO_TICKS(IMU_POLL_DELAY_MS));
        } else {
            // No new data available
            no_data_count++;
            // Longer delay when no data to avoid busy-waiting
            if (no_data_count > 10) {
                vTaskDelay(pdMS_TO_TICKS(HID_REPORT_DELAY_MS));  // Back off to HID rate when idle
            } else {
                vTaskDelay(pdMS_TO_TICKS(IMU_POLL_DELAY_MS));   // Use polling rate
            }
        }
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
#if !CONFIG_BT_NIMBLE_ENABLED // || CONFIG_HID_DEVICE_ROLE == 1
    /* Executed for bluedroid and nimble sensor mode */
    ESP_LOGI(TAG, "Creating HID reporting task");
    xTaskCreate(hid_reporting_task, "hid_reporting_task", 4 * 1024, NULL, configMAX_PRIORITIES - 3,
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
        
        // Send initial empty input report after protocol mode is established
        sensor_hid_report_t initial_report = {0};
        // Set body position in first 4 button bits
        initial_report.buttons = (initial_report.buttons & 0xF0) | (current_body_position & 0x0F);
        
        if (s_ble_hid_param.hid_dev) {
            ESP_LOGI(TAG, "Sending initial empty input report with position: 0x%02X", current_body_position);
            esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, (uint8_t*)&initial_report, sizeof(initial_report));
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Initial input report sent successfully");
            } else {
                ESP_LOGW(TAG, "Failed to send initial input report: %s", esp_err_to_name(ret));
            }
        }
        
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
                    bno08x_reset();
                }
            } else if (param->output.length == 0) {
                // Empty output report also triggers IMU reset
                ESP_LOGI(TAG, "Triggering IMU reset via empty HID output command");
                // Trigger LED reset sequence
                led_trigger_reset_sequence();
                // Reset IMU
                bno08x_reset();
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
    
    // Change body position (cycle through 0-15 for now)
    current_body_position = (current_body_position + 1) % 16;
    ESP_LOGI(TAG, "Body position changed to: 0x%02X", current_body_position);
    
    // Update global state (reporting task will handle sending)
    ESP_LOGI(TAG, "Updated body position state - Position: 0x%02X", current_body_position);
    
    // Trigger LED reset sequence
    led_trigger_reset_sequence();
    // Reset IMU
    bno08x_reset();
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
    
// #if CONFIG_HID_DEVICE_ROLE == 2 // Glove - Gamepad mode
//     ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GAMEPAD, ble_hid_config.device_name);
// #else
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, ble_hid_config.device_name);
// #endif
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
    
    // Start BNO085 sensor task
    ESP_LOGI(TAG, "Starting BNO085 IMU sensor task");
    xTaskCreate(imu_task, "imu_task", 4 * 1024, NULL, configMAX_PRIORITIES - 2, NULL);  // Higher priority for SPI timing
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
