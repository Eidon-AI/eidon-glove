#ifndef ADAFRUIT_BNO085_H
#define ADAFRUIT_BNO085_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sh2_SensorValue.h"

// Initialize the BNO085 sensor using Adafruit-style approach
esp_err_t adafruit_bno08x_init(void);

// Deinitialize the BNO085 sensor
void adafruit_bno08x_deinit(void);

// Enable game rotation vector reports (exactly like Adafruit)
esp_err_t adafruit_bno08x_enable_game_rotation_vector(uint32_t period_us);

// Service the sensor (must be called regularly)
esp_err_t adafruit_bno08x_service(void);

// Get the latest quaternion data
esp_err_t adafruit_bno08x_get_quaternion(sh2_RotationVector_t *quat);

// Check if new quaternion data is available
bool adafruit_bno08x_has_new_quaternion(void);

// Check if a reset has occurred
bool adafruit_bno08x_was_reset(void);

// Apply coordinate system transformation to fix yaw/pitch swapping
// This transforms from BNO085's coordinate system to the expected application coordinate system
void adafruit_bno08x_transform_coordinate_system(sh2_RotationVector_t *quat);

#endif // ADAFRUIT_BNO085_H 