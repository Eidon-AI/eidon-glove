# Main component for Eidon Glove firmware
set(srcs "main.c"
         "esp_hid_gap.c"
         "button.c"
         "imu.c"
         "led.c")

set(include_dirs ".")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${include_dirs}
                    REQUIRES bt nvs_flash esp_timer driver sh2 esp_hid efuse)

# Enable debug logging for BNO085
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
 