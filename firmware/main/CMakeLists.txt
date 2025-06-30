# Define which example to build
# Options: HID_EXAMPLE, SIMPLE_BLE_EXAMPLE
set(EXAMPLE_TO_BUILD "HID_EXAMPLE" CACHE STRING "Which example to build")

if(${EXAMPLE_TO_BUILD} STREQUAL "HID_EXAMPLE")
    set(srcs "esp_hid_device_main.c"
             "esp_hid_gap.c"
             "bno085.c")
elseif(${EXAMPLE_TO_BUILD} STREQUAL "SIMPLE_BLE_EXAMPLE")
    set(srcs "simple_ble_example.c")
else()
    message(FATAL_ERROR "Unknown example: ${EXAMPLE_TO_BUILD}")
endif()

set(include_dirs ".")

idf_component_register(
    SRCS "esp_hid_device_main.c" "esp_hid_gap.c" "bno085.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hid freertos bt nvs_flash esp_driver_i2c
)

# Enable debug logging for BNO085
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
