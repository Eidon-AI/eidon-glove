/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "button.h"
#include "ble_hid.h"
#include "imu.h"
#include "led.h"
#include "storage.h"
#include "hid_reports.h"

static const char *TAG = "MAIN";

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
    
    // Set initial LED state to STROBE for testing (will be set properly when BLE starts)
    led_set_state(LED_STATE_STROBE);
    ESP_LOGI(TAG, "Set initial LED state to STROBE for testing");

    // Initialize BLE stack
    ret = ble_hid_init();
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
        // Start button task (button now has default IMU reset callback)
        ret = button_task_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Button task start failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Button functionality started successfully");
        }
    }
}
