/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "ble.h"
#include "imu.h"
#include "config.h"
#include "led.h"
#include "button.h"
#include "storage.h"
#include "hid_device.h"

static const char *TAG = "MAIN";

// Button callback function for IMU reset and position change
static void button_imu_reset_callback(void)
{
    ESP_LOGI(TAG, "Button pressed - triggering IMU reset and position change");
    
    // Change body position (cycle through 0-15 for now)
    uint8_t current_position = hid_device_get_body_position();
    uint8_t new_position = (current_position + 1) % 16;
    hid_device_update_body_position(new_position);
    ESP_LOGI(TAG, "Body position changed to: 0x%02X", new_position);
    
    // Update global state (reporting task will handle sending)
    ESP_LOGI(TAG, "Updated body position state - Position: 0x%02X", new_position);
    
    // Trigger LED reset sequence
    led_trigger_reset_sequence();
    // Reset IMU
    imu_reset();
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main() started");
    
    esp_err_t ret;
    
    // Initialize LEDs
    ESP_LOGI(TAG, "*** Initializing LEDs ***");
    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED initialization failed, continuing without LED control");
    }
    
    // Initialize storage module (includes NVS flash initialization)
    ESP_LOGI(TAG, "*** Initializing storage module ***");
    ret = storage_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Storage initialization failed: %s", esp_err_to_name(ret));
    }
    
    // Initialize HID device globals (loads saved values from storage)
    hid_device_init_globals();
    
    // Get current shell color and update feature report data
    shell_color_t color;
    ret = storage_get_shell_color(&color);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Device shell color loaded from storage ***");
        ESP_LOGI(TAG, "Loaded color: R=0x%02X, G=0x%02X, B=0x%02X", color.r, color.g, color.b);
        
        // Update feature report data in HID device module
        hid_device_update_feature_report(color.r, color.g, color.b);
    } else {
        ESP_LOGI(TAG, "*** Using default device shell color on startup ***");
        ESP_LOGI(TAG, "Default color: R=0x%02X, G=0x%02X, B=0x%02X", 
                 PREFERENCES_DEFAULT_SHELL_COLOR_R, PREFERENCES_DEFAULT_SHELL_COLOR_G, PREFERENCES_DEFAULT_SHELL_COLOR_B);
    }
    
    // Get current body position from storage
    body_position_t position;
    ret = storage_get_body_position(&position);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "*** Body position loaded from storage ***");
        ESP_LOGI(TAG, "Loaded position: 0x%02X", position.position);
        hid_device_update_body_position(position.position);
    } else {
        ESP_LOGI(TAG, "*** Using default body position on startup ***");
        ESP_LOGI(TAG, "Default position: 0x%02X", PREFERENCES_DEFAULT_BODY_POSITION);
    }
    
    // Set initial LED state to STROBE for testing (will be set properly when BLE starts)
    led_set_state(LED_STATE_STROBE);
    ESP_LOGI(TAG, "Set initial LED state to STROBE for testing");

    // Initialize BLE stack
    ret = ble_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed: %s", esp_err_to_name(ret));
        return;
    }

    // Start BNO085 sensor task FIRST (so it can install GPIO ISR service with its preferred flags)
    ESP_LOGI(TAG, "Starting BNO085 IMU sensor task");
    xTaskCreate(imu_task, "imu_task", 4 * 1024, NULL, configMAX_PRIORITIES - 2, NULL);  // Higher priority for SPI timing
    
    // Small delay to let IMU initialize and install GPIO ISR service
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize and start button functionality
    ESP_LOGI(TAG, "*** Initializing button ***");
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
}
