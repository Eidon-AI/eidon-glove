/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef HID_REPORTS_H
#define HID_REPORTS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_hidd.h"
#include "descriptors/tracker.h"
#include "descriptors/glove.h"

#ifdef __cplusplus
extern "C" {
#endif

// Report IDs
#define INPUT_REPORT_ID   1
#define OUTPUT_REPORT_ID  2
#define FEATURE_REPORT_ID 3

// HID report structure type selection based on device role
#if CONFIG_HID_DEVICE_ROLE == 1
typedef tracker_hid_report_t sensor_hid_report_t;
#elif CONFIG_HID_DEVICE_ROLE == 2
typedef glove_hid_report_t sensor_hid_report_t;
#else
typedef tracker_hid_report_t sensor_hid_report_t;  // Default to tracker
#endif

// HID device parameter structure
typedef struct {
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} hid_param_t;

// Function declarations

// Initialize HID device globals
void hid_device_init_globals(void);

// Get HID device parameters
hid_param_t* hid_device_get_params(void);

// HID reporting task
void hid_reporting_task(void *pvParameters);

// Update sensor report with new quaternion data
void hid_device_update_quaternion(float i, float j, float k, float real);

// Update button state in report
void hid_device_update_button_state(bool pressed);

// Update body position in report
void hid_device_update_body_position(uint8_t position);

// Get current body position
uint8_t hid_device_get_body_position(void);

// Feature report functions
esp_err_t hid_device_get_feature_report(uint8_t report_id, uint8_t *data, size_t *length);
void hid_device_update_feature_report(uint8_t r, uint8_t g, uint8_t b);
uint16_t hid_device_get_feature_handle(void);
void hid_device_set_feature_handle(uint16_t handle);

// Get report maps
esp_hid_raw_report_map_t* hid_device_get_report_maps(void);

// Get device config
esp_hid_device_config_t* hid_device_get_config(void);

#ifdef __cplusplus
}
#endif

#endif // HID_REPORTS_H 