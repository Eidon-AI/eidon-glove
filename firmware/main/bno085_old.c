#include "bno085.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include <string.h>
#include <math.h>

static const char *TAG = "BNO085";

// SH2 Protocol definitions
#define SH2_HEADER_SIZE 4
#define SHTP_HEADER_SIZE 4

// Channels
#define CHANNEL_COMMAND 0
#define CHANNEL_EXECUTABLE 1
#define CHANNEL_CONTROL 2
#define CHANNEL_REPORTS 3

// SH2 Commands
#define SH2_CMD_GET_FEATURE_REQUEST 0xFE
#define SH2_CMD_SET_FEATURE_COMMAND 0xFD
#define SH2_CMD_GET_FEATURE_RESPONSE 0xFC

// Global state
static i2c_port_t g_i2c_port;
static uint8_t g_i2c_addr;
static uint8_t g_sequence_number[6] = {0};
static bno085_quaternion_t g_last_quaternion = {0};

// Helper function to send I2C data
static esp_err_t bno085_i2c_write(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

// Helper function to read I2C data - simple version first
static esp_err_t bno085_i2c_read(uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    
    // Clear buffer first
    memset(data, 0, len);
    
    // Simple read - just try to read the requested bytes
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_READ, true);
    
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

// Send a packet using SHTP protocol
static esp_err_t bno085_send_packet(uint8_t channel, const uint8_t *data, uint16_t data_len)
{
    uint8_t packet[256];
    uint16_t total_len = data_len + SHTP_HEADER_SIZE;
    
    // SHTP header
    packet[0] = total_len & 0xFF;
    packet[1] = total_len >> 8;
    packet[2] = channel;
    packet[3] = g_sequence_number[channel]++;
    
    // Copy data
    memcpy(&packet[4], data, data_len);
    
    return bno085_i2c_write(packet, total_len);
}

// Deinitialize BNO085 (cleanup I2C)
void bno085_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BNO085...");
    i2c_driver_delete(g_i2c_port);
}

// Initialize I2C and reset the BNO085
esp_err_t bno085_init(const bno085_config_t *config)
{
    ESP_LOGI(TAG, "Initializing BNO085...");
    
    g_i2c_port = config->i2c_port;
    g_i2c_addr = config->i2c_addr;
    
    // Give BNO085 time to boot up (like Arduino code does)
    ESP_LOGI(TAG, "Waiting for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second boot wait
    
    // Try toggling the reset pin if available (some BNO085 boards have it)
    // The Arduino library might be doing this internally
    ESP_LOGI(TAG, "Attempting BNO085 reset sequence...");
    
    ESP_LOGI(TAG, "Configuring I2C: port=%d, SDA=%d, SCL=%d, freq=%lu", 
             config->i2c_port, config->sda_pin, config->scl_pin, config->i2c_freq);
    
    // First, let's manually check the GPIO pins and perform I2C recovery
    ESP_LOGI(TAG, "Testing GPIO pins and performing I2C recovery...");
    gpio_reset_pin(config->sda_pin);
    gpio_reset_pin(config->scl_pin);
    
    // Configure pins as open-drain outputs with pull-ups for manual control
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->sda_pin) | (1ULL << config->scl_pin),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Set both lines high
    gpio_set_level(config->sda_pin, 1);
    gpio_set_level(config->scl_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    ESP_LOGI(TAG, "Initial GPIO pin states: SDA=%d, SCL=%d", 
             gpio_get_level(config->sda_pin), gpio_get_level(config->scl_pin));
    
    // I2C bus recovery: Send 9 clock pulses to release any stuck device
    ESP_LOGI(TAG, "Performing I2C bus recovery (9 clock pulses)...");
    for (int i = 0; i < 9; i++) {
        gpio_set_level(config->scl_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(1));
        gpio_set_level(config->scl_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Generate STOP condition
    gpio_set_level(config->sda_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(config->scl_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(config->sda_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    ESP_LOGI(TAG, "I2C recovery complete, final pin states: SDA=%d, SCL=%d", 
             gpio_get_level(config->sda_pin), gpio_get_level(config->scl_pin));
    
    // Reset pins to input mode before I2C driver takes over
    gpio_set_direction(config->sda_pin, GPIO_MODE_INPUT);
    gpio_set_direction(config->scl_pin, GPIO_MODE_INPUT);
    
    // Configure I2C - ESP32-C6 specific configuration
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,  // Try with internal pull-ups
        .scl_pullup_en = GPIO_PULLUP_ENABLE,  // Try with internal pull-ups
        .master.clk_speed = config->i2c_freq,
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,  // Use APB clock source
    };
    
    ESP_LOGI(TAG, "Calling i2c_param_config...");
    esp_err_t ret = i2c_param_config(g_i2c_port, &i2c_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }
    ESP_LOGI(TAG, "I2C param config successful");
    
    ESP_LOGI(TAG, "Calling i2c_driver_install...");
    ret = i2c_driver_install(g_i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C driver already installed, deleting and reinstalling...");
        i2c_driver_delete(g_i2c_port);
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay after delete
        ret = i2c_driver_install(g_i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C driver installed successfully");
    
    // Set I2C timeout - ESP32-C6 might need this
    ESP_LOGI(TAG, "Setting I2C timeout...");
    ret = i2c_set_timeout(g_i2c_port, 0xFFFFF); // Maximum timeout
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set I2C timeout: %s", esp_err_to_name(ret));
    }
    
    // Give BNO085 more time to stabilize after I2C init
    ESP_LOGI(TAG, "Waiting for BNO085 to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Scan I2C bus for devices with more detailed error reporting
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    int devices_found = 0;
    
    // First try a simple test at the expected address
    ESP_LOGI(TAG, "Quick test at BNO085 address 0x%02X...", g_i2c_addr);
    i2c_cmd_handle_t test_cmd = i2c_cmd_link_create();
    i2c_master_start(test_cmd);
    i2c_master_write_byte(test_cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(test_cmd);
    esp_err_t test_ret = i2c_master_cmd_begin(g_i2c_port, test_cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(test_cmd);
    ESP_LOGI(TAG, "BNO085 test result: %s", esp_err_to_name(test_ret));
    
    // Full scan
    for (int addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t scan_ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        
        if (scan_ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02X", addr);
            devices_found++;
        } else if (addr == g_i2c_addr) {
            ESP_LOGW(TAG, "BNO085 at 0x%02X not responding: %s", addr, esp_err_to_name(scan_ret));
        }
    }
    
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found on bus!");
        ESP_LOGW(TAG, "Please check:");
        ESP_LOGW(TAG, "  - Power connections (3.3V and GND)");
        ESP_LOGW(TAG, "  - I2C connections (SDA=%d, SCL=%d)", config->sda_pin, config->scl_pin);
        ESP_LOGW(TAG, "  - Pull-up resistors (2.2k-10k on SDA and SCL)");
    } else {
        ESP_LOGI(TAG, "Found %d I2C device(s)", devices_found);
    }
    
    // Wait for BNO085 to boot (longer wait like Arduino code)
    ESP_LOGI(TAG, "Waiting for BNO085 to boot...");
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // First, just test basic I2C ACK (like Arduino's Wire.beginTransmission/endTransmission)
    ESP_LOGI(TAG, "Testing basic I2C ACK from BNO085 at address 0x%02X", g_i2c_addr);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(g_i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No ACK from BNO085: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Check wiring: SDA=%d, SCL=%d, I2C_ADDR=0x%02X", 
                 config->sda_pin, config->scl_pin, g_i2c_addr);
        return ret;
    }
    ESP_LOGI(TAG, "BNO085 ACK received!");
    
    // Now try to read SHTP header (like Arduino's testBNO085Direct)
    ESP_LOGI(TAG, "Testing SHTP communication...");
    uint8_t shtp_header[4];
    ret = bno085_i2c_read(shtp_header, 4);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SHTP Header: %02X %02X %02X %02X", 
                 shtp_header[0], shtp_header[1], shtp_header[2], shtp_header[3]);
        uint16_t packet_len = shtp_header[0] | (shtp_header[1] << 8);
        ESP_LOGI(TAG, "SHTP packet length: %d", packet_len);
    } else {
        ESP_LOGW(TAG, "Could not read SHTP header: %s", esp_err_to_name(ret));
    }
    
    // Wait a bit more before sending reset
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Send reset command
    ESP_LOGI(TAG, "Sending soft reset command...");
    uint8_t reset_cmd[] = {0x01};  // Reset command
    ret = bno085_send_packet(CHANNEL_EXECUTABLE, reset_cmd, sizeof(reset_cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send reset command: %s", esp_err_to_name(ret));
        // Don't return error here - BNO085 might still work
    } else {
        ESP_LOGI(TAG, "Reset command sent");
    }
    
    // Wait for reset to complete (longer wait)
    ESP_LOGI(TAG, "Waiting for BNO085 to reset...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "BNO085 initialized successfully");
    return ESP_OK;
}

// Enable a sensor report
static esp_err_t bno085_set_feature_command(uint8_t report_id, uint32_t interval_us)
{
    uint8_t cmd[17] = {0};
    
    cmd[0] = SH2_CMD_SET_FEATURE_COMMAND;
    cmd[1] = report_id;
    cmd[2] = 0;  // Feature flags
    cmd[3] = 0;  // Change sensitivity LSB
    cmd[4] = 0;  // Change sensitivity MSB
    
    // Report interval in microseconds
    cmd[5] = (interval_us >> 0) & 0xFF;
    cmd[6] = (interval_us >> 8) & 0xFF;
    cmd[7] = (interval_us >> 16) & 0xFF;
    cmd[8] = (interval_us >> 24) & 0xFF;
    
    // Batch interval (0 = no batching)
    cmd[9] = 0;
    cmd[10] = 0;
    cmd[11] = 0;
    cmd[12] = 0;
    
    // Sensor-specific configuration (0)
    cmd[13] = 0;
    cmd[14] = 0;
    cmd[15] = 0;
    cmd[16] = 0;
    
    return bno085_send_packet(CHANNEL_CONTROL, cmd, sizeof(cmd));
}

// Enable rotation vector reports
esp_err_t bno085_enable_rotation_vector(uint32_t time_between_reports_ms)
{
    uint32_t interval_us = time_between_reports_ms * 1000;
    return bno085_set_feature_command(SENSOR_REPORTID_ROTATION_VECTOR, interval_us);
}

// Enable game rotation vector reports
esp_err_t bno085_enable_game_rotation_vector(uint32_t time_between_reports_ms)
{
    ESP_LOGI(TAG, "Enabling game rotation vector with %lu ms interval", time_between_reports_ms);
    uint32_t interval_us = time_between_reports_ms * 1000;
    esp_err_t ret = bno085_set_feature_command(SENSOR_REPORTID_GAME_ROTATION_VECTOR, interval_us);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Game rotation vector enabled successfully");
    } else {
        ESP_LOGE(TAG, "Failed to enable game rotation vector: %s", esp_err_to_name(ret));
    }
    return ret;
}

// Check if new data is available
bool bno085_data_available(void)
{
    uint8_t header[4];
    
    // Try to read the header
    if (bno085_i2c_read(header, 4) != ESP_OK) {
        return false;
    }
    
    // Check if there's data available
    uint16_t packet_len = header[0] | (header[1] << 8);
    if (packet_len > 4) {
        ESP_LOGD(TAG, "Data available: packet_len=%d, channel=%d", packet_len, header[2]);
    }
    return (packet_len > 4);
}

// Parse quaternion from sensor report
static bool parse_quaternion(const uint8_t *data, uint16_t len, bno085_quaternion_t *quat)
{
    if (len < 14) return false;
    
    uint8_t report_id = data[5];
    if (report_id != SENSOR_REPORTID_ROTATION_VECTOR && 
        report_id != SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
        return false;
    }
    
    // Extract quaternion data (Q14 fixed point format)
    int16_t qi = (data[7] << 8) | data[6];
    int16_t qj = (data[9] << 8) | data[8];
    int16_t qk = (data[11] << 8) | data[10];
    int16_t qr = (data[13] << 8) | data[12];
    
    // Convert from Q14 to float
    const float scale = 1.0f / 16384.0f;
    quat->i = qi * scale;
    quat->j = qj * scale;
    quat->k = qk * scale;
    quat->real = qr * scale;
    
    // Accuracy (0-3)
    if (len >= 16) {
        quat->accuracy = data[15] & 0x03;
    }
    
    return true;
}

// Get the latest quaternion data
esp_err_t bno085_get_quaternion(bno085_quaternion_t *quat)
{
    uint8_t buffer[256];
    
    // Read header
    if (bno085_i2c_read(buffer, 4) != ESP_OK) {
        return ESP_FAIL;
    }
    
    uint16_t packet_len = buffer[0] | (buffer[1] << 8);
    uint8_t channel = buffer[2];
    
    if (packet_len <= 4 || packet_len > sizeof(buffer)) {
        return ESP_FAIL;
    }
    
    // Read the rest of the packet
    if (bno085_i2c_read(buffer + 4, packet_len - 4) != ESP_OK) {
        return ESP_FAIL;
    }
    
    // Check if this is a sensor report
    if (channel != CHANNEL_REPORTS) {
        return ESP_FAIL;
    }
    
    // Parse the quaternion
    if (parse_quaternion(buffer, packet_len, &g_last_quaternion)) {
        *quat = g_last_quaternion;
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

// Convert quaternion to 16-bit values for HID report
void bno085_quaternion_to_hid(const bno085_quaternion_t *quat, uint16_t *hid_data)
{
    // Quaternion components are in range [-1, 1]
    // Map to unsigned 16-bit range [0, 65535]
    // Formula: (value + 1.0) * 32767.5
    
    hid_data[0] = (uint16_t)((quat->real + 1.0f) * 32767.5f);
    hid_data[1] = (uint16_t)((quat->i + 1.0f) * 32767.5f);
    hid_data[2] = (uint16_t)((quat->j + 1.0f) * 32767.5f);
    hid_data[3] = (uint16_t)((quat->k + 1.0f) * 32767.5f);
    
    // No need to clamp - uint16_t already limits to 0-65535
} 