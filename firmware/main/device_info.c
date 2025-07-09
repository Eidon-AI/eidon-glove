/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "device_info.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "DEVICE_INFO";

void device_info_get_serial_number(char *serial_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // Use WiFi STA MAC address (unique per chip)
    
    // Convert last 4 bytes of MAC to a 32-bit decimal number
    uint32_t mac_decimal = ((uint32_t)mac[2] << 24) | 
                          ((uint32_t)mac[3] << 16) | 
                          ((uint32_t)mac[4] << 8) | 
                          ((uint32_t)mac[5]);

    // Format: 10-digit decimal + last 2 MAC bytes in hex
    // Example: "0123456789A1B2"
    snprintf(serial_buffer, buffer_size, "%010" PRIu32 "%02X%02X", 
             mac_decimal, mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Generated serial number: %s", serial_buffer);
}

void device_info_get_device_name(char *name_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
#if CONFIG_HID_DEVICE_ROLE == 1
    // Format as "Eidon Tracker-XXXX" where XXXX is last 4 hex digits of MAC
    snprintf(name_buffer, buffer_size, "Eidon Tracker-%02X%02X", 
             mac[4], mac[5]);
#elif CONFIG_HID_DEVICE_ROLE == 2
    // Format as "Eidon Glove-XXXX" where XXXX is last 4 hex digits of MAC
    snprintf(name_buffer, buffer_size, "Eidon Glove-%02X%02X", 
             mac[4], mac[5]);
#else
    // Default to Tracker
    snprintf(name_buffer, buffer_size, "Eidon Tracker-%02X%02X", 
             mac[4], mac[5]);
#endif
    
    ESP_LOGI(TAG, "Generated device name: %s", name_buffer);
} 