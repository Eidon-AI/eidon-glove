#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SIMPLE_I2C_TEST";

// Test different pin combinations
typedef struct {
    int sda_pin;
    int scl_pin;
    const char* description;
} pin_config_t;

static pin_config_t pin_configs[] = {
    {20, 18, "GPIO 20/18 (current code)"},
    {9, 10,  "GPIO 9/10 (D9/D10)"},
    {8, 9,   "GPIO 8/9 (alternative)"},
    {21, 22, "GPIO 21/22 (common I2C)"},
};

#define BNO085_I2C_ADDR 0x4B

static void test_pin_combination(int sda_pin, int scl_pin, const char* desc)
{
    ESP_LOGI(TAG, "=== Testing %s (SDA=%d, SCL=%d) ===", desc, sda_pin, scl_pin);
    
    // Configure I2C bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
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
    
    // Test if any device responds
    bool found_device = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        ret = i2c_master_probe(i2c_bus, addr, 100);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Found device at address 0x%02X", addr);
            found_device = true;
            
            if (addr == BNO085_I2C_ADDR) {
                ESP_LOGI(TAG, "*** BNO085 FOUND! ***");
            }
        }
    }
    
    if (!found_device) {
        ESP_LOGW(TAG, "No devices found on this pin combination");
    }
    
    i2c_del_master_bus(i2c_bus);
    ESP_LOGI(TAG, "--- End test ---\n");
}

static void gpio_manual_test(void)
{
    ESP_LOGI(TAG, "=== Manual GPIO Test ===");
    
    // Test each pin combination manually
    for (int i = 0; i < sizeof(pin_configs)/sizeof(pin_configs[0]); i++) {
        int sda = pin_configs[i].sda_pin;
        int scl = pin_configs[i].scl_pin;
        
        ESP_LOGI(TAG, "Testing pins SDA=%d, SCL=%d", sda, scl);
        
        // Configure as outputs
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        
        // Set high
        gpio_set_level(sda, 1);
        gpio_set_level(scl, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Check levels
        int sda_level = gpio_get_level(sda);
        int scl_level = gpio_get_level(scl);
        ESP_LOGI(TAG, "  Pin levels: SDA=%d, SCL=%d", sda_level, scl_level);
        
        // Reset to input
        gpio_set_direction(sda, GPIO_MODE_INPUT);
        gpio_set_direction(scl, GPIO_MODE_INPUT);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Simple I2C Test for BNO085 ===");
    ESP_LOGI(TAG, "This will test multiple pin combinations");
    
    // Wait for system to stabilize
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Manual GPIO test first
    gpio_manual_test();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Test each pin combination with I2C
    for (int i = 0; i < sizeof(pin_configs)/sizeof(pin_configs[0]); i++) {
        test_pin_combination(pin_configs[i].sda_pin, pin_configs[i].scl_pin, pin_configs[i].description);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "=== Test Complete ===");
    ESP_LOGI(TAG, "Check the logs above for results");
    ESP_LOGI(TAG, "If no devices found, check:");
    ESP_LOGI(TAG, "1. Power connections (3.3V and GND)");
    ESP_LOGI(TAG, "2. I2C wiring (SDA and SCL)");
    ESP_LOGI(TAG, "3. Pull-up resistors (2.2k-4.7k to 3.3V)");
    ESP_LOGI(TAG, "4. BNO085 is not in reset state");
} 