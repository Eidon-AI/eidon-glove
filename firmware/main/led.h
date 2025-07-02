#ifndef LED_H
#define LED_H

#include "esp_err.h"
#include <stdint.h>

// LED state management
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_STROBE,      // Slow strobe: device on but not paired
    LED_STATE_PAIRED,      // Solid dim: paired but not transmitting
    LED_STATE_TRANSMITTING, // Solid bright: paired and transmitting
    LED_STATE_RESET        // Quick three flash sequence
} led_state_t;

// Function declarations
esp_err_t led_init(void);
void led_set_brightness(uint32_t brightness);
void led_set_state(led_state_t state);
void led_trigger_reset_sequence(void);
void led_task(void *pvParameters);

#endif // LED_H 