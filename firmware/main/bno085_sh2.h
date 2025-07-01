#ifndef BNO085_SH2_H
#define BNO085_SH2_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sh2.h"
#include "sh2_SensorValue.h"

// BNO085 quaternion structure
typedef struct {
    float real;  // w component
    float i;     // x component  
    float j;     // y component
    float k;     // z component
    uint8_t accuracy;
} bno085_quat_t;

// Initialize the BNO085 sensor using SH2 library
esp_err_t bno085_sh2_init(void);

// Deinitialize the BNO085 sensor
void bno085_sh2_deinit(void);

// Enable game rotation vector reports
esp_err_t bno085_sh2_enable_game_rotation_vector(uint32_t period_us);

// Service the sensor (must be called regularly)
esp_err_t bno085_sh2_service(void);

// Get the latest quaternion data
esp_err_t bno085_sh2_get_quaternion(bno085_quat_t *quat);

// Check if new quaternion data is available
bool bno085_sh2_has_new_quaternion(void);

#endif // BNO085_SH2_H 