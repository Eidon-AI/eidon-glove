/*
 * BNO085 wrapper using esp32_bno08x_driver library
 * Much simpler implementation using the proven library
 */

#include "imu.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "IMU";

// Static variables
static BNO08x imu;
static bool sensor_initialized = false;
static bool new_data_available = false;
static imu_quaternion_t latest_quaternion = {0};
// static int callback_count = 0;

// Data callback function for BNO08x
static void imu_data_callback(void *arg) {
    BNO08x *device = (BNO08x *)arg;
    
    // Get quaternion data
    float i, j, k, real, rad_accuracy;
    uint8_t accuracy;
    BNO08x_get_quat(device, &i, &j, &k, &real, &rad_accuracy, &accuracy);
    
    // Store in our format
    latest_quaternion.i = i;
    latest_quaternion.j = j;
    latest_quaternion.k = k;
    latest_quaternion.real = real;
    new_data_available = true;
    
    // Log every 100 callbacks to show it's working
    // if (++callback_count % 100 == 0) {
    //     ESP_LOGI(TAG, "IMU callback #%d - Quaternion: w=%.3f, x=%.3f, y=%.3f, z=%.3f", 
    //              callback_count, real, i, j, k);
    // }
}

// Configure PS0/PS1 pins for SPI mode
static void configure_interface_pins(void) {
    // Configure PS0/PS1 pins for SPI mode
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BNO08X_PS0) | (1ULL << BNO08X_PS1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // For SPI mode: PS1=1, PS0=1 (PS0 becomes WAKE in SPI mode)
    gpio_set_level(BNO08X_PS0, 1);
    gpio_set_level(BNO08X_PS1, 1);
    
    ESP_LOGI(TAG, "Configured PS0/PS1 pins for SPI mode");
}

// Public functions
esp_err_t bno08x_init(void) {
    ESP_LOGI(TAG, "Initializing BNO085 using esp32_bno08x_driver library");
    
    // Configure interface pins for SPI mode
    configure_interface_pins();
    
    // Give BNO085 time to boot
    ESP_LOGI(TAG, "Waiting for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Configure BNO08x with our pin mappings
    BNO08x_config_t cfg = {
        .spi_peripheral = SPI_HOST,
        .io_mosi = SPI_MOSI,
        .io_miso = SPI_MISO,
        .io_sclk = SPI_SCLK,
        .io_cs = SPI_CS,
        .io_int = BNO08X_INT,
        .io_rst = BNO08X_RST,
        .io_wake = BNO08X_PS0,  // WAKE pin (PS0 in SPI mode)
        .sclk_speed = SPI_FREQ,
        .cpu_spi_intr_affinity = 0  // CPU 0
    };
    
    // Initialize the BNO08x driver
    BNO08x_init(&imu, &cfg);
    
    // Initialize the BNO08x device
    if (!BNO08x_initialize(&imu)) {
        ESP_LOGE(TAG, "Failed to initialize BNO08x device");
        return ESP_FAIL;
    }
    
    // Register data callback
    BNO08x_register_cb(&imu, imu_data_callback);
    
    sensor_initialized = true;
    ESP_LOGI(TAG, "BNO085 initialized successfully using esp32_bno08x_driver");
    
    return ESP_OK;
}

void bno08x_deinit(void) {
    if (sensor_initialized) {
        // The library doesn't provide a deinit function, so we'll just mark as uninitialized
        sensor_initialized = false;
        ESP_LOGI(TAG, "BNO085 deinitialized");
    }
}

esp_err_t bno08x_enable_game_rotation_vector(uint32_t period_us) {
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "BNO085 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Enabling game rotation vector with period %lu us", (unsigned long)period_us);
    
    // Enable game rotation vector reports
    BNO08x_enable_game_rotation_vector(&imu, period_us);
    
    ESP_LOGI(TAG, "Game rotation vector enabled");
    return ESP_OK;
}

esp_err_t bno08x_service(void) {
    // Not needed with callback-based approach, but keep for compatibility
    return ESP_OK;
}

esp_err_t bno08x_get_quaternion(imu_quaternion_t *quat) {
    if (!sensor_initialized || !quat) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!new_data_available) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Copy the latest quaternion data
    memcpy(quat, &latest_quaternion, sizeof(imu_quaternion_t));
    new_data_available = false;
    
    return ESP_OK;
}

bool bno08x_has_new_quaternion(void) {
    return new_data_available;
}

bool bno08x_was_reset(void) {
    // The library handles resets internally
    return false;
}

void bno08x_transform_coordinate_system(imu_quaternion_t *quat) {
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

esp_err_t bno08x_reset(void) {
    ESP_LOGI(TAG, "Resetting IMU...");
    
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "IMU not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Perform hardware reset
    BNO08x_hard_reset(&imu);
    
    // Wait for reset to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Re-initialize the device
    if (!BNO08x_initialize(&imu)) {
        ESP_LOGE(TAG, "Failed to re-initialize BNO08x after reset");
        return ESP_FAIL;
    }
    
    // Re-register data callback
    BNO08x_register_cb(&imu, imu_data_callback);
    
    // Clear any stale data
    new_data_available = false;
    
    // Re-enable game rotation vector at 400Hz (matching main.c configuration)
    BNO08x_enable_game_rotation_vector(&imu, 2500);  // 400Hz = 2500us
    
    ESP_LOGI(TAG, "IMU reset completed successfully");
    return ESP_OK;
} 