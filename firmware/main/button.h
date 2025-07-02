/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

// Button configuration
#define BOOT_BUTTON_GPIO          9
#define BOOT_BUTTON_ACTIVE_LEVEL  0  // Active low (button pressed = 0)

// Button callback function type
typedef void (*button_callback_t)(void);

/**
 * @brief Initialize the boot button
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t button_init(void);

/**
 * @brief Set the callback function to be called when button is pressed
 * 
 * @param callback Function to call on button press
 */
void button_set_callback(button_callback_t callback);

/**
 * @brief Start the button task
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t button_task_start(void);

/**
 * @brief Stop the button task
 */
void button_task_stop(void);

/**
 * @brief Get the current button state
 * 
 * @return int 1 if button is pressed, 0 if not pressed
 */
int button_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H 