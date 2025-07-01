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
esp_err_t shtp_receive_packet(uint8_t *channel, uint8_t *data, uint16_t *length, uint32_t timeout_ms);
static esp_err_t bno085_request_product_id(void);
static esp_err_t bno085_wait_for_product_id(uint32_t timeout_ms);
static esp_err_t bno085_send_initialize(void);

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
                        got_product_id = true;
                    }
                }
            }
        } else if (read_ret != ESP_ERR_TIMEOUT && read_ret != ESP_ERR_NOT_FOUND) {
            break;
        }
    }
    
    // Send initialize command
    ESP_LOGI(TAG, "Sending initialize command...");
    ret = bno085_send_initialize();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send initialize command: %s", esp_err_to_name(ret));
    }
    
    // Wait for initialization
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Clear any pending packets
    for (int i = 0; i < 5; i++) {
        shtp_receive_packet(&dummy_channel, dummy_data, &dummy_length, 10);
    }
    
    // If we didn't get product ID automatically, request it
    if (!got_product_id) {
        ESP_LOGI(TAG, "Requesting product ID...");
        ret = bno085_request_product_id();
        if (ret == ESP_OK) {
            // Wait for product ID response
            ret = bno085_wait_for_product_id(1000);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to receive product ID response: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Failed to request product ID: %s", esp_err_to_name(ret));
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

// Receive a packet using SHTP (handles fragmentation)
esp_err_t shtp_receive_packet(uint8_t *channel, uint8_t *data, uint16_t *length, uint32_t timeout_ms)
{
    if (!g_i2c_dev || !channel || !data || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t header[4];
    uint16_t total_received = 0;
    bool first_packet = true;
    uint16_t expected_length = 0;
    
    while (true) {
        // Read the 4-byte header
        esp_err_t ret = i2c_master_receive(g_i2c_dev, header, 4, timeout_ms);
        
        if (ret == ESP_ERR_TIMEOUT) {
            if (first_packet) {
                return ESP_ERR_NOT_FOUND;
            }
            // Timeout on continuation packet - return what we have
            break;
        }
        
        if (ret != ESP_OK) {
            return ret;
        }
        
        // Parse header
        uint16_t packet_size = header[0] | (header[1] << 8);
        bool has_continuation = (packet_size & 0x8000) != 0;
        packet_size &= 0x7FFF;  // Clear continuation bit
        
        // Check for empty packet
        if (packet_size == 0) {
            if (first_packet) {
                *length = 0;
                *channel = 0;
                return ESP_ERR_NOT_FOUND;
            }
            break;
        }
        
        if (first_packet) {
            // First packet - get channel and expected total length
            *channel = header[2];
            expected_length = packet_size;
            first_packet = false;
        }
        
        // Calculate how much payload to read (excluding the 4-byte header we already read)
        uint16_t payload_size = packet_size - 4;
        
        if (payload_size > 0) {
            // Make sure we don't overflow the buffer
            if (total_received + payload_size > SHTP_MAX_TRANSFER_SIZE - 4) {
                ESP_LOGW(TAG, "Packet too large for buffer");
                break;
            }
            
            // Read the payload
            ret = i2c_master_receive(g_i2c_dev, data + total_received, payload_size, 10);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read payload: %s", esp_err_to_name(ret));
                return ret;
            }
            
            total_received += payload_size;
        }
        
        // If no continuation bit, we're done
        if (!has_continuation) {
            break;
        }
        
        // Small delay between continuation packets
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    *length = total_received;
    return ESP_OK;
}

// Send soft reset the BNO085
esp_err_t bno085_soft_reset(void)
{
    uint8_t reset_cmd[] = {0x01};  // Executable reset command
    return shtp_send_packet(SHTP_CHANNEL_EXECUTABLE, reset_cmd, sizeof(reset_cmd));
}

// Send initialize command
static esp_err_t bno085_send_initialize(void)
{
    // Send initialize command on channel 1 (executable)
    uint8_t cmd[] = {0x04};  // Initialize command
    return shtp_send_packet(SHTP_CHANNEL_EXECUTABLE, cmd, sizeof(cmd));
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
// Currently unused but may be useful for debugging
/*
static esp_err_t bno085_get_feature(uint8_t report_id)
{
    uint8_t cmd[] = {SHTP_REPORT_GET_FEATURE_REQUEST, report_id};
    return shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
}
*/

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
    
    ESP_LOGI(TAG, "Sending Set Feature command for Game Rotation Vector (0x%02X)", SENSOR_REPORTID_GAME_ROTATION_VECTOR);
    ESP_LOGI(TAG, "Report interval: %lu us (%lu ms)", time_between_reports_us, time_between_reports_ms);
    
    esp_err_t ret = shtp_send_packet(SHTP_CHANNEL_CONTROL, cmd, sizeof(cmd));
    
    if (ret == ESP_OK) {
        // Wait for the feature to be enabled
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Read any responses
        uint8_t response_data[100];
        uint16_t response_length;
        uint8_t response_channel;
        bool got_feature_response = false;
        
        for (int i = 0; i < 10; i++) {
            esp_err_t read_ret = shtp_receive_packet(&response_channel, response_data, &response_length, 50);
            if (read_ret == ESP_OK) {
                ESP_LOGI(TAG, "Response %d: channel=%d, length=%d", i, response_channel, response_length);
                
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
                    if (response_data[0] == SHTP_REPORT_GET_FEATURE_RESPONSE && response_data[1] == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
                        ESP_LOGI(TAG, "Got feature response confirming Game Rotation Vector is enabled");
                        got_feature_response = true;
                        
                        // Check the actual report interval
                        if (response_length >= 9) {
                            uint32_t actual_interval = response_data[5] | (response_data[6] << 8) | (response_data[7] << 16) | (response_data[8] << 24);
                            ESP_LOGI(TAG, "Actual report interval: %lu us", actual_interval);
                        }
                    }
                }
            }
        }
        
        if (!got_feature_response) {
            ESP_LOGW(TAG, "Did not receive feature response confirmation");
        }
        
        // Send a force sensor flush command to trigger initial data
        ESP_LOGI(TAG, "Sending force sensor flush command");
        uint8_t flush_cmd[] = {SHTP_REPORT_COMMAND_REQUEST, 0, 0x01, SENSOR_REPORTID_GAME_ROTATION_VECTOR};
        ret = shtp_send_packet(SHTP_CHANNEL_CONTROL, flush_cmd, sizeof(flush_cmd));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send force flush command: %s", esp_err_to_name(ret));
        }
        
        // Wait a bit more
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    return ret;
}

// Check if data is available by peeking at the header
// Currently unused but may be useful for optimization
/*
static bool bno085_has_data(void)
{
    if (!g_i2c_dev) {
        return false;
    }
    
    uint8_t header[4];
    esp_err_t ret = i2c_master_receive(g_i2c_dev, header, 4, 1);  // Very short timeout
    
    if (ret != ESP_OK) {
        return false;
    }
    
    // Check if there's a valid packet
    uint16_t packet_size = header[0] | (header[1] << 8);
    packet_size &= 0x7FFF;
    
    return (packet_size >= 4);
}
*/

// Get the latest quaternion data
esp_err_t bno085_get_quaternion(bno085_quaternion_t *quat)
{
    if (!quat || !g_i2c_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t channel;
    uint8_t data[SHTP_MAX_TRANSFER_SIZE];
    uint16_t length;
    
    // Try to receive a packet
    esp_err_t ret = shtp_receive_packet(&channel, data, &length, 5);  // 5ms timeout
    
    if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_NOT_FOUND) {
        // No data available - this is normal
        return ESP_ERR_NOT_FOUND;
    }
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "get_quaternion: receive error: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "get_quaternion: received channel=%d, length=%d", channel, length);
    if (length > 0) {
        ESP_LOGD(TAG, "  Data[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X", 
                 data[0], 
                 length > 1 ? data[1] : 0,
                 length > 2 ? data[2] : 0,
                 length > 3 ? data[3] : 0,
                 length > 4 ? data[4] : 0,
                 length > 5 ? data[5] : 0,
                 length > 6 ? data[6] : 0,
                 length > 7 ? data[7] : 0);
    }
    
    // Check if this is a sensor report
    if (channel != SHTP_CHANNEL_REPORTS) {
        ESP_LOGD(TAG, "Not a sensor report - channel %d", channel);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Parse the packet - it might be a base timestamp report containing sensor data
    uint8_t report_id = data[0];
    
    if (report_id == SHTP_REPORT_BASE_TIMESTAMP) {
        // Base timestamp report - check if it contains game rotation vector data
        ESP_LOGD(TAG, "Base timestamp report, length=%d", length);
        
        // Base timestamp format:
        // [0] = 0xFB (report ID)
        // [1-4] = timestamp delta
        // [5+] = embedded sensor reports
        
        if (length > 5) {
            // Look for embedded sensor reports starting at byte 5
            int offset = 5;
            while (offset < length) {
                if (offset + 14 > length) {
                    // Not enough data for a full sensor report
                    break;
                }
                
                uint8_t embedded_report_id = data[offset];
                ESP_LOGD(TAG, "Embedded report at offset %d: ID=0x%02X", offset, embedded_report_id);
                
                if (embedded_report_id == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
                    // Found game rotation vector!
                    ESP_LOGI(TAG, "Found game rotation vector in base timestamp report!");
                    
                    // Game rotation vector format (14 bytes):
                    // [0] = report ID (0x08)
                    // [1] = sequence number
                    // [2] = status
                    // [3] = delay
                    // [4-5] = i (int16)
                    // [6-7] = j (int16)
                    // [8-9] = k (int16)
                    // [10-11] = real (int16)
                    // [12-13] = accuracy (uint16) - might not be present
                    
                    // Extract quaternion data
                    int16_t q_i = (int16_t)(data[offset + 4] | (data[offset + 5] << 8));
                    int16_t q_j = (int16_t)(data[offset + 6] | (data[offset + 7] << 8));
                    int16_t q_k = (int16_t)(data[offset + 8] | (data[offset + 9] << 8));
                    int16_t q_real = (int16_t)(data[offset + 10] | (data[offset + 11] << 8));
                    
                    // Convert Q14 fixed point to float
                    const float Q14_SCALE = 1.0f / 16384.0f;
                    quat->i = q_i * Q14_SCALE;
                    quat->j = q_j * Q14_SCALE;
                    quat->k = q_k * Q14_SCALE;
                    quat->real = q_real * Q14_SCALE;
                    
                    // Extract accuracy if available
                    if (offset + 13 < length) {
                        quat->accuracy = data[offset + 12] & 0x03;
                    } else {
                        quat->accuracy = 3;  // Unknown
                    }
                    
                    ESP_LOGD(TAG, "Quaternion: i=%d, j=%d, k=%d, real=%d", q_i, q_j, q_k, q_real);
                    ESP_LOGD(TAG, "Float: i=%.3f, j=%.3f, k=%.3f, real=%.3f", quat->i, quat->j, quat->k, quat->real);
                    
                    return ESP_OK;
                }
                
                // Skip to next potential report
                // Most sensor reports are 14 bytes, but some might be different
                if (embedded_report_id == REPORT_ROTATION_VECTOR || 
                    embedded_report_id == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
                    offset += 14;  // Rotation vector reports are 14 bytes
                } else {
                    // Unknown report, try to skip it
                    offset += 14;  // Assume standard size
                }
            }
        }
        
        // No game rotation vector found in this packet
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check if this is a direct rotation vector report (less common with base timestamps)
    if (report_id == REPORT_ROTATION_VECTOR || report_id == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
        ESP_LOGI(TAG, "Direct rotation vector report ID 0x%02X!", report_id);
        
        if (length < 14) {
            ESP_LOGD(TAG, "Report too short - length %d", length);
            return ESP_ERR_NOT_FOUND;
        }
        
        // Extract quaternion data
        int16_t q_i = (int16_t)(data[4] | (data[5] << 8));
        int16_t q_j = (int16_t)(data[6] | (data[7] << 8));
        int16_t q_k = (int16_t)(data[8] | (data[9] << 8));
        int16_t q_real = (int16_t)(data[10] | (data[11] << 8));
        
        // Convert Q14 fixed point to float
        const float Q14_SCALE = 1.0f / 16384.0f;
        quat->i = q_i * Q14_SCALE;
        quat->j = q_j * Q14_SCALE;
        quat->k = q_k * Q14_SCALE;
        quat->real = q_real * Q14_SCALE;
        
        // Extract accuracy if available
        if (length >= 14) {
            quat->accuracy = data[12] & 0x03;
        } else {
            quat->accuracy = 3;  // Unknown
        }
        
        return ESP_OK;
    }
    
    ESP_LOGD(TAG, "Not a rotation vector report - ID 0x%02X", report_id);
    return ESP_ERR_NOT_FOUND;
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

// Simple test to see if we can read from the BNO085
esp_err_t bno085_test_raw_read(void)
{
    if (!g_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Testing raw I2C read...");
    
    uint8_t header[4];
    esp_err_t ret = i2c_master_receive(g_i2c_dev, header, 4, 100);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read header: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Raw read header: %02X %02X %02X %02X", 
             header[0], header[1], header[2], header[3]);
    
    // Parse packet size
    uint16_t packet_size = header[0] | (header[1] << 8);
    packet_size &= 0x7FFF;
    
    if (packet_size > 4) {
        ESP_LOGI(TAG, "Reading remaining %d bytes", packet_size - 4);
        uint8_t payload[SHTP_MAX_TRANSFER_SIZE];
        ret = i2c_master_receive(g_i2c_dev, payload, packet_size - 4, 100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read payload: %s", esp_err_to_name(ret));
        }
    }
    
    return ret;
}

// Debug function to read and log all available packets
esp_err_t bno085_debug_read_all(void)
{
    if (!g_i2c_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "=== Reading all available packets ===");
    
    uint8_t channel;
    uint8_t data[SHTP_MAX_TRANSFER_SIZE];
    uint16_t length;
    int packet_count = 0;
    
    // Read up to 20 packets
    for (int i = 0; i < 20; i++) {
        esp_err_t ret = shtp_receive_packet(&channel, data, &length, 50);
        
        if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_NOT_FOUND) {
            break;  // No more data
        }
        
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "I2C in invalid state");
            break;
        }
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Read error: %s", esp_err_to_name(ret));
            break;
        }
        
        packet_count++;
        ESP_LOGI(TAG, "Packet %d: channel=%d, length=%d", packet_count, channel, length);
        
        if (length > 0) {
            // Log first 16 bytes
            char hex_str[49] = {0};  // 16 * 3 + 1
            int bytes_to_log = (length > 16) ? 16 : length;
            for (int j = 0; j < bytes_to_log; j++) {
                sprintf(&hex_str[j * 3], "%02X ", data[j]);
            }
            ESP_LOGI(TAG, "  Data: %s%s", hex_str, (length > 16) ? "..." : "");
            
            // Decode packet type
            if (channel == SHTP_CHANNEL_CONTROL && length >= 2) {
                switch (data[0]) {
                    case SHTP_REPORT_PRODUCT_ID_RESPONSE:
                        ESP_LOGI(TAG, "  -> Product ID Response");
                        break;
                    case SHTP_REPORT_GET_FEATURE_RESPONSE:
                        ESP_LOGI(TAG, "  -> Get Feature Response for report 0x%02X", data[1]);
                        break;
                    case SHTP_REPORT_BASE_TIMESTAMP:
                        ESP_LOGI(TAG, "  -> Base Timestamp");
                        break;
                    default:
                        ESP_LOGI(TAG, "  -> Control packet type 0x%02X", data[0]);
                        break;
                }
            } else if (channel == SHTP_CHANNEL_REPORTS && length >= 1) {
                ESP_LOGI(TAG, "  -> Sensor Report ID 0x%02X", data[0]);
            }
        }
    }
    
    ESP_LOGI(TAG, "Total packets read: %d", packet_count);
    ESP_LOGI(TAG, "=== End of packet dump ===");
    
    return ESP_OK;
} 