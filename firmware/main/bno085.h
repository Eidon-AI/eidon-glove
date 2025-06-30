#ifndef BNO085_H
#define BNO085_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_types.h"

// I2C Address
#define BNO085_I2C_ADDR_DEFAULT 0x4B

// SHTP (Sensor Hub Transport Protocol) constants
#define SHTP_HEADER_SIZE        4
#define SHTP_MAX_TRANSFER_SIZE  300

// SHTP channels
#define SHTP_CHANNEL_COMMAND      0
#define SHTP_CHANNEL_EXECUTABLE   1
#define SHTP_CHANNEL_CONTROL      2
#define SHTP_CHANNEL_REPORTS      3
#define SHTP_CHANNEL_WAKE_REPORTS 4
#define SHTP_CHANNEL_GYRO         5

// SHTP Report IDs
#define SHTP_REPORT_COMMAND_REQUEST      0xF2
#define SHTP_REPORT_COMMAND_RESPONSE     0xF1
#define SHTP_REPORT_FRS_READ_REQUEST     0xF3
#define SHTP_REPORT_FRS_READ_RESPONSE    0xF3
#define SHTP_REPORT_PRODUCT_ID_REQUEST   0xF9
#define SHTP_REPORT_PRODUCT_ID_RESPONSE  0xF8
#define SHTP_REPORT_BASE_TIMESTAMP       0xFB
#define SHTP_REPORT_SET_FEATURE_COMMAND  0xFD
#define SHTP_REPORT_GET_FEATURE_REQUEST  0xFE
#define SHTP_REPORT_GET_FEATURE_RESPONSE 0xFC

// BNO085 Sensor Report IDs
#define REPORT_ROTATION_VECTOR              0x05
#define SENSOR_REPORTID_GAME_ROTATION_VECTOR 0x08

// BNO085 configuration structure
typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    int sda_pin;
    int scl_pin;
    uint32_t i2c_freq;
} bno085_config_t;

// Quaternion structure
typedef struct {
    float real;  // w component
    float i;     // x component
    float j;     // y component
    float k;     // z component
    uint8_t accuracy;
} bno085_quaternion_t;

// Initialize the BNO085
esp_err_t bno085_init(const bno085_config_t *config);

// Deinitialize the BNO085
void bno085_deinit(void);

// Soft reset the BNO085
esp_err_t bno085_soft_reset(void);

// Enable rotation vector reports
esp_err_t bno085_enable_rotation_vector(uint32_t time_between_reports_us);

// Enable game rotation vector reports (no magnetometer)
esp_err_t bno085_enable_game_rotation_vector(uint32_t time_between_reports_ms);

// Check if new data is available
bool bno085_data_available(void);

// Check if BNO085 is available
bool bno085_is_available(void);

// Get the latest quaternion data
esp_err_t bno085_get_quaternion(bno085_quaternion_t *quat);

// Get rotation vector data (alias for get_quaternion)
esp_err_t bno085_get_rotation_vector(bno085_quaternion_t *quat);

// Convert quaternion to 16-bit values for HID report (0-65535 range)
void bno085_quaternion_to_hid(const bno085_quaternion_t *quat, uint16_t *hid_data);

// Test raw I2C read
esp_err_t bno085_test_raw_read(void);

#endif // BNO085_H 