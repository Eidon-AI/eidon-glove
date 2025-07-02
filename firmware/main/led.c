#include "led.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED";

// LED configuration
#define LED_GPIO          15  // Single LED GPIO (D15)
#define LEDC_TIMER        LEDC_TIMER_0
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL      LEDC_CHANNEL_0
#define LEDC_DUTY_RES     LEDC_TIMER_8_BIT   // 8-bit resolution (simpler)
#define LEDC_FREQ         1000               // 1kHz frequency (lower)

// LED brightness levels (8-bit resolution: 0-255)
#define LED_BRIGHTNESS_OFF        0
#define LED_BRIGHTNESS_DIM        64     // ~25% brightness
#define LED_BRIGHTNESS_BRIGHT     128    // ~50% brightness
#define LED_BRIGHTNESS_FULL       255    // 100% brightness

// LED timing constants
#define LED_STROBE_PERIOD_MS      1000   // 1 second strobe period
#define LED_RESET_FLASH_DURATION_MS 200  // 200ms per flash
#define LED_RESET_FLASH_GAP_MS    300    // 300ms between flashes
#define LED_RESET_SEQUENCE_TOTAL_MS 1500 // Total reset sequence time

// Static variables for LED state management
static led_state_t current_led_state = LED_STATE_OFF;
static bool led_state_override = false;  // True when showing reset sequence
static uint32_t led_state_timer = 0;     // Timer for state transitions

// Function to initialize RGB LEDs using LEDC
esp_err_t led_init(void)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure single LED channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_GPIO,
        .duty          = 0, // Set duty to 0%
        .hpoint        = 0
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Single LED initialized successfully with LEDC PWM on GPIO %d", LED_GPIO);
    
    // Start LED state management task
    TaskHandle_t led_task_handle = NULL;
    BaseType_t task_created = xTaskCreate(led_task, "led_task", 2048, NULL, 3, &led_task_handle);
    if (task_created == pdPASS) {
        ESP_LOGI(TAG, "LED state task created successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create LED state task");
    }
    
    return ESP_OK;
}

// Function to set single LED brightness (for state management)
void led_set_brightness(uint32_t brightness)
{
    // ESP_LOGI(TAG, "Setting LED brightness to %lu", brightness); // Commented out for reduced log noise
    
    // Use the single LED channel
    esp_err_t ret = ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LED duty: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LED duty: %s", esp_err_to_name(ret));
        return;
    }
    
    // ESP_LOGI(TAG, "LED brightness set successfully to %lu", brightness); // Commented out for reduced log noise
}

// Function to set LED state
void led_set_state(led_state_t state)
{
    if (led_state_override) {
        // Don't change state if we're in override mode (showing reset sequence)
        return;
    }
    
    current_led_state = state;
    led_state_timer = 0;
    
    switch (state) {
        case LED_STATE_OFF:
            ESP_LOGI(TAG, "LED state: OFF");
            break;
        case LED_STATE_STROBE:
            ESP_LOGI(TAG, "LED state: STROBE (device on, not paired)");
            break;
        case LED_STATE_PAIRED:
            ESP_LOGI(TAG, "LED state: PAIRED (bright)");
            break;
        case LED_STATE_TRANSMITTING:
            ESP_LOGI(TAG, "LED state: TRANSMITTING (bright)");
            break;
        case LED_STATE_RESET:
            // This will be handled by the LED task
            ESP_LOGI(TAG, "LED state: RESET (sequence starting)");
            break;
    }
}

// Function to trigger IMU reset LED sequence
void led_trigger_reset_sequence(void)
{
    led_state_override = true;
    current_led_state = LED_STATE_RESET;
    led_state_timer = 0;
    ESP_LOGI(TAG, "Starting LED reset sequence");
}

// LED state management task
void led_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LED state task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    led_state_t last_state = LED_STATE_OFF;
    
    while (1) {
        // Check if state has changed
        if (current_led_state != last_state && !led_state_override) {
            ESP_LOGI(TAG, "LED state changed from %d to %d", last_state, current_led_state);
            last_state = current_led_state;
        }
        
        if (led_state_override && current_led_state == LED_STATE_RESET) {
            // Handle reset sequence (3 quick flashes)
            uint32_t elapsed = led_state_timer;
            
            if (elapsed < LED_RESET_FLASH_DURATION_MS) {
                // First flash
                led_set_brightness(LED_BRIGHTNESS_FULL);
            } else if (elapsed < LED_RESET_FLASH_DURATION_MS + LED_RESET_FLASH_GAP_MS) {
                // Gap after first flash
                led_set_brightness(LED_BRIGHTNESS_OFF);
            } else if (elapsed < 2 * LED_RESET_FLASH_DURATION_MS + LED_RESET_FLASH_GAP_MS) {
                // Second flash
                led_set_brightness(LED_BRIGHTNESS_FULL);
            } else if (elapsed < 2 * LED_RESET_FLASH_DURATION_MS + 2 * LED_RESET_FLASH_GAP_MS) {
                // Gap after second flash
                led_set_brightness(LED_BRIGHTNESS_OFF);
            } else if (elapsed < 3 * LED_RESET_FLASH_DURATION_MS + 2 * LED_RESET_FLASH_GAP_MS) {
                // Third flash
                led_set_brightness(LED_BRIGHTNESS_FULL);
            } else if (elapsed < LED_RESET_SEQUENCE_TOTAL_MS) {
                // Final gap
                led_set_brightness(LED_BRIGHTNESS_OFF);
            } else {
                // Reset sequence complete, return to normal state
                led_state_override = false;
                ESP_LOGI(TAG, "LED reset sequence complete, returning to normal state");
            }
            
            led_state_timer += 50; // Update every 50ms
        } else if (current_led_state == LED_STATE_STROBE) {
            // Handle strobe pattern (slow blinking dim)
            uint32_t strobe_phase = led_state_timer % LED_STROBE_PERIOD_MS;
            if (strobe_phase < LED_STROBE_PERIOD_MS / 2) {
                led_set_brightness(LED_BRIGHTNESS_DIM);
            } else {
                led_set_brightness(LED_BRIGHTNESS_OFF);
            }
            led_state_timer += 50; // Update every 50ms
        } else if (!led_state_override) {
            // Handle static states
            switch (current_led_state) {
                case LED_STATE_OFF:
                    led_set_brightness(LED_BRIGHTNESS_OFF);
                    break;
                case LED_STATE_PAIRED:
                    led_set_brightness(LED_BRIGHTNESS_DIM);
                    break;
                case LED_STATE_TRANSMITTING:
                    led_set_brightness(LED_BRIGHTNESS_BRIGHT);
                    break;
                default:
                    // Do nothing for other states
                    break;
            }
        }
        
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(50)); // 50ms update rate
    }
} 