/*
 * SPDX-FileCopyrightText: 2024-2025 Solidic Labs - Eidon AI
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "button.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BUTTON";

// Global variables
static TaskHandle_t button_task_handle = NULL;
static button_callback_t button_callback = NULL;
static bool button_initialized = false;
static bool button_pressed_flag = false;  // Flag to track button press state
static volatile bool button_pressed = false;  // Global debounced button state

// Button interrupt handler
static void IRAM_ATTR button_isr_handler(void* arg)
{
    // Notify the button task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)arg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Button task to handle debouncing and callback execution
static void button_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Button task started");
    
    while (1) {
        // Wait for button press notification
        if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            // Debounce delay
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Check if button is still pressed (debounce)
            if (gpio_get_level(BOOT_BUTTON_GPIO) == BOOT_BUTTON_ACTIVE_LEVEL) {
                ESP_LOGI(TAG, "Boot button pressed");
                
                // Set button pressed flag
                button_pressed_flag = true;
                button_pressed = true;
                
                // Execute callback if set
                if (button_callback != NULL) {
                    button_callback();
                }
                
                // Wait for button release to prevent multiple triggers
                while (gpio_get_level(BOOT_BUTTON_GPIO) == BOOT_BUTTON_ACTIVE_LEVEL) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                
                ESP_LOGI(TAG, "Boot button released");
                button_pressed = false;
            }
        }
    }
}

esp_err_t button_init(void)
{
    if (button_initialized) {
        ESP_LOGW(TAG, "Button already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing boot button on GPIO %d", BOOT_BUTTON_GPIO);
    
    // Configure boot button as input with internal pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Trigger on button press (falling edge)
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Install GPIO ISR service
    ret = gpio_install_isr_service(0);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GPIO ISR service installed successfully");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "GPIO ISR service already installed, reusing existing service");
    } else {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    
    button_initialized = true;
    ESP_LOGI(TAG, "Boot button initialized successfully");
    
    return ESP_OK;
}

void button_set_callback(button_callback_t callback)
{
    button_callback = callback;
    ESP_LOGI(TAG, "Button callback set");
}

esp_err_t button_task_start(void)
{
    if (!button_initialized) {
        ESP_LOGE(TAG, "Button not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (button_task_handle != NULL) {
        ESP_LOGW(TAG, "Button task already running");
        return ESP_OK;
    }
    
    // Add ISR handler
    esp_err_t ret = gpio_isr_handler_add(BOOT_BUTTON_GPIO, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create button task
    BaseType_t task_ret = xTaskCreate(button_task, "button_task", 2048, NULL, 4, &button_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        gpio_isr_handler_remove(BOOT_BUTTON_GPIO);
        return ESP_FAIL;
    }
    
    // Update ISR handler with task handle
    ret = gpio_isr_handler_add(BOOT_BUTTON_GPIO, button_isr_handler, button_task_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update GPIO ISR handler: %s", esp_err_to_name(ret));
        vTaskDelete(button_task_handle);
        button_task_handle = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "Button task started successfully");
    return ESP_OK;
}

void button_task_stop(void)
{
    if (button_task_handle != NULL) {
        // Remove ISR handler
        gpio_isr_handler_remove(BOOT_BUTTON_GPIO);
        
        // Delete task
        vTaskDelete(button_task_handle);
        button_task_handle = NULL;
        
        ESP_LOGI(TAG, "Button task stopped");
    }
}

int button_get_state(void)
{
    if (!button_initialized) {
        return 0;
    }
    
    return (gpio_get_level(BOOT_BUTTON_GPIO) == BOOT_BUTTON_ACTIVE_LEVEL) ? 1 : 0;
}

bool button_was_pressed(void)
{
    if (!button_initialized) {
        return false;
    }
    
    bool was_pressed = button_pressed_flag;
    button_pressed_flag = false;  // Clear the flag when checked
    return was_pressed;
}

void button_clear_pressed_state(void)
{
    button_pressed_flag = false;
}

bool button_is_pressed(void)
{
    return button_pressed;
} 