/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// NVS storage constants
#define PREFERENCES_NAMESPACE "eidon_config"

// Shell color settings
#define PREFERENCES_KEY_SHELL_COLOR "shell_color"
#define PREFERENCES_DEFAULT_SHELL_COLOR_R 0xFF  // Default red
#define PREFERENCES_DEFAULT_SHELL_COLOR_G 0xFF  // Default green  
#define PREFERENCES_DEFAULT_SHELL_COLOR_B 0xFF  // Default blue

// Body position settings (for future use)
#define PREFERENCES_KEY_BODY_POSITION "body_pos"
#define PREFERENCES_DEFAULT_BODY_POSITION 0x01  // Default body position

// Shell color structure
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} shell_color_t;

// Body position structure (for future use)
typedef struct {
    uint8_t position;
} body_position_t;

// Function declarations

/**
 * @brief Initialize the preferences module
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_init(void);

/**
 * @brief Save shell color to NVS flash storage
 * 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_save_shell_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Load shell color from NVS flash storage
 * 
 * @param r Pointer to store red component
 * @param g Pointer to store green component
 * @param b Pointer to store blue component
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if not found, other error on failure
 */
esp_err_t storage_load_shell_color(uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief Get current shell color
 * 
 * @param color Pointer to shell_color_t structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_get_shell_color(shell_color_t *color);

/**
 * @brief Set current shell color (saves to flash)
 * 
 * @param color Pointer to shell_color_t structure with new color
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_set_shell_color(const shell_color_t *color);

/**
 * @brief Save body position to NVS flash storage (for future use)
 * 
 * @param position Body position value (0-255)
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_save_body_position(uint8_t position);

/**
 * @brief Load body position from NVS flash storage (for future use)
 * 
 * @param position Pointer to store body position value
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if not found, other error on failure
 */
esp_err_t storage_load_body_position(uint8_t *position);

/**
 * @brief Get current body position (for future use)
 * 
 * @param position Pointer to body_position_t structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_get_body_position(body_position_t *position);

/**
 * @brief Set current body position (saves to flash) (for future use)
 * 
 * @param position Pointer to body_position_t structure with new position
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t storage_set_body_position(const body_position_t *position);

#ifdef __cplusplus
}
#endif

#endif // STORAGE_H 