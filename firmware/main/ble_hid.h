/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef BLE_HID_H
#define BLE_HID_H

#include "esp_err.h"
#include "esp_hidd.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the BLE HID stack
 * 
 * This function handles all BLE-related initialization including:
 * - HID GAP initialization
 * - BLE advertisement setup
 * - Device name and serial number generation
 * - GATT server callbacks (Bluedroid)
 * - HID device initialization
 * - NimBLE-specific setup if enabled
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ble_hid_init(void);

/**
 * @brief Start the BLE HID task
 * 
 * Creates the HID reporting task if it doesn't already exist
 */
void ble_hid_task_start_up(void);

/**
 * @brief Stop the BLE HID task
 * 
 * Deletes the HID reporting task if it exists
 */
void ble_hid_task_shut_down(void);

/**
 * @brief BLE HID event callback handler
 * 
 * Handles all HID events from the ESP-IDF HID stack
 * 
 * @param handler_args Handler arguments (unused)
 * @param base Event base
 * @param id Event ID
 * @param event_data Event specific data
 */
void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

/**
 * @brief Get feature report data
 * 
 * Retrieves feature report data for the specified report ID
 * 
 * @param report_id The report ID to get
 * @param data Buffer to store the report data
 * @param length Input: buffer size, Output: actual data length
 * @return ESP_OK on success
 */
esp_err_t get_feature_report(uint8_t report_id, uint8_t *data, size_t *length);

#if CONFIG_BT_NIMBLE_ENABLED
/**
 * @brief NimBLE HID device host task
 * 
 * Main task for NimBLE HID operation
 * 
 * @param param Task parameters (unused)
 */
void ble_hid_device_host_task(void *param);

/**
 * @brief NimBLE BLE store configuration initialization
 */
void ble_store_config_init(void);
#endif

#ifdef __cplusplus
}
#endif

#endif // BLE_HID_H 