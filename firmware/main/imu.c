/*
 * BNO085 Adafruit-style C wrapper
 * Based on Adafruit_BNO08x library but rewritten in C for ESP-IDF
 */

#include "imu.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "sh2.h"
#include "sh2_SensorValue.h"
#include "sh2_err.h"

static const char *TAG = "IMU";

// I2C Configuration
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ I2C_FREQ
#define I2C_MASTER_TIMEOUT_MS 100
#define BNO085_I2C_ADDR I2C_ADDR

// Static variables
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t i2c_dev = NULL;
static sh2_Hal_t hal;
static bool sensor_initialized = false;
static bool new_quaternion_available = false;
static sh2_RotationVector_t latest_quaternion = {0};
static sh2_SensorValue_t sensor_value = {0};
static bool reset_occurred = false;

// Forward declarations for HAL functions
static int hal_open(sh2_Hal_t *self);
static void hal_close(sh2_Hal_t *self);
static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);
static uint32_t hal_getTimeUs(sh2_Hal_t *self);
static void hal_hardwareReset(void);

// Callback functions
static void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent);
static void sensorHandler(void *cookie, sh2_SensorEvent_t *event);

// Hardware reset function
static void hal_hardwareReset(void) {
    ESP_LOGI(TAG, "BNO085 Hardware reset");
    // For now, just delay to simulate reset
    // In a real implementation, you'd toggle a reset pin
    vTaskDelay(pdMS_TO_TICKS(100));
}

// HAL implementation - follows Adafruit's approach exactly
static int hal_open(sh2_Hal_t *self) {
    ESP_LOGI(TAG, "HAL Open called");
    
    // Send soft reset packet (exactly like Adafruit)
    uint8_t softreset_pkt[] = {5, 0, 1, 0, 1};
    bool success = false;
    
    for (uint8_t attempts = 0; attempts < 5; attempts++) {
        esp_err_t ret = i2c_master_transmit(i2c_dev, softreset_pkt, 5, I2C_MASTER_TIMEOUT_MS);
        if (ret == ESP_OK) {
            success = true;
            break;
        }
        ESP_LOGW(TAG, "Soft reset attempt %d failed: %s", attempts + 1, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    
    if (!success) {
        ESP_LOGE(TAG, "Failed to send soft reset packet");
        return -1;
    }
    
    // Wait for reset to complete (exactly like Adafruit)
    vTaskDelay(pdMS_TO_TICKS(300));
    
    ESP_LOGI(TAG, "HAL Open complete");
    return 0;
}

static void hal_close(sh2_Hal_t *self) {
    ESP_LOGI(TAG, "HAL Close");
}

static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us) {
    // Get timestamp
    *t_us = (uint32_t)esp_timer_get_time();
    
    // Validate input parameters
    if (pBuffer == NULL) {
        ESP_LOGD(TAG, "hal_read: pBuffer is NULL");
        return 0;
    }
    if (len == 0) {
        ESP_LOGD(TAG, "hal_read: len is 0");
        return 0;
    }
    
    // Read SHTP header (4 bytes) - exactly like Adafruit
    uint8_t header[4];
    esp_err_t ret = i2c_master_receive(i2c_dev, header, 4, I2C_MASTER_TIMEOUT_MS);
    if (ret == ESP_ERR_INVALID_ARG || ret == ESP_ERR_TIMEOUT) {
        // No data available - this is normal
        return 0;
    }
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read SHTP header: %s", esp_err_to_name(ret));
        return 0;
    }
    
    // Determine packet size (exactly like Adafruit)
    uint16_t packet_size = (uint16_t)header[0] | (uint16_t)header[1] << 8;
    // Unset the "continue" bit
    packet_size &= ~0x8000;
    
    ESP_LOGD(TAG, "SHTP packet size: %d, buffer size: %d", packet_size, len);
    
    if (packet_size == 0) {
        ESP_LOGD(TAG, "Packet size is 0, skipping");
        return 0;
    }
    
    if (packet_size > len) {
        ESP_LOGE(TAG, "Packet too large for buffer");
        return 0;
    }
    
    // Read the packet data
    ret = i2c_master_receive(i2c_dev, pBuffer, packet_size, I2C_MASTER_TIMEOUT_MS);
    if (ret == ESP_ERR_INVALID_ARG || ret == ESP_ERR_TIMEOUT) {
        // No data available for packet body - this can happen
        ESP_LOGD(TAG, "No packet data available");
        return 0;
    }
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Failed to read packet data: %s", esp_err_to_name(ret));
        return 0;
    }
    
    return packet_size;
}

static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len) {
    esp_err_t ret = i2c_master_transmit(i2c_dev, pBuffer, len, I2C_MASTER_TIMEOUT_MS);
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "HAL write failed: %s", esp_err_to_name(ret));
        return 0;
    }
    
    return len;
}

static uint32_t hal_getTimeUs(sh2_Hal_t *self) {
    return (uint32_t)esp_timer_get_time();
}

// Callback functions - exactly like Adafruit
static void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent) {
    // If we see a reset, set a flag so that sensors will be reconfigured
    if (pEvent->eventId == SH2_RESET) {
        ESP_LOGI(TAG, "BNO085 Reset detected");
        reset_occurred = true;
    }
}

static void sensorHandler(void *cookie, sh2_SensorEvent_t *event) {
    int rc;
    
    ESP_LOGD(TAG, "Got sensor event");
    
    rc = sh2_decodeSensorEvent(&sensor_value, event);
    if (rc != SH2_OK) {
        ESP_LOGE(TAG, "Error decoding sensor event");
        sensor_value.timestamp = 0;
        return;
    }
    
    // Check if this is a game rotation vector report
    if (sensor_value.sensorId == SH2_GAME_ROTATION_VECTOR) {
        // Update our quaternion data
        latest_quaternion.i = sensor_value.un.gameRotationVector.i;
        latest_quaternion.j = sensor_value.un.gameRotationVector.j;
        latest_quaternion.k = sensor_value.un.gameRotationVector.k;
        latest_quaternion.real = sensor_value.un.gameRotationVector.real;
        new_quaternion_available = true;
        
        ESP_LOGD(TAG, "Quaternion: w=%.3f, x=%.3f, y=%.3f, z=%.3f", 
                 latest_quaternion.real, latest_quaternion.i, 
                 latest_quaternion.j, latest_quaternion.k);
    }
}

// Public functions
esp_err_t adafruit_bno08x_init(void) {
    ESP_LOGI(TAG, "Initializing BNO085 with Adafruit-style wrapper");
    
    // Give BNO085 time to boot
    ESP_LOGI(TAG, "Waiting 2 seconds for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Configure I2C bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BNO085_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };
    
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
        return ret;
    }
    
    // Set up HAL functions (exactly like Adafruit)
    hal.open = hal_open;
    hal.close = hal_close;
    hal.read = hal_read;
    hal.write = hal_write;
    hal.getTimeUs = hal_getTimeUs;
    
    // Initialize SH2 library (exactly like Adafruit)
    int status = sh2_open(&hal, hal_callback, NULL);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to open SH2 interface: %d", status);
        i2c_master_bus_rm_device(i2c_dev);
        i2c_del_master_bus(i2c_bus);
        i2c_dev = NULL;
        i2c_bus = NULL;
        return ESP_FAIL;
    }
    
    // Check connection by getting product IDs (exactly like Adafruit)
    sh2_ProductIds_t prodIds;
    memset(&prodIds, 0, sizeof(prodIds));
    status = sh2_getProdIds(&prodIds);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to get product IDs: %d", status);
        sh2_close();
        i2c_master_bus_rm_device(i2c_dev);
        i2c_del_master_bus(i2c_bus);
        i2c_dev = NULL;
        i2c_bus = NULL;
        return ESP_FAIL;
    }
    if (prodIds.numEntries > 0) {
        ESP_LOGI(TAG, "BNO085 Product ID: SW Part: %lu, Ver: %u.%u.%u, Build: %lu", 
            (unsigned long)prodIds.entry[0].swPartNumber, 
            prodIds.entry[0].swVersionMajor, prodIds.entry[0].swVersionMinor, prodIds.entry[0].swVersionPatch, 
            (unsigned long)prodIds.entry[0].swBuildNumber);
    } else {
        ESP_LOGW(TAG, "No product ID entries found");
    }
    
    // Register sensor listener (exactly like Adafruit)
    sh2_setSensorCallback(sensorHandler, NULL);
    
    sensor_initialized = true;
    ESP_LOGI(TAG, "BNO085 initialized successfully with Adafruit-style wrapper");
    
    return ESP_OK;
}

void adafruit_bno08x_deinit(void) {
    if (sensor_initialized) {
        sh2_close();
        sensor_initialized = false;
    }
    
    if (i2c_dev) {
        i2c_master_bus_rm_device(i2c_dev);
        i2c_dev = NULL;
    }
    
    if (i2c_bus) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
}

esp_err_t adafruit_bno08x_enable_game_rotation_vector(uint32_t period_us) {
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "BNO085 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure sensor exactly like Adafruit
    sh2_SensorConfig_t config;
    
    // These sensor options are disabled or not used in most cases (exactly like Adafruit)
    config.changeSensitivityEnabled = false;
    config.wakeupEnabled = false;
    config.changeSensitivityRelative = false;
    config.alwaysOnEnabled = false;
    config.changeSensitivity = 0;
    config.batchInterval_us = 0;
    config.sensorSpecific = 0;
    
    config.reportInterval_us = period_us;
    
    int status = sh2_setSensorConfig(SH2_GAME_ROTATION_VECTOR, &config);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector: %d", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Game rotation vector enabled with period %lu us", (unsigned long)period_us);
    return ESP_OK;
}

esp_err_t adafruit_bno08x_service(void) {
    if (!sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Service the SH2 library (exactly like Adafruit)
    sh2_service();
    return ESP_OK;
}

esp_err_t adafruit_bno08x_get_quaternion(sh2_RotationVector_t *quat) {
    if (!sensor_initialized || !quat) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!new_quaternion_available) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Copy the latest quaternion data
    memcpy(quat, &latest_quaternion, sizeof(sh2_RotationVector_t));
    new_quaternion_available = false;
    
    return ESP_OK;
}

bool adafruit_bno08x_has_new_quaternion(void) {
    return new_quaternion_available;
}

bool adafruit_bno08x_was_reset(void) {
    bool x = reset_occurred;
    reset_occurred = false;
    return x;
}

// This transforms from BNO085's coordinate system to the expected application coordinate system
void adafruit_bno08x_transform_coordinate_system(sh2_RotationVector_t *quat) {
    if (!quat) {
        return;
    }
    
    // Store original values
    float w = quat->real;
    float x = quat->i;
    float y = quat->j;
    float z = quat->k;
    
    // Apply coordinate system transformation
    // This transformation swaps the coordinate axes to match expected orientation
    // From BNO085 coordinate system to application coordinate system
    quat->real = w;  // w stays the same
    quat->i = -x;    // x becomes -x (negate)
    quat->j = -y;    // y becomes -y (negate)
    quat->k = z;     // z stays the same
}

// Reset the IMU sensor (disable and re-enable game rotation vector)
esp_err_t adafruit_bno08x_reset(void) {
    ESP_LOGI(TAG, "Resetting IMU...");
    
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "IMU not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Disable game rotation vector report
    esp_err_t ret = adafruit_bno08x_enable_game_rotation_vector(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable game rotation vector: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait 100ms as in the original code
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Re-enable game rotation vector at 50Hz (20ms = 10000us)
    ret = adafruit_bno08x_enable_game_rotation_vector(10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-enable game rotation vector: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "IMU reset completed successfully");
    return ESP_OK;
} 