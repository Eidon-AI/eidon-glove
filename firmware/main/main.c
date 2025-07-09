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
#include "hid_device.h"
#include "device_info.h"
#include "ble_hid.h"

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
    
    ESP_LOGI(TAG, "*** Current feature report data after startup ***");
    // Note: Feature report data is now managed by hid_device module
    
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

    ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_ERROR_CHECK( ret );

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
    // Get HID device config
    esp_hid_device_config_t *hid_config = hid_device_get_config();
    
    // Generate unique device name with MAC suffix
    static char unique_device_name[32];
    device_info_get_device_name(unique_device_name, sizeof(unique_device_name));
    hid_config->device_name = unique_device_name;
    
// #if CONFIG_HID_DEVICE_ROLE == 2 // Glove - Gamepad mode
//     ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GAMEPAD, hid_config->device_name);
// #else
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, hid_config->device_name);
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
    device_info_get_serial_number(unique_serial, sizeof(unique_serial));
    hid_config->serial_number = unique_serial;

    // For NimBLE, HID device initialization and advertisement will be started in the sync callback
    // For Bluedroid, we need to do it here
#if !CONFIG_BT_NIMBLE_ENABLED
    ESP_LOGI(TAG, "setting ble device");
    hid_param_t *hid_param = hid_device_get_params();
    ret = esp_hidd_dev_init(hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &hid_param->hid_dev);
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
