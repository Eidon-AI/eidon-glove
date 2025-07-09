/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef DEVICE_INFO_H
#define DEVICE_INFO_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate a unique serial number based on MAC address
 * 
 * Formats as 10-digit decimal number (from last 4 MAC bytes) 
 * followed by last 2 MAC bytes in hexadecimal
 * 
 * Example: "0123456789A1B2"
 * - First 10 digits: decimal representation of MAC[2:5]
 * - Last 4 chars: hex representation of MAC[4:5]
 * 
 * @param serial_buffer Buffer to store the serial number
 * @param buffer_size Size of the buffer
 */
void device_info_get_serial_number(char *serial_buffer, size_t buffer_size);

/**
 * @brief Generate a unique device name with MAC suffix
 * 
 * Formats as "Eidon [Device]-XXXX" where XXXX is last 4 hex digits of MAC
 * 
 * @param name_buffer Buffer to store the device name
 * @param buffer_size Size of the buffer
 */
void device_info_get_device_name(char *name_buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_INFO_H 