/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef MAIN_CONFIG_H
#define MAIN_CONFIG_H

/*
 * Frequency Configuration
 * 
 * Simply set the desired frequencies in Hz, and all timing delays will be
 * automatically calculated. This makes it easy to experiment with different
 * update rates without manually calculating delay values.
 * 
 * Note: Higher frequencies consume more power and CPU resources.
 * The BNO085 sensor supports up to 1000Hz for certain modes.
 */

// Define desired frequencies and auto-calculate delays
#define HID_REPORT_FREQ_HZ      100     // BLE HID report transmission rate (<=100Hz)
#define IMU_POLL_FREQ_HZ        100     // IMU data polling rate
#define IMU_SENSOR_FREQ_HZ      100     // BNO085 sensor update rate (100-400Hz)

// Auto-calculate delays in milliseconds
#define HID_REPORT_DELAY_MS     (1000 / HID_REPORT_FREQ_HZ)
#define IMU_POLL_DELAY_MS       (1000 / IMU_POLL_FREQ_HZ)
#define IMU_SENSOR_PERIOD_US    (1000000 / IMU_SENSOR_FREQ_HZ)

#endif // MAIN_CONFIG_H 