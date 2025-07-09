#ifndef IMU_H
#define IMU_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bno08x_driver.h"

// Interface mode selection
// Set to 1 for SPI mode, 0 for I2C mode
#define BNO08X_MODE_SPI 1

// Pin mapping for Seeed XIAO ESP32-C6
#define SPI_MISO    20   // D9  - GPIO 20 for MISO
#define SPI_MOSI    18   // D10 - GPIO 18 for MOSI
#define SPI_SCLK    19   // D8  - GPIO 19 for SCLK
#define SPI_CS      17   // D7  - GPIO 17 for CS (manual control)
#define BNO08X_PS0  2    // D2  - GPIO 2  for PS0/WAKE (controls I2C addr in I2C mode, WAKE in SPI mode)
#define BNO08X_PS1  21   // D3  - GPIO 21 for PS1 (protocol selection)
#define BNO08X_INT  22   // D4  - GPIO 22 for H_INTN (active low interrupt)
#define BNO08X_RST  23   // D5  - GPIO 23 for RESET

// Protocol Selection (PS0/PS1) per BNO085 datasheet:
// PS1=0, PS0=0: UART-RVC mode
// PS1=0, PS0=1: I2C mode (address 0x4A)
// PS1=1, PS0=0: UART-SHTP mode  
// PS1=1, PS0=1: SPI mode (PS0 becomes WAKE)
// Note: PS0=floating in I2C mode gives address 0x4B

// I2C pin mapping for Seeed XIAO ESP32-C6 (shared with SPI pins)
#define I2C_SDA     SPI_MISO   // D9  - GPIO 20 (shared with SPI MISO)
#define I2C_SCL     SPI_SCLK   // D8  - GPIO 19 (shared with SPI SCLK)
#define I2C_ADR     SPI_MOSI   // D10 - GPIO 18 (shared with SPI MOSI, controls I2C address)
#define I2C_PORT    I2C_NUM_0
#define I2C_FREQ    100000  // Back to original 100kHz - it was working before
#define I2C_ADDR    0x4B  // Default I2C address when ADR pin is floating

// SPI Configuration
#define SPI_HOST    SPI2_HOST
#define SPI_DMA_CH  SPI_DMA_CH_AUTO
#define SPI_FREQ    3000000  // 3MHz SPI frequency (maximum supported by BNO085)
#define SPI_MODE    3        // SPI mode 3 (CPOL=1, CPHA=1) per BNO085 datasheet

// SHTP constants
#define SHTP_MAX_TRANSFER_SIZE 300

// Quaternion structure (compatible with sh2_RotationVector_t)
typedef struct {
    float i;     // x component
    float j;     // y component
    float k;     // z component
    float real;  // w component
} imu_quaternion_t;

// Initialize the BNO085 sensor
esp_err_t bno08x_init(void);

// Deinitialize the BNO085 sensor
void bno08x_deinit(void);

// Enable game rotation vector reports
esp_err_t bno08x_enable_game_rotation_vector(uint32_t period_us);

// Service the sensor (must be called regularly)
esp_err_t bno08x_service(void);

// Get the latest quaternion data
esp_err_t bno08x_get_quaternion(imu_quaternion_t *quat);

// Check if new quaternion data is available
bool bno08x_has_new_quaternion(void);

// Check if a reset has occurred
bool bno08x_was_reset(void);

// Apply coordinate system transformation to fix yaw/pitch swapping
// This transforms from BNO085's coordinate system to the expected application coordinate system
void bno08x_transform_coordinate_system(imu_quaternion_t *quat);

// Reset the IMU sensor (hardware reset via I2C)
esp_err_t bno08x_reset(void);

#endif //IMU_H 