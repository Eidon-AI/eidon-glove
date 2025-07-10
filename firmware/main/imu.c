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
#include "config.h"
#include "hid_reports.h"
#include "bno08x_driver.h"

static const char *TAG = "IMU";

// Static variables
static BNO08x imu;
static bool sensor_initialized = false;
static bool new_data_available = false;
static imu_quaternion_t latest_quaternion = {0, 0, 0, 1.0f}; // Initialize with forward quaternion
static TickType_t last_tare_time = 0;  // Track last tare operation time

// Minimum time between tare operations (in milliseconds)
#define TARE_COOLDOWN_MS 500

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
}

// Configure PS0/PS1 pins for SPI mode
static void configure_interface_pins(void) {
    // Configure PS0/PS1 pins for SPI mode
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IMU_PS0) | (1ULL << IMU_PS1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // For SPI mode: PS1=1, PS0=1 (PS0 becomes WAKE in SPI mode)
    gpio_set_level(IMU_PS0, 1);
    gpio_set_level(IMU_PS1, 1);
    
    ESP_LOGI(TAG, "Configured PS0/PS1 pins for SPI mode");
}

// Public functions
esp_err_t imu_init(void) {
    ESP_LOGI(TAG, "Initializing BNO085 using esp32_bno08x_driver library");
    
    // Configure interface pins for SPI mode
    configure_interface_pins();
    
    // Give BNO085 time to boot
    ESP_LOGI(TAG, "Waiting for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Configure BNO08x with our pin mappings
    BNO08x_config_t cfg = {
        .spi_peripheral = IMU_SPI_HOST,
        .io_mosi = IMU_SPI_MOSI,
        .io_miso = IMU_SPI_MISO,
        .io_sclk = IMU_SPI_SCLK,
        .io_cs = IMU_SPI_CS,
        .io_int = IMU_INT,
        .io_rst = IMU_RST,
        .io_wake = IMU_PS0,  // WAKE pin (PS0 in SPI mode)
        .sclk_speed = IMU_SPI_FREQ,
        .cpu_spi_intr_affinity = 0  // CPU 0
    };
    
    // Initialize the BNO08x driver
    BNO08x_init(&imu, &cfg);
    
    // Try to initialize the BNO08x device with retries
    const int max_retries = 3;
    const int retry_delay_ms = 500;
    
    for (int retry = 0; retry < max_retries; retry++) {
        ESP_LOGI(TAG, "Attempting to initialize BNO08x (attempt %d/%d)...", retry + 1, max_retries);
        
        if (BNO08x_initialize(&imu)) {
            // Success!
            ESP_LOGI(TAG, "BNO08x initialization successful on attempt %d", retry + 1);
            
            // Register data callback
            BNO08x_register_cb(&imu, imu_data_callback);
            
            sensor_initialized = true;
            ESP_LOGI(TAG, "BNO085 initialized successfully using esp32_bno08x_driver");
            
            return ESP_OK;
        }
        
        ESP_LOGW(TAG, "BNO08x initialization failed on attempt %d", retry + 1);
        
        if (retry < max_retries - 1) {
            // Not the last attempt, try resetting the device
            ESP_LOGI(TAG, "Performing hardware reset before retry...");
            
            // Toggle reset pin if available
            if (IMU_RST != GPIO_NUM_NC) {
                gpio_set_level(IMU_RST, 0);
                vTaskDelay(pdMS_TO_TICKS(10));
                gpio_set_level(IMU_RST, 1);
            }
            
            // Wait before next attempt
            ESP_LOGI(TAG, "Waiting %d ms before retry...", retry_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }
    }
    
    // All retries failed
    ESP_LOGE(TAG, "Failed to initialize BNO08x device after %d attempts", max_retries);
    return ESP_FAIL;
}

void imu_deinit(void) {
    if (sensor_initialized) {
        // The library doesn't provide a deinit function, so we'll just mark as uninitialized
        sensor_initialized = false;
        ESP_LOGI(TAG, "BNO085 deinitialized");
    }
}

esp_err_t imu_enable_game_rotation_vector(uint32_t period_us) {
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "BNO085 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Enabling ARVR stabilized game rotation vector with period %lu us", (unsigned long)period_us);
    
    // Enable ARVR stabilized game rotation vector reports for more stable output
    BNO08x_enable_ARVR_stabilized_game_rotation_vector(&imu, period_us);
    
    ESP_LOGI(TAG, "ARVR stabilized game rotation vector enabled");
    return ESP_OK;
}

esp_err_t imu_get_quaternion(imu_quaternion_t *quat) {
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

bool imu_has_new_quaternion(void) {
    return new_data_available;
}

void imu_transform_coordinate_system(imu_quaternion_t *quat) {
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

// IMU task for continuous sensor reading
void imu_task(void *pvParameters) {
    (void) pvParameters;
    ESP_LOGI(TAG, "IMU task started - Polling: %dHz, Sensor: %dHz", IMU_POLL_FREQ_HZ, IMU_SENSOR_FREQ_HZ);
    
    // Initialize IMU
    esp_err_t ret = imu_init();
    bool imu_failed = false;
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BNO085 - continuing with identity quaternion");
        imu_failed = true;
        // Set identity quaternion (facing forward, no rotation)
        hid_device_update_quaternion(0, 0, 0, 1.0f);
    } else {
        ESP_LOGI(TAG, "BNO085 initialized successfully, starting sensor loop");
        
        // Enable game rotation vector at configured frequency
        ESP_LOGI(TAG, "About to enable game rotation vector at %dHz", IMU_SENSOR_FREQ_HZ);
        ret = imu_enable_game_rotation_vector(IMU_SENSOR_PERIOD_US);  // Period in microseconds
        ESP_LOGI(TAG, "imu_enable_game_rotation_vector returned: %d", ret);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable game rotation vector - continuing with identity quaternion");
            imu_deinit();
            imu_failed = true;
            // Set identity quaternion
            hid_device_update_quaternion(0, 0, 0, 1.0f);
        } else {
            // Give the sensor time to process configuration and start sending data
            ESP_LOGI(TAG, "Waiting for sensor to start sending data...");
            vTaskDelay(pdMS_TO_TICKS(100));  // Reduced from 500ms to 100ms
            
            ESP_LOGI(TAG, "Starting sensor monitoring loop");
        }
    }
    
    // Tracking variables
    imu_quaternion_t quat;
    uint32_t no_data_count = 0;
    uint32_t retry_init_counter = 0;
    const uint32_t retry_init_interval = 5000 / IMU_POLL_DELAY_MS; // Retry every 5 seconds
    
    while (1) {
        if (imu_failed) {
            // If IMU failed, periodically try to reinitialize
            retry_init_counter++;
            if (retry_init_counter >= retry_init_interval) {
                retry_init_counter = 0;
                ESP_LOGI(TAG, "Attempting to reinitialize IMU...");
                
                ret = imu_init();
                if (ret == ESP_OK) {
                    ret = imu_enable_game_rotation_vector(IMU_SENSOR_PERIOD_US);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "IMU reinitialized successfully!");
                        imu_failed = false;
                        vTaskDelay(pdMS_TO_TICKS(100));
                    } else {
                        imu_deinit();
                    }
                }
            }
            
            // Continue sending identity quaternion while IMU is failed
            vTaskDelay(pdMS_TO_TICKS(HID_REPORT_DELAY_MS));
            continue;
        }
        
        // Try to get quaternion data
        ret = imu_get_quaternion(&quat);
        if (ret == ESP_OK) {
            // Apply coordinate transformation
            imu_transform_coordinate_system(&quat);
            
            // Update the global sensor report with new quaternion data
            hid_device_update_quaternion(quat.i, quat.j, quat.k, quat.real);
            
            no_data_count = 0;  // Reset counter on successful read
            // Short delay when actively receiving data
            vTaskDelay(pdMS_TO_TICKS(IMU_POLL_DELAY_MS));
        } else {
            // No new data available
            no_data_count++;
            
            // If no data for too long, assume sensor failure
            if (no_data_count > 500) { // ~5 seconds at 100Hz
                ESP_LOGE(TAG, "No data from IMU for 5 seconds, assuming sensor failure");
                imu_failed = true;
                no_data_count = 0;
                // Set identity quaternion
                hid_device_update_quaternion(0, 0, 0, 1.0f);
            }
            
            // Longer delay when no data to avoid busy-waiting
            if (no_data_count > 10) {
                vTaskDelay(pdMS_TO_TICKS(HID_REPORT_DELAY_MS));  // Back off to HID rate when idle
            } else {
                vTaskDelay(pdMS_TO_TICKS(IMU_POLL_DELAY_MS));   // Use polling rate
            }
        }
    }
}

esp_err_t imu_reset(void) {
    ESP_LOGI(TAG, "Resetting IMU...");
    
    if (!sensor_initialized) {
        ESP_LOGW(TAG, "IMU not initialized - attempting initialization instead");
        return imu_init();
    }
    
    // Perform hardware reset
    BNO08x_hard_reset(&imu);
    
    // Wait for reset to complete
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Re-initialize the device with retries
    const int max_retries = 3;
    bool reset_success = false;
    
    for (int retry = 0; retry < max_retries; retry++) {
        ESP_LOGI(TAG, "Attempting to re-initialize after reset (attempt %d/%d)...", retry + 1, max_retries);
        
        if (BNO08x_initialize(&imu)) {
            reset_success = true;
            break;
        }
        
        if (retry < max_retries - 1) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    
    if (!reset_success) {
        ESP_LOGE(TAG, "Failed to re-initialize BNO08x after reset");
        sensor_initialized = false;
        return ESP_FAIL;
    }
    
    // Re-register data callback
    BNO08x_register_cb(&imu, imu_data_callback);
    
    // Clear any stale data
    new_data_available = false;
    
    // Re-enable game rotation vector at configured frequency
    BNO08x_enable_game_rotation_vector(&imu, IMU_SENSOR_PERIOD_US);
    
    ESP_LOGI(TAG, "IMU reset completed successfully");
    return ESP_OK;
}

esp_err_t imu_tare_heading(void) {
    ESP_LOGI(TAG, "Taring IMU heading (yaw axis only)...");
    
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "IMU not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if enough time has passed since last tare
    TickType_t current_time = xTaskGetTickCount();
    TickType_t time_since_last_tare = (current_time - last_tare_time) * portTICK_PERIOD_MS;
    
    if (last_tare_time != 0 && time_since_last_tare < TARE_COOLDOWN_MS) {
        ESP_LOGW(TAG, "Tare cooldown active. Please wait %lu ms before next tare.", 
                 (unsigned long)(TARE_COOLDOWN_MS - time_since_last_tare));
        return ESP_ERR_INVALID_STATE;
    }
    
    // Get current yaw before tare (for debugging)
    float yaw_before = BNO08x_get_yaw_deg(&imu);
    ESP_LOGI(TAG, "Yaw before tare: %.2f degrees", yaw_before);
    
    // Tare only the Z axis (yaw/heading) - use game rotation vector basis for tare
    // Note: Even though we're using ARVR stabilized reports, tare uses game rotation vector basis
    BNO08x_tare_now(&imu, TARE_AXIS_Z, TARE_GAME_ROTATION_VECTOR);
    
    // Update last tare time
    last_tare_time = current_time;
    
    // Give the sensor a bit more time to process the tare command
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Get yaw after tare (for debugging)
    float yaw_after = BNO08x_get_yaw_deg(&imu);
    ESP_LOGI(TAG, "Yaw after tare: %.2f degrees", yaw_after);
    
    ESP_LOGI(TAG, "IMU heading tared successfully");
    return ESP_OK;
}

esp_err_t imu_tare_all_axes(void) {
    ESP_LOGI(TAG, "Taring all IMU axes (roll, pitch, yaw)...");
    
    if (!sensor_initialized) {
        ESP_LOGE(TAG, "IMU not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if enough time has passed since last tare
    TickType_t current_time = xTaskGetTickCount();
    TickType_t time_since_last_tare = (current_time - last_tare_time) * portTICK_PERIOD_MS;
    
    if (last_tare_time != 0 && time_since_last_tare < TARE_COOLDOWN_MS) {
        ESP_LOGW(TAG, "Tare cooldown active. Please wait %lu ms before next tare.", 
                 (unsigned long)(TARE_COOLDOWN_MS - time_since_last_tare));
        return ESP_ERR_INVALID_STATE;
    }
    
    // Tare all axes - use game rotation vector basis for tare
    // Note: Even though we're using ARVR stabilized reports, tare uses game rotation vector basis
    BNO08x_tare_now(&imu, TARE_AXIS_ALL, TARE_GAME_ROTATION_VECTOR);
    
    // Update last tare time
    last_tare_time = current_time;
    
    // Give the sensor time to process
    vTaskDelay(pdMS_TO_TICKS(50));
    
    ESP_LOGI(TAG, "All IMU axes tared successfully");
    return ESP_OK;
} 