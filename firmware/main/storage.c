/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "storage.h"

static const char *TAG = "STORAGE";

// Global storage for current values
static shell_color_t current_shell_color = {
    .r = PREFERENCES_DEFAULT_SHELL_COLOR_R,
    .g = PREFERENCES_DEFAULT_SHELL_COLOR_G,
    .b = PREFERENCES_DEFAULT_SHELL_COLOR_B
};

static body_position_t current_body_position = {
    .position = PREFERENCES_DEFAULT_BODY_POSITION
};

esp_err_t storage_init(void)
{
    ESP_LOGI(TAG, "Initializing preferences module");
    
    // Load shell color from NVS
    uint8_t r, g, b;
    esp_err_t ret = storage_load_shell_color(&r, &g, &b);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Shell color loaded from NVS: R=0x%02X, G=0x%02X, B=0x%02X", r, g, b);
    } else {
        ESP_LOGI(TAG, "Using default shell color: R=0x%02X, G=0x%02X, B=0x%02X", 
                 current_shell_color.r, current_shell_color.g, current_shell_color.b);
    }
    
    // Load body position from NVS (for future use)
    uint8_t position;
    ret = storage_load_body_position(&position);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Body position loaded from NVS: 0x%02X", position);
    } else {
        ESP_LOGI(TAG, "Using default body position: 0x%02X", current_body_position.position);
    }
    
    return ESP_OK;
}

esp_err_t storage_save_shell_color(uint8_t r, uint8_t g, uint8_t b)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS namespace
    err = nvs_open(PREFERENCES_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Prepare color data (3 bytes: R, G, B)
    uint8_t color_data[3] = {r, g, b};
    
    // Write color data to NVS
    err = nvs_set_blob(nvs_handle, PREFERENCES_KEY_SHELL_COLOR, color_data, sizeof(color_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing shell color to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    // Update global color
    current_shell_color.r = r;
    current_shell_color.g = g;
    current_shell_color.b = b;
    
    ESP_LOGI(TAG, "Shell color saved to NVS: R=0x%02X, G=0x%02X, B=0x%02X", r, g, b);
    return ESP_OK;
}

esp_err_t storage_load_shell_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS namespace
    err = nvs_open(PREFERENCES_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Read color data from NVS
    size_t required_size = 3;
    uint8_t color_data[3];
    err = nvs_get_blob(nvs_handle, PREFERENCES_KEY_SHELL_COLOR, color_data, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Shell color not found in NVS, using defaults: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        // Return default values
        *r = PREFERENCES_DEFAULT_SHELL_COLOR_R;
        *g = PREFERENCES_DEFAULT_SHELL_COLOR_G;
        *b = PREFERENCES_DEFAULT_SHELL_COLOR_B;
        return ESP_ERR_NOT_FOUND;
    }
    
    nvs_close(nvs_handle);
    
    // Extract RGB values
    *r = color_data[0];
    *g = color_data[1];
    *b = color_data[2];
    
    // Update global color
    current_shell_color.r = *r;
    current_shell_color.g = *g;
    current_shell_color.b = *b;
    
    ESP_LOGI(TAG, "Shell color loaded from NVS: R=0x%02X, G=0x%02X, B=0x%02X", *r, *g, *b);
    return ESP_OK;
}

esp_err_t storage_get_shell_color(shell_color_t *color)
{
    if (color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    color->r = current_shell_color.r;
    color->g = current_shell_color.g;
    color->b = current_shell_color.b;
    
    return ESP_OK;
}

esp_err_t storage_set_shell_color(const shell_color_t *color)
{
    if (color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return storage_save_shell_color(color->r, color->g, color->b);
}

esp_err_t storage_save_body_position(uint8_t position)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS namespace
    err = nvs_open(PREFERENCES_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Write position data to NVS
    err = nvs_set_u8(nvs_handle, PREFERENCES_KEY_BODY_POSITION, position);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing body position to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    
    // Update global position
    current_body_position.position = position;
    
    ESP_LOGI(TAG, "Body position saved to NVS: 0x%02X", position);
    return ESP_OK;
}

esp_err_t storage_load_body_position(uint8_t *position)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS namespace
    err = nvs_open(PREFERENCES_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }
    
    // Read position data from NVS
    err = nvs_get_u8(nvs_handle, PREFERENCES_KEY_BODY_POSITION, position);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Body position not found in NVS, using default: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        // Return default value
        *position = PREFERENCES_DEFAULT_BODY_POSITION;
        return ESP_ERR_NOT_FOUND;
    }
    
    nvs_close(nvs_handle);
    
    // Update global position
    current_body_position.position = *position;
    
    ESP_LOGI(TAG, "Body position loaded from NVS: 0x%02X", *position);
    return ESP_OK;
}

esp_err_t storage_get_body_position(body_position_t *position)
{
    if (position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    position->position = current_body_position.position;
    
    return ESP_OK;
}

esp_err_t storage_set_body_position(const body_position_t *position)
{
    if (position == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return storage_save_body_position(position->position);
} 