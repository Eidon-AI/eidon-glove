#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "I2C_DIAG";

#define I2C_MASTER_SCL_IO           18      // GPIO number for I2C master clock
#define I2C_MASTER_SDA_IO           20      // GPIO number for I2C master data
#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          50000   // Start with 50kHz for debugging
#define I2C_MASTER_TIMEOUT_MS       1000

#define BNO085_I2C_ADDR             0x4B

// Test different I2C addresses for BNO085
static uint8_t bno085_addresses[] = {0x4A, 0x4B};

static void gpio_test(void)
{
    ESP_LOGI(TAG, "=== GPIO Pin Test ===");
    
    // Configure pins as outputs for testing
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Test SDA pin
    ESP_LOGI(TAG, "Testing SDA pin (GPIO %d)...", I2C_MASTER_SDA_IO);
    gpio_set_level(I2C_MASTER_SDA_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    int sda_high = gpio_get_level(I2C_MASTER_SDA_IO);
    
    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    int sda_low = gpio_get_level(I2C_MASTER_SDA_IO);
    
    ESP_LOGI(TAG, "SDA pin test: HIGH=%d, LOW=%d", sda_high, sda_low);
    
    // Test SCL pin
    ESP_LOGI(TAG, "Testing SCL pin (GPIO %d)...", I2C_MASTER_SCL_IO);
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    int scl_high = gpio_get_level(I2C_MASTER_SCL_IO);
    
    gpio_set_level(I2C_MASTER_SCL_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    int scl_low = gpio_get_level(I2C_MASTER_SCL_IO);
    
    ESP_LOGI(TAG, "SCL pin test: HIGH=%d, LOW=%d", scl_high, scl_low);
    
    // Reset pins to input mode
    gpio_set_direction(I2C_MASTER_SDA_IO, GPIO_MODE_INPUT);
    gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_INPUT);
}

static esp_err_t i2c_master_init(void)
{
    ESP_LOGI(TAG, "=== I2C Master Initialization ===");
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "I2C bus created successfully");
    return ESP_OK;
}

static void i2c_scan_devices(void)
{
    ESP_LOGI(TAG, "=== I2C Device Scan ===");
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    int devices_found = 0;
    
    for (uint8_t addr = 1; addr < 127; addr++) {
        esp_err_t ret = i2c_master_probe(i2c_bus, addr, 100);  // 100ms timeout
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found I2C device at address 0x%02X", addr);
            devices_found++;
            
            // Check if this is one of the expected BNO085 addresses
            for (int i = 0; i < sizeof(bno085_addresses); i++) {
                if (addr == bno085_addresses[i]) {
                    ESP_LOGI(TAG, "  *** This matches expected BNO085 address! ***");
                }
            }
        }
    }
    
    if (devices_found == 0) {
        ESP_LOGW(TAG, "No I2C devices found on bus!");
        ESP_LOGW(TAG, "Please check:");
        ESP_LOGW(TAG, "  1. Power connections (3.3V and GND)");
        ESP_LOGW(TAG, "  2. I2C connections (SDA=GPIO%d, SCL=GPIO%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
        ESP_LOGW(TAG, "  3. Pull-up resistors (2.2k-4.7k to 3.3V)");
        ESP_LOGW(TAG, "  4. BNO085 is properly powered and not in reset");
    } else {
        ESP_LOGI(TAG, "Found %d I2C device(s)", devices_found);
    }
    
    i2c_del_master_bus(i2c_bus);
}

static void test_bno085_communication(void)
{
    ESP_LOGI(TAG, "=== BNO085 Communication Test ===");
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return;
    }
    
    // Test each BNO085 address
    for (int i = 0; i < sizeof(bno085_addresses); i++) {
        uint8_t addr = bno085_addresses[i];
        ESP_LOGI(TAG, "Testing BNO085 at address 0x%02X...", addr);
        
        // Create device for this address
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 50000,   // 50kHz for debugging
            .scl_wait_us = 1000,
            .flags = {
                .disable_ack_check = false,
            },
        };
        
        i2c_master_dev_handle_t i2c_dev = NULL;
        ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create device for address 0x%02X: %s", addr, esp_err_to_name(ret));
            continue;
        }
        
        // Try to read 4 bytes (SHTP header)
        uint8_t header[4];
        ret = i2c_master_receive(i2c_dev, header, 4, 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Successfully read from 0x%02X: %02X %02X %02X %02X", 
                     addr, header[0], header[1], header[2], header[3]);
            
            // Parse packet size
            uint16_t packet_size = header[0] | (header[1] << 8);
            packet_size &= 0x7FFF;  // Remove continuation bit
            
            ESP_LOGI(TAG, "Packet size: %d bytes", packet_size);
            
            if (packet_size > 4 && packet_size < 300) {
                // Read the rest of the packet
                uint8_t payload[300];
                ret = i2c_master_receive(i2c_dev, payload, packet_size - 4, 1000);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Read payload: %02X %02X %02X %02X...", 
                             payload[0], payload[1], payload[2], payload[3]);
                }
            }
        } else {
            ESP_LOGW(TAG, "Failed to read from 0x%02X: %s", addr, esp_err_to_name(ret));
        }
        
        i2c_master_bus_rm_device(i2c_dev);
    }
    
    i2c_del_master_bus(i2c_bus);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== BNO085 I2C Diagnostic Tool ===");
    ESP_LOGI(TAG, "This tool will help diagnose I2C connection issues");
    
    // Wait for system to stabilize
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 1: GPIO pins
    gpio_test();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test 2: I2C initialization
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Test 3: I2C device scan
    i2c_scan_devices();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Test 4: BNO085 communication
    test_bno085_communication();
    
    ESP_LOGI(TAG, "=== Diagnostic Complete ===");
    ESP_LOGI(TAG, "Check the logs above for any issues");
} 