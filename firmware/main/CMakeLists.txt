# Main component for Eidon Glove firmware
set(srcs "esp_hid_device_main.c"
         "esp_hid_gap.c"
         "adafruit_bno08x.c")

set(include_dirs ".")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${include_dirs}
                    REQUIRES bt nvs_flash esp_timer driver sh2 esp_hid efuse)

# Enable debug logging for BNO085
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
 