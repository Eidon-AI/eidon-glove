#include "bno085_sh2.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <inttypes.h>  // For PRIu32
#include "sh2_err.h"   // For SH2_OK
#include "driver/gpio.h"

static const char *TAG = "BNO085_SH2";

// I2C Configuration
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TIMEOUT_MS 100
#define BNO085_I2C_ADDR 0x4B

// BNO085 Register addresses
#define BNO085_CHIP_ID_REG 0x00
#define BNO085_CHIP_ID_VAL 0xA0  // Expected chip ID for BNO085

// Static variables
static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t i2c_dev = NULL;
static sh2_Hal_t hal;
static bool sensor_initialized = false;
static bool new_quaternion_available = false;
static bno085_quat_t latest_quaternion = {0};

// Forward declarations for HAL functions
static int hal_open(sh2_Hal_t *self);
static void hal_close(sh2_Hal_t *self);
static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us);
static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len);
static uint32_t hal_getTimeUs(sh2_Hal_t *self);



// Sensor event callback
static void sensor_event_callback(void *cookie, sh2_SensorEvent_t *event)
{
    if (!event) {
        return;
    }
    
    // Check if this is a game rotation vector report
    if (event->reportId == SH2_GAME_ROTATION_VECTOR) {
        // Decode the sensor event
        sh2_SensorValue_t value;
        int rc = sh2_decodeSensorEvent(&value, event);
        if (rc == SH2_OK && value.sensorId == SH2_GAME_ROTATION_VECTOR) {
            // Update our quaternion data
            latest_quaternion.real = value.un.gameRotationVector.real;
            latest_quaternion.i = value.un.gameRotationVector.i;
            latest_quaternion.j = value.un.gameRotationVector.j;
            latest_quaternion.k = value.un.gameRotationVector.k;
            latest_quaternion.accuracy = value.status & 0x03;
            new_quaternion_available = true;
            
            // Log quaternion data at INFO level so it's visible
            // ESP_LOGI(TAG, "Quaternion: w=%.3f, x=%.3f, y=%.3f, z=%.3f (acc=%d)", 
            //          latest_quaternion.real, latest_quaternion.i, 
            //          latest_quaternion.j, latest_quaternion.k, latest_quaternion.accuracy);
        }
    }
}

// HAL implementation
static int hal_open(sh2_Hal_t *self)
{
    ESP_LOGI(TAG, "HAL Open called");
    
    // Configure I2C bus - simplified and direct
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = GPIO_NUM_20,
        .scl_io_num = GPIO_NUM_18,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return -1;
    }
    
    // Configure device - use standard settings
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
        return -1;
    }
    
    ESP_LOGI(TAG, "HAL Open complete");
    return 0;
}

static void hal_close(sh2_Hal_t *self)
{
    ESP_LOGI(TAG, "HAL Close");
    
    if (i2c_dev) {
        i2c_master_bus_rm_device(i2c_dev);
        i2c_dev = NULL;
    }
    
    if (i2c_bus) {
        i2c_del_master_bus(i2c_bus);
        i2c_bus = NULL;
    }
}

static int hal_read(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len, uint32_t *t_us)
{
    if (!i2c_dev || !pBuffer || len == 0) {
        return 0;
    }
    
    // Get timestamp
    *t_us = (uint32_t)esp_timer_get_time();
    
    // Simple I2C read - let SH2 library handle the protocol
    esp_err_t ret = i2c_master_receive(i2c_dev, pBuffer, len, 20);  // 20ms timeout
    
    if (ret == ESP_OK) {
        return len;
    } else if (ret == ESP_ERR_TIMEOUT) {
        // No data available - this is normal
        return 0;
    } else {
        // Other error - only log occasionally to avoid spam
        static int error_count = 0;
        error_count++;
        
        if ((error_count % 100) == 1) {  // Log every 100th error
            ESP_LOGD(TAG, "HAL read error (count=%d): %s", error_count, esp_err_to_name(ret));
        }
        return 0;
    }
}

static int hal_write(sh2_Hal_t *self, uint8_t *pBuffer, unsigned len)
{
    if (!i2c_dev || !pBuffer || len == 0) {
        return 0;
    }
    
    esp_err_t ret = i2c_master_transmit(i2c_dev, pBuffer, len, I2C_MASTER_TIMEOUT_MS);
    
    if (ret != ESP_OK) {
        // Only log errors occasionally to avoid spam
        static int error_count = 0;
        error_count++;
        if ((error_count % 100) == 1) {  // Log every 100th error
            ESP_LOGE(TAG, "HAL write failed (count=%d): %s", error_count, esp_err_to_name(ret));
        }
        return 0;
    }
    
    return len;
}

static uint32_t hal_getTimeUs(sh2_Hal_t *self)
{
    return (uint32_t)esp_timer_get_time();
}

// Public functions
esp_err_t bno085_sh2_init(void)
{
    ESP_LOGI(TAG, "Initializing BNO085 with SH2 library");
    
    // Give BNO085 time to boot
    ESP_LOGI(TAG, "Waiting 2 seconds for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Set up HAL functions
    hal.open = hal_open;
    hal.close = hal_close;
    hal.read = hal_read;
    hal.write = hal_write;
    hal.getTimeUs = hal_getTimeUs;
    
    // Initialize SH2 library
    ESP_LOGI(TAG, "Opening SH2 library");
    int status = sh2_open(&hal, NULL, NULL);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to open SH2: %d", status);
        return ESP_FAIL;
    }
    
    // Register sensor callback
    status = sh2_setSensorCallback(sensor_event_callback, NULL);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to set sensor callback: %d", status);
        sh2_close();
        return ESP_FAIL;
    }
    
    // Send device reset
    ESP_LOGI(TAG, "Sending device reset...");
    status = sh2_devReset();
    if (status != SH2_OK) {
        ESP_LOGW(TAG, "Device reset returned: %d", status);
    }
    
    // Wait for device to reset
    vTaskDelay(pdMS_TO_TICKS(300));
    
    // Service the device to handle any reset responses
    for (int i = 0; i < 10; i++) {
        sh2_service();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Get product IDs
    sh2_ProductIds_t prodIds;
    status = sh2_getProdIds(&prodIds);
    if (status == SH2_OK && prodIds.numEntries > 0) {
        ESP_LOGI(TAG, "BNO085 initialized successfully!");
        ESP_LOGI(TAG, "SW Part Number: %" PRIu32, prodIds.entry[0].swPartNumber);
        ESP_LOGI(TAG, "SW Version: %d.%d.%d", 
                 prodIds.entry[0].swVersionMajor,
                 prodIds.entry[0].swVersionMinor,
                 prodIds.entry[0].swVersionPatch);
        ESP_LOGI(TAG, "BNO085 ready to receive sensor data!");
    } else {
        ESP_LOGW(TAG, "Could not get product IDs, status: %d", status);
        ESP_LOGI(TAG, "BNO085 initialized but product info unavailable");
    }
    
    sensor_initialized = true;
    return ESP_OK;
}

void bno085_sh2_deinit(void)
{
    if (sensor_initialized) {
        sh2_close();
        sensor_initialized = false;
    }
}

esp_err_t bno085_sh2_enable_game_rotation_vector(uint32_t period_us)
{
    if (!sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Enabling game rotation vector with period %" PRIu32 " us", period_us);
    
    sh2_SensorConfig_t config = {
        .changeSensitivityEnabled = false,
        .changeSensitivityRelative = false,
        .wakeupEnabled = false,
        .alwaysOnEnabled = false,
        .sniffEnabled = false,
        .changeSensitivity = 0,
        .reportInterval_us = period_us,
        .batchInterval_us = 0,
        .sensorSpecific = 0
    };
    
    int status = sh2_setSensorConfig(SH2_GAME_ROTATION_VECTOR, &config);
    if (status != SH2_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector: %d", status);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Game rotation vector enabled successfully");
    return ESP_OK;
}

esp_err_t bno085_sh2_service(void)
{
    if (!sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Service the SH2 library - this will read data and call our callback
    sh2_service();
    
    return ESP_OK;
}

esp_err_t bno085_sh2_get_quaternion(bno085_quat_t *quat)
{
    if (!quat) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *quat = latest_quaternion;
    new_quaternion_available = false;
    return ESP_OK;
}

bool bno085_sh2_has_new_quaternion(void)
{
    return new_quaternion_available;
}