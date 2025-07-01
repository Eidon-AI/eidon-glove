# Define which example to build
# Options: HID_EXAMPLE, SIMPLE_BLE_EXAMPLE, I2C_DIAGNOSTIC, SIMPLE_I2C_TEST
set(EXAMPLE_TO_BUILD "HID_EXAMPLE" CACHE STRING "Which example to build")

if(${EXAMPLE_TO_BUILD} STREQUAL "HID_EXAMPLE")
    set(srcs "esp_hid_device_main.c"
             "esp_hid_gap.c"
             "bno085.c"
             "bno085_sh2.c"
             "../components/sh2/sh2.c"
             "../components/sh2/sh2_SensorValue.c"
             "../components/sh2/sh2_util.c"
             "../components/sh2/shtp.c"
             "../components/sh2/euler.c")
elseif(${EXAMPLE_TO_BUILD} STREQUAL "SIMPLE_BLE_EXAMPLE")
    set(srcs "simple_ble_example.c")
elseif(${EXAMPLE_TO_BUILD} STREQUAL "I2C_DIAGNOSTIC")
    set(srcs "i2c_diagnostic.c")
elseif(${EXAMPLE_TO_BUILD} STREQUAL "SIMPLE_I2C_TEST")
    set(srcs "simple_i2c_test.c")
else()
    message(FATAL_ERROR "Unknown example: ${EXAMPLE_TO_BUILD}")
endif()

set(include_dirs ".")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_timer driver sh2 esp_hid efuse)

# Enable debug logging for BNO085
target_compile_definitions(${COMPONENT_LIB} PRIVATE LOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
 