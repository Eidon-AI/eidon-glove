/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "hid_reports.h"
#include "esp_log.h"
#include "esp_hidd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "button.h"
#include "led.h"
#include "storage.h"
#include "config.h"
#include <string.h>
#include <math.h>

static const char *TAG = "HID_DEVICE";

// Global HID device parameters
static hid_param_t s_ble_hid_param = {0};

// Global sensor data state
static sensor_hid_report_t current_sensor_report = {0};
static sensor_hid_report_t last_sent_report = {0};
static bool last_sent_button_state = false;

// Global state variables for input reports
static uint8_t current_body_position = 0x00;  // Default position (0-15)

// Global feature report data for Report ID 3 (3 bytes: R, G, B)
static uint8_t current_feature_report[3] = {PREFERENCES_DEFAULT_SHELL_COLOR_R, PREFERENCES_DEFAULT_SHELL_COLOR_G, PREFERENCES_DEFAULT_SHELL_COLOR_B};

// Feature report GATT attribute handle (set during device initialization)
static uint16_t feature_report_handle = 0;

// Report maps
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

// Device configuration
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

// Initialize HID device globals
void hid_device_init_globals(void)
{
    ESP_LOGI(TAG, "Initializing HID device globals");
    
    // Initialize with defaults
    memset(&current_sensor_report, 0, sizeof(current_sensor_report));
    memset(&last_sent_report, 0, sizeof(last_sent_report));
    last_sent_button_state = false;
    
    // Load saved shell color from storage
    shell_color_t color;
    esp_err_t ret = storage_get_shell_color(&color);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Device shell color loaded from storage ***");
        ESP_LOGI(TAG, "Loaded color: R=0x%02X, G=0x%02X, B=0x%02X", color.r, color.g, color.b);
        current_feature_report[0] = color.r;
        current_feature_report[1] = color.g;
        current_feature_report[2] = color.b;
    } else {
        ESP_LOGI(TAG, "*** Using default device shell color ***");
        ESP_LOGI(TAG, "Default color: R=0x%02X, G=0x%02X, B=0x%02X", 
                 PREFERENCES_DEFAULT_SHELL_COLOR_R, PREFERENCES_DEFAULT_SHELL_COLOR_G, PREFERENCES_DEFAULT_SHELL_COLOR_B);
        // Defaults already set in initialization
    }
    
    // Load saved body position from storage
    body_position_t position;
    ret = storage_get_body_position(&position);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Body position loaded from storage ***");
        ESP_LOGI(TAG, "Loaded position: 0x%02X", position.position);
        current_body_position = position.position;
    } else {
        ESP_LOGI(TAG, "*** Using default body position ***");
        ESP_LOGI(TAG, "Default position: 0x%02X", PREFERENCES_DEFAULT_BODY_POSITION);
        current_body_position = PREFERENCES_DEFAULT_BODY_POSITION;
    }
}

// Get HID device parameters
hid_param_t* hid_device_get_params(void)
{
    return &s_ble_hid_param;
}

// Get report maps
esp_hid_raw_report_map_t* hid_device_get_report_maps(void)
{
    return ble_report_maps;
}

// Get device config
esp_hid_device_config_t* hid_device_get_config(void)
{
    return &ble_hid_config;
}

// Update sensor report with new quaternion data
void hid_device_update_quaternion(float i, float j, float k, float real)
{
    current_sensor_report.quaternion[0] = (uint16_t)((i + 1.0f) * 32767.5f);     // x
    current_sensor_report.quaternion[1] = (uint16_t)((j + 1.0f) * 32767.5f);     // y
    current_sensor_report.quaternion[2] = (uint16_t)((k + 1.0f) * 32767.5f);     // z
    current_sensor_report.quaternion[3] = (uint16_t)((real + 1.0f) * 32767.5f);  // w
}

// Update button state - not used directly anymore
void hid_device_update_button_state(bool pressed)
{
    // Button state is now read directly in reporting task
    (void)pressed;
}

// Update body position
void hid_device_update_body_position(uint8_t position)
{
    current_body_position = position & 0x0F;  // Ensure only lower 4 bits
}

// Get current body position
uint8_t hid_device_get_body_position(void)
{
    return current_body_position;
}

// Feature report functions
esp_err_t hid_device_get_feature_report(uint8_t report_id, uint8_t *data, size_t *length)
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

void hid_device_update_feature_report(uint8_t r, uint8_t g, uint8_t b)
{
    current_feature_report[0] = r;
    current_feature_report[1] = g;
    current_feature_report[2] = b;
}

uint16_t hid_device_get_feature_handle(void)
{
    return feature_report_handle;
}

void hid_device_set_feature_handle(uint16_t handle)
{
    feature_report_handle = handle;
}

// High-speed HID reporting task
void hid_reporting_task(void *pvParameters)
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