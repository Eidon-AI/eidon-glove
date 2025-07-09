/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef BLE_H
#define BLE_H

#include "esp_err.h"
#include "esp_hid_gap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the BLE stack and HID device
 * 
 * This function handles all BLE-related initialization including:
 * - HID GAP initialization
 * - BLE advertisement setup
 * - GATT server callbacks (Bluedroid)
 * - HID device initialization
 * - NimBLE-specific setup if enabled
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ble_init(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_H 