/*
 * BNO085 9-DOF IMU Driver
 * Based on Adafruit_BNO08x library implementation
 */

#include "bno085.h"
#include <string.h>
#include <math.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BNO085";

// Global I2C handles
static i2c_master_bus_handle_t g_i2c_bus = NULL;
static i2c_master_dev_handle_t g_i2c_dev = NULL;

// Sequence number for SHTP packets
static uint8_t sequence_number[6] = {0};

// Forward declarations for static functions
static esp_err_t shtp_send_packet(uint8_t channel, uint8_t *data, uint16_t length);
static esp_err_t shtp_receive_packet(uint8_t *channel, uint8_t *data, uint16_t *length, uint32_t timeout_ms);
static esp_err_t bno085_request_product_id(void);
static esp_err_t bno085_wait_for_product_id(uint32_t timeout_ms);

// Initialize the BNO085
esp_err_t bno085_init(const bno085_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing BNO085...");
    
    // Wait for BNO085 to boot
    ESP_LOGI(TAG, "Waiting for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Configure I2C master bus
    ESP_LOGI(TAG, "Configuring I2C bus: port=%d, SDA=%d, SCL=%d, freq=%ld", 
             config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_freq);
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = config->i2c_port,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    ESP_LOGI(TAG, "Creating I2C master bus...");
    esp_err_t ret = i2c_new_master_bus(&bus_config, &g_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add BNO085 device to the bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = config->i2c_freq,
        .scl_wait_us = 0,
    };
    
    ESP_LOGI(TAG, "Adding BNO085 device at address 0x%02X...", config->i2c_addr);
    ret = i2c_master_bus_add_device(g_i2c_bus, &dev_config, &g_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(g_i2c_bus);
        g_i2c_bus = NULL;
        return ret;
    }
    
    // Wait a bit after I2C setup
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Read and clear any initial advertisement packets
    ESP_LOGI(TAG, "Reading initial packets...");
    uint8_t dummy_data[300];
    uint16_t dummy_length;
    uint8_t dummy_channel;
    
    // Try to read initial packets (may timeout, that's OK)
    for (int i = 0; i < 5; i++) {
        esp_err_t read_ret = shtp_receive_packet(&dummy_channel, dummy_data, &dummy_length, 100);
        if (read_ret == ESP_OK) {
            ESP_LOGI(TAG, "Read initial packet %d: channel=%d, length=%d", i, dummy_channel, dummy_length);
            
            // Log first few bytes of data if available
            if (dummy_length > 0) {
                ESP_LOGI(TAG, "  Data[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X", 
                         dummy_data[0], 
                         dummy_length > 1 ? dummy_data[1] : 0,
                         dummy_length > 2 ? dummy_data[2] : 0,
                         dummy_length > 3 ? dummy_data[3] : 0,
                         dummy_length > 4 ? dummy_data[4] : 0,
                         dummy_length > 5 ? dummy_data[5] : 0,
                         dummy_length > 6 ? dummy_data[6] : 0,
                         dummy_length > 7 ? dummy_data[7] : 0);
            }
            
            // Check if this is a product ID response
            if (dummy_channel == SHTP_CHANNEL_CONTROL && dummy_length >= 2) {
                if (dummy_data[0] == SHTP_REPORT_PRODUCT_ID_RESPONSE) {
                    ESP_LOGI(TAG, "Received product ID response in initial packets!");
                    if (dummy_length >= 6) {
                        uint8_t sw_major = dummy_data[2];
                        uint8_t sw_minor = dummy_data[3];
                        uint16_t sw_build = dummy_data[4] | (dummy_data[5] << 8);
                        ESP_LOGI(TAG, "BNO085 Software Version: %d.%d.%d", sw_major, sw_minor, sw_build);
                    }
                }
            }
            
            // Also check channel 2 for command responses
            if (dummy_channel == 2 && dummy_length >= 2) {
                ESP_LOGI(TAG, "Channel 2 packet - might be command response: 0x%02X", dummy_data[0]);
            }
        } else if (read_ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Error reading initial packet: %s", esp_err_to_name(read_ret));
            break;
        }
    }
    
    // Send soft reset
    ESP_LOGI(TAG, "Sending soft reset...");
    ret = bno085_soft_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send soft reset: %s", esp_err_to_name(ret));
    }
    
    // Wait longer for reset to complete - BNO085 needs time
    ESP_LOGI(TAG, "Waiting for BNO085 to complete reset...");
    vTaskDelay(pdMS_TO_TICKS(500));  // Increased from 300ms
    
    // Read any packets after reset - look for product ID
    bool got_product_id = false;
    for (int i = 0; i < 10; i++) {  // Try more times
        esp_err_t read_ret = shtp_receive_packet(&dummy_channel, dummy_data, &dummy_length, 100);
        if (read_ret == ESP_OK) {
            ESP_LOGI(TAG, "Read post-reset packet %d: channel=%d, length=%d", i, dummy_channel, dummy_length);
            if (dummy_length > 0) {
                ESP_LOGI(TAG, "  Data[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X", 
                         dummy_data[0], 
                         dummy_length > 1 ? dummy_data[1] : 0,
                         dummy_length > 2 ? dummy_data[2] : 0,
                         dummy_length > 3 ? dummy_data[3] : 0,
                         dummy_length > 4 ? dummy_data[4] : 0,
                         dummy_length > 5 ? dummy_data[5] : 0,
                         dummy_length > 6 ? dummy_data[6] : 0,
                         dummy_length > 7 ? dummy_data[7] : 0);
            }
            
            // Check for product ID response
            if (dummy_channel == SHTP_CHANNEL_CONTROL && dummy_length >= 2 && 
                dummy_data[0] == SHTP_REPORT_PRODUCT_ID_RESPONSE) {
                ESP_LOGI(TAG, "Got product ID response after reset!");
                got_product_id = true;
                if (dummy_length >= 6) {
                    uint8_t sw_major = dummy_data[2];
                    uint8_t sw_minor = dummy_data[3];
                    uint16_t sw_build = dummy_data[4] | (dummy_data[5] << 8);
                    ESP_LOGI(TAG, "BNO085 Software Version: %d.%d.%d", sw_major, sw_minor, sw_build);
                }
            }
        } else if (read_ret != ESP_ERR_TIMEOUT && read_ret != ESP_ERR_NOT_FOUND) {
            break;
        }
    }
    
    // Only request product ID if we didn't get it automatically
    if (!got_product_id) {
        ESP_LOGI(TAG, "Requesting product ID...");
        ret = bno085_request_product_id();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to request product ID: %s", esp_err_to_name(ret));
        } else {
            // Wait for product ID response
            ret = bno085_wait_for_product_id(1000);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to receive product ID response: %s", esp_err_to_name(ret));
            }
        }
    }
    
    ESP_LOGI(TAG, "BNO085 initialization complete");
    return ESP_OK;
}

// Deinitialize the BNO085
void bno085_deinit(void)
{
    if (g_i2c_dev) {
        i2c_master_bus_rm_device(g_i2c_dev);
        g_i2c_dev = NULL;
    }
    
    if (g_i2c_bus) {
        i2c_del_master_bus(g_i2c_bus);
        g_i2c_bus = NULL;
    }
}

// Send a packet using SHTP
static esp_err_t shtp_send_packet(uint8_t channel, uint8_t *data, uint16_t length)
{
    if (!g_i2c_dev || !data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build SHTP header
    uint8_t packet[SHTP_MAX_TRANSFER_SIZE];
    uint16_t packet_length = length + 4;  // Add header size
    
    // Length (little endian) with continuation bit clear
    packet[0] = packet_length & 0xFF;
    packet[1] = (packet_length >> 8) & 0x7F;  // Clear continuation bit
    
    // Channel
    packet[2] = channel;
    
    // Sequence number
    packet[3] = sequence_number[channel]++;
    
    // Copy data
    memcpy(&packet[4], data, length);
    
    ESP_LOGD(TAG, "Sending SHTP packet: len=%d, channel=%d, seq=%d", packet_length, channel, packet[3]);
    
    // Send the packet
    esp_err_t ret = i2c_master_transmit(g_i2c_dev, packet, packet_length, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send SHTP packet: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// Receive a packet using SHTP
static esp_err_t shtp_receive_packet(uint8_t *channel, uint8_t *data, uint16_t *length, uint32_t timeout_ms)
{
    if (!g_i2c_dev || !channel || !data || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t buffer[SHTP_MAX_TRANSFER_SIZE];
    
    // Read the entire packet in one transaction
    // Start by reading enough to get the header and some data
    esp_err_t ret = i2c_master_receive(g_i2c_dev, buffer, SHTP_MAX_TRANSFER_SIZE, timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Parse header
    uint16_t packet_size = buffer[0] | (buffer[1] << 8);
    packet_size &= 0x7FFF;  // Clear continuation bit
    
    // Check for empty packet (size 0 means no data available)
    if (packet_size == 0) {
        *length = 0;
        *channel = 0;
        return ESP_ERR_NOT_FOUND;  // No data available
    }
    
    if (packet_size < 4) {
        ESP_LOGW(TAG, "Invalid packet size: %d (too small)", packet_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (packet_size > SHTP_MAX_TRANSFER_SIZE) {
        ESP_LOGW(TAG, "Invalid packet size: %d (too large)", packet_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    *channel = buffer[2];
    uint8_t seq = buffer[3];
    
    // Calculate payload size
    uint16_t payload_size = packet_size - 4;
    *length = payload_size;
    
    ESP_LOGD(TAG, "SHTP header: size=%d, channel=%d, seq=%d, payload=%d", 
             packet_size, *channel, seq, payload_size);
    
    // Copy payload if there is one
    if (payload_size > 0) {
        memcpy(data, buffer + 4, payload_size);
    }
    
    return ESP_OK;
}

// Send soft reset the BNO085
esp_err_t bno085_soft_reset(void)
{
    uint8_t reset_cmd[] = {0x01};  // Executable reset command
    return shtp_send_packet(SHTP_CHANNEL_EXECUTABLE, reset_cmd, sizeof(reset_cmd));
}

// Request product ID
static esp_err_t bno085_request_product_id(void)
{
    uint8_t cmd[] = {SHTP_REPORT_PRODUCT_ID_REQUEST, 0};  // Reserved byte
    return shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
}

// Wait for and parse product ID response
static esp_err_t bno085_wait_for_product_id(uint32_t timeout_ms)
{
    uint8_t data[300];
    uint16_t length;
    uint8_t channel;
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < timeout_ms) {
        esp_err_t ret = shtp_receive_packet(&channel, data, &length, 100);
        if (ret == ESP_OK) {
            if (channel == SHTP_CHANNEL_CONTROL && length >= 2 && data[0] == SHTP_REPORT_PRODUCT_ID_RESPONSE) {
                ESP_LOGI(TAG, "Received product ID response, length=%d", length);
                if (length >= 6) {
                    uint8_t sw_major = data[2];
                    uint8_t sw_minor = data[3];
                    uint16_t sw_build = data[4] | (data[5] << 8);
                    ESP_LOGI(TAG, "BNO085 Software Version: %d.%d.%d", sw_major, sw_minor, sw_build);
                }
                return ESP_OK;
            }
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Error waiting for product ID: %s", esp_err_to_name(ret));
        }
    }
    
    return ESP_ERR_TIMEOUT;
}

// Enable rotation vector reports
esp_err_t bno085_enable_rotation_vector(uint32_t time_between_reports_us)
{
    uint8_t cmd[17] = {0};
    
    cmd[0] = SHTP_REPORT_SET_FEATURE_COMMAND;
    cmd[1] = REPORT_ROTATION_VECTOR;
    cmd[2] = 0;     // Feature flags
    cmd[3] = 0;     // Change sensitivity (LSB)
    cmd[4] = 0;     // Change sensitivity (MSB)
    
    // Report interval in microseconds (little endian)
    cmd[5] = (time_between_reports_us >> 0) & 0xFF;
    cmd[6] = (time_between_reports_us >> 8) & 0xFF;
    cmd[7] = (time_between_reports_us >> 16) & 0xFF;
    cmd[8] = (time_between_reports_us >> 24) & 0xFF;
    
    // Batch interval (0 = no batching)
    cmd[9] = 0;
    cmd[10] = 0;
    cmd[11] = 0;
    cmd[12] = 0;
    
    // Sensor-specific configuration (0 for rotation vector)
    cmd[13] = 0;
    cmd[14] = 0;
    cmd[15] = 0;
    cmd[16] = 0;
    
    return shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
}

// Request feature status
static esp_err_t bno085_get_feature(uint8_t report_id)
{
    uint8_t cmd[] = {SHTP_REPORT_GET_FEATURE_REQUEST, report_id};
    return shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
}

// Enable game rotation vector reports (no magnetometer)
esp_err_t bno085_enable_game_rotation_vector(uint32_t time_between_reports_ms)
{
    uint32_t time_between_reports_us = time_between_reports_ms * 1000;
    uint8_t cmd[17] = {0};
    
    cmd[0] = SHTP_REPORT_SET_FEATURE_COMMAND;
    cmd[1] = SENSOR_REPORTID_GAME_ROTATION_VECTOR;
    cmd[2] = 0;     // Feature flags
    cmd[3] = 0;     // Change sensitivity (LSB)
    cmd[4] = 0;     // Change sensitivity (MSB)
    
    // Report interval in microseconds (little endian)
    cmd[5] = (time_between_reports_us >> 0) & 0xFF;
    cmd[6] = (time_between_reports_us >> 8) & 0xFF;
    cmd[7] = (time_between_reports_us >> 16) & 0xFF;
    cmd[8] = (time_between_reports_us >> 24) & 0xFF;
    
    // Batch interval (0 = no batching)
    cmd[9] = 0;
    cmd[10] = 0;
    cmd[11] = 0;
    cmd[12] = 0;
    
    // Sensor-specific configuration
    cmd[13] = 0;
    cmd[14] = 0;
    cmd[15] = 0;
    cmd[16] = 0;
    
    esp_err_t ret = shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
    
    if (ret == ESP_OK) {
        // Wait for the feature to be enabled
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Request feature status to confirm it's enabled
        ESP_LOGI(TAG, "Requesting feature status for report ID 0x%02X", SENSOR_REPORTID_GAME_ROTATION_VECTOR);
        bno085_get_feature(SENSOR_REPORTID_GAME_ROTATION_VECTOR);
        
        // Try to read the response
        uint8_t response_data[20];
        uint16_t response_length;
        uint8_t response_channel;
        
        for (int i = 0; i < 10; i++) {
            esp_err_t read_ret = shtp_receive_packet(&response_channel, response_data, &response_length, 50);
            if (read_ret == ESP_OK) {
                ESP_LOGI(TAG, "Response %d after enable: channel=%d, length=%d", i, response_channel, response_length);
                
                // Log first few bytes
                if (response_length > 0) {
                    ESP_LOGI(TAG, "  Data[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X", 
                             response_data[0], 
                             response_length > 1 ? response_data[1] : 0,
                             response_length > 2 ? response_data[2] : 0,
                             response_length > 3 ? response_data[3] : 0,
                             response_length > 4 ? response_data[4] : 0,
                             response_length > 5 ? response_data[5] : 0,
                             response_length > 6 ? response_data[6] : 0,
                             response_length > 7 ? response_data[7] : 0);
                }
                
                // Check if this is a feature response
                if (response_channel == SHTP_CHANNEL_CONTROL && response_length >= 2) {
                    if (response_data[0] == SHTP_REPORT_GET_FEATURE_RESPONSE) {
                        ESP_LOGI(TAG, "Received feature response for report ID 0x%02X", response_data[1]);
                        break;
                    }
                }
                
                // Check if we're already getting sensor data
                if (response_channel == SHTP_CHANNEL_REPORTS && response_length >= 14) {
                    if (response_data[0] == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
                        ESP_LOGI(TAG, "Already receiving game rotation vector data!");
                        break;
                    }
                }
            } else if (read_ret != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Error reading response: %s", esp_err_to_name(read_ret));
            }
        }
    }
    
    return ret;
}

// Get the latest quaternion data
esp_err_t bno085_get_quaternion(bno085_quaternion_t *quat)
{
    if (!quat || !g_i2c_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t channel;
    uint8_t data[SHTP_MAX_TRANSFER_SIZE];
    uint16_t length;
    
    // Try to receive a packet (non-blocking)
    esp_err_t ret = shtp_receive_packet(&channel, data, &length, 1);
    
    if (ret == ESP_ERR_TIMEOUT) {
        // No data available
        return ESP_ERR_NOT_FOUND;
    }
    
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGD(TAG, "get_quaternion: received channel=%d, length=%d", channel, length);
    if (length > 0) {
        ESP_LOGD(TAG, "  Data[0-3]: %02X %02X %02X %02X", 
                 data[0], 
                 length > 1 ? data[1] : 0,
                 length > 2 ? data[2] : 0,
                 length > 3 ? data[3] : 0);
    }
    
    // Check if this is a sensor report
    if (channel != SHTP_CHANNEL_REPORTS) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if this is a rotation vector report
    if (length < 14) {
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t report_id = data[0];
    if (report_id != REPORT_ROTATION_VECTOR && report_id != SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // Extract quaternion data
    // Data format: [report_id, sequence, status, delay, i, j, k, real, accuracy]
    // Quaternion values start at byte 5 and are 16-bit signed integers in Q14 format
    
    int16_t q_i = (int16_t)(data[5] | (data[6] << 8));
    int16_t q_j = (int16_t)(data[7] | (data[8] << 8));
    int16_t q_k = (int16_t)(data[9] | (data[10] << 8));
    int16_t q_real = (int16_t)(data[11] | (data[12] << 8));
    
    // Convert Q14 fixed point to float
    const float Q14_SCALE = 1.0f / 16384.0f;
    quat->i = q_i * Q14_SCALE;
    quat->j = q_j * Q14_SCALE;
    quat->k = q_k * Q14_SCALE;
    quat->real = q_real * Q14_SCALE;
    
    // Extract accuracy if available
    if (length >= 14) {
        quat->accuracy = data[13] & 0x03;
    } else {
        quat->accuracy = 3;  // Unknown
    }
    
    return ESP_OK;
}

// Check if BNO085 is available
bool bno085_is_available(void)
{
    if (!g_i2c_dev) {
        return false;
    }
    
    // Try to probe the device
    esp_err_t ret = i2c_master_probe(g_i2c_bus, 0x4B, 100);
    return (ret == ESP_OK);
}

// Check if new data is available
bool bno085_data_available(void)
{
    return true;  // Always return true, let get_quaternion handle timeouts
}

// Get rotation vector data (alias for get_quaternion)
esp_err_t bno085_get_rotation_vector(bno085_quaternion_t *quat)
{
    return bno085_get_quaternion(quat);
}

// Convert quaternion to 16-bit values for HID report (0-65535 range)
void bno085_quaternion_to_hid(const bno085_quaternion_t *quat, uint16_t *hid_data)
{
    if (!quat || !hid_data) {
        return;
    }
    
    // Convert from [-1, 1] range to [0, 65535] range
    // Add 1 to shift from [-1, 1] to [0, 2], then multiply by 32767.5
    hid_data[0] = (uint16_t)((quat->real + 1.0f) * 32767.5f);
    hid_data[1] = (uint16_t)((quat->i + 1.0f) * 32767.5f);
    hid_data[2] = (uint16_t)((quat->j + 1.0f) * 32767.5f);
    hid_data[3] = (uint16_t)((quat->k + 1.0f) * 32767.5f);
}

// Simple raw I2C read test
esp_err_t bno085_test_raw_read(void)
{
    if (!g_i2c_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Testing raw I2C read...");
    
    uint8_t header[4];
    esp_err_t ret = i2c_master_receive(g_i2c_dev, header, 4, 100);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Raw read header: %02X %02X %02X %02X", 
                 header[0], header[1], header[2], header[3]);
                 
        // Parse packet size
        uint16_t packet_size = header[0] | (header[1] << 8);
        packet_size &= 0x7FFF;
        
        if (packet_size > 4 && packet_size <= SHTP_MAX_TRANSFER_SIZE) {
            // Read the rest of the packet
            uint8_t buffer[SHTP_MAX_TRANSFER_SIZE];
            uint16_t remaining = packet_size - 4;
            
            ESP_LOGI(TAG, "Reading remaining %d bytes", remaining);
            ret = i2c_master_receive(g_i2c_dev, buffer, remaining, 100);
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read remaining bytes: %s", esp_err_to_name(ret));
            }
        }
    } else {
        ESP_LOGE(TAG, "Raw read failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
} 