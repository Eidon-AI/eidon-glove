/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "ble.h"
#include "esp_log.h"
#include "esp_hidd.h"
#include "hid_device.h"
#include "device_info.h"
#include "ble_hid.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_hid_gap.h"

#if CONFIG_BT_HID_DEVICE_ENABLED
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#if CONFIG_BT_SDP_COMMON_ENABLED
#include "esp_sdp_api.h"
#endif
#endif

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#else
#if CONFIG_BT_BLE_ENABLED
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#endif
#endif

static const char *TAG = "BLE";

// Static storage for device identifiers
static char unique_device_name[32];
static char unique_serial[32];

esp_err_t ble_init(void)
{
    esp_err_t ret;
    
#if HID_DEV_MODE == HIDD_IDLE_MODE
    ESP_LOGE(TAG, "Please turn on BT HID device or BLE!");
    return;
#endif
    
    ESP_LOGI(TAG, "Initializing BLE stack");
    
    // Initialize HID GAP
    ESP_LOGI(TAG, "Setting HID GAP, mode: %d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hid_gap_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
    // Get HID device config
    esp_hid_device_config_t *hid_config = hid_device_get_config();
    
    // Generate unique device name with MAC suffix
    device_info_get_device_name(unique_device_name, sizeof(unique_device_name));
    hid_config->device_name = unique_device_name;
    
    // Initialize BLE advertisement
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, hid_config->device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hid_ble_gap_adv_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
#if CONFIG_BT_BLE_ENABLED
    // Register GATT server callback for Bluedroid
    ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }
#endif
    
    // Generate unique serial number from MAC address
    device_info_get_serial_number(unique_serial, sizeof(unique_serial));
    hid_config->serial_number = unique_serial;

    // For NimBLE, HID device initialization will be done in the sync callback
    // For Bluedroid, we need to do it here
#if !CONFIG_BT_NIMBLE_ENABLED
    ESP_LOGI(TAG, "Initializing BLE HID device");
    hid_param_t *hid_param = hid_device_get_params();
    ret = esp_hidd_dev_init(hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &hid_param->hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BLE HID device initialized successfully");
    esp_hid_ble_gap_adv_start();
#endif

#endif // CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED

#if CONFIG_BT_NIMBLE_ENABLED
    // NimBLE-specific initialization
    extern void ble_store_config_init(void);
    extern void ble_store_util_status_rr(struct ble_store_status_event *event, void *arg);
    
    /* XXX Need to have template for store */
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    /* Starting nimble task after gatts is initialized*/
    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "NimBLE enabled, waiting for host sync callback...");
#endif

#if CONFIG_BT_HID_DEVICE_ENABLED
    // Initialize Classic Bluetooth HID
    ESP_LOGI(TAG, "Initializing Classic Bluetooth HID");
    
    esp_hid_device_config_t *bt_config = hid_device_get_config();
    hid_param_t *bt_hid_param = hid_device_get_params();
    
    ESP_LOGI(TAG, "Setting Classic BT device name: %s", bt_config->device_name);
    esp_bt_gap_set_device_name(bt_config->device_name);
    
    ESP_LOGI(TAG, "Setting COD major: peripheral");
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_POINTING;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "Initializing Classic BT HID device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(bt_config, ESP_HID_TRANSPORT_BT, ble_hidd_event_callback, &bt_hid_param->hid_dev));
    
#if CONFIG_BT_SDP_COMMON_ENABLED
    // Note: SDP callback registration might be needed for Classic BT service discovery
    // This would need a proper implementation if Classic BT is actually used
    ESP_LOGI(TAG, "SDP initialization would go here for Classic BT");
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif /* CONFIG_BT_HID_DEVICE_ENABLED */

    return ESP_OK;
} 