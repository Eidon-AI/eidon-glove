# ESP-IDF Examples

This directory contains multiple ESP-IDF examples that can be compiled individually.

## Available Examples

1. **HID_EXAMPLE** (default) - HID device that changes volume up/down
   - Files: `esp_hid_device_main.c`, `esp_hid_gap.c`, `esp_hid_gap.h`
   
2. **SIMPLE_BLE_EXAMPLE** - Simple BLE advertising example
   - File: `simple_ble_example.c`

## How to Switch Between Examples

### Method 1: Using CMake Cache Variable (Recommended)

```bash
# To build the simple BLE example
cd /path/to/firmware
idf.py -DEXAMPLE_TO_BUILD=SIMPLE_BLE_EXAMPLE build

# To build the HID example (default)
idf.py -DEXAMPLE_TO_BUILD=HID_EXAMPLE build

# Or just
idf.py build
```

### Method 2: Using Environment Variable

```bash
# Set the example to build
export EXAMPLE_TO_BUILD=SIMPLE_BLE_EXAMPLE
idf.py build

# Or in one line
EXAMPLE_TO_BUILD=SIMPLE_BLE_EXAMPLE idf.py build
```

### Method 3: Modify CMakeLists.txt

Edit `firmware/main/CMakeLists.txt` and change the `EXAMPLE_TO_BUILD` variable:

```cmake
set(EXAMPLE_TO_BUILD "SIMPLE_BLE_EXAMPLE" CACHE STRING "Which example to build")
```

## Adding New Examples

1. Create your new example file in the `main` directory
2. Add a new condition in `CMakeLists.txt`:

```cmake
elseif(${EXAMPLE_TO_BUILD} STREQUAL "YOUR_NEW_EXAMPLE")
    set(srcs "your_new_example.c")
```

3. Build with: `idf.py -DEXAMPLE_TO_BUILD=YOUR_NEW_EXAMPLE build`

## Cleaning Between Examples

When switching between examples, it's recommended to clean the build:

```bash
idf.py fullclean
idf.py -DEXAMPLE_TO_BUILD=SIMPLE_BLE_EXAMPLE build
``` 