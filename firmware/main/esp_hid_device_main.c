/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_mac.h"

#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#else
#include "esp_bt_defs.h"
#if CONFIG_BT_BLE_ENABLED
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#endif
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#if CONFIG_BT_SDP_COMMON_ENABLED
#include "esp_sdp_api.h"
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif

#include "esp_hidd.h"
#include "esp_hid_gap.h"
#include "adafruit_bno08x.h"
#include "sensor_descriptor.h"
#include "esp_efuse.h"

static const char *TAG = "HID_DEV_DEMO";

#define INPUT_REPORT_ID 1

// Firmware version information
#define FIRMWARE_VERSION_MAJOR   1
#define FIRMWARE_VERSION_MINOR   0
#define FIRMWARE_VERSION_PATCH   0
#define FIRMWARE_VERSION_STRING  "1.0.0"

// Device Information Service UUIDs
#define DIS_SERVICE_UUID    0x180A
#define DIS_CHAR_MANUFACTURER_NAME_UUID  0x2A29
#define DIS_CHAR_MODEL_NUMBER_UUID       0x2A24
#define DIS_CHAR_SERIAL_NUMBER_UUID      0x2A25
#define DIS_CHAR_FIRMWARE_REVISION_UUID  0x2A26

// DIS attribute values - DISABLED FOR NOW
/*
static const char dis_manufacturer[] = "Eidon AI";
static const char dis_model[] = "Eidon Glove";
static char dis_serial[32] = ""; // Will be set dynamically
static const char dis_firmware[] = FIRMWARE_VERSION_STRING;

// DIS handles
static uint16_t dis_service_handle = 0;
static uint16_t dis_char_handle_manufacturer = 0;
static uint16_t dis_char_handle_model = 0;
static uint16_t dis_char_handle_serial = 0;
static uint16_t dis_char_handle_firmware = 0;
*/

// DIS GATT server event handler - DISABLED FOR NOW
/*
static void dis_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_err_t ret;
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        // Create DIS service
        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id = {
                .inst_id = 0,
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid = {.uuid16 = DIS_SERVICE_UUID}
                }
            }
        };
        ret = esp_ble_gatts_create_service(gatts_if, &service_id, 8);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create DIS service: %s", esp_err_to_name(ret));
        }
        break;
    }
    case ESP_GATTS_CREATE_EVT: {
        dis_service_handle = param->create.service_handle;
        // Add Manufacturer Name
        esp_bt_uuid_t char_uuid = {.len = ESP_UUID_LEN_16, .uuid = {.uuid16 = DIS_CHAR_MANUFACTURER_NAME_UUID}};
        esp_attr_value_t attr_val = {
            .attr_max_len = sizeof(dis_manufacturer),
            .attr_len = strlen(dis_manufacturer),
            .attr_value = (uint8_t*)dis_manufacturer
        };
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS manufacturer char: %s", esp_err_to_name(ret));
        // Add Model Number
        char_uuid.uuid.uuid16 = DIS_CHAR_MODEL_NUMBER_UUID;
        attr_val.attr_max_len = sizeof(dis_model);
        attr_val.attr_len = strlen(dis_model);
        attr_val.attr_value = (uint8_t*)dis_model;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS model char: %s", esp_err_to_name(ret));
        // Add Serial Number (set after creation)
        char_uuid.uuid.uuid16 = DIS_CHAR_SERIAL_NUMBER_UUID;
        attr_val.attr_max_len = sizeof(dis_serial);
        attr_val.attr_len = strlen(dis_serial);
        attr_val.attr_value = (uint8_t*)dis_serial;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS serial char: %s", esp_err_to_name(ret));
        // Add Firmware Revision
        char_uuid.uuid.uuid16 = DIS_CHAR_FIRMWARE_REVISION_UUID;
        attr_val.attr_max_len = sizeof(dis_firmware);
        attr_val.attr_len = strlen(dis_firmware);
        attr_val.attr_value = (uint8_t*)dis_firmware;
        ret = esp_ble_gatts_add_char(dis_service_handle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ, &attr_val, NULL);
        if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to add DIS firmware char: %s", esp_err_to_name(ret));
        // Start service
        esp_ble_gatts_start_service(dis_service_handle);
        break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
        // Save handles for later updates
        uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
        if (uuid == DIS_CHAR_MANUFACTURER_NAME_UUID) dis_char_handle_manufacturer = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_MODEL_NUMBER_UUID) dis_char_handle_model = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_SERIAL_NUMBER_UUID) dis_char_handle_serial = param->add_char.attr_handle;
        else if (uuid == DIS_CHAR_FIRMWARE_REVISION_UUID) dis_char_handle_firmware = param->add_char.attr_handle;
        break;
    }
    default:
        break;
    }
}
*/

// BNO085 I2C configuration
#define I2C_SCL  18   // GPIO 18 for SCL
#define I2C_SDA  20   // GPIO 20 for SDA
#define I2C_FREQ 100000
#define I2C_ADDR 0x4B

// SHTP constants
#define SHTP_MAX_TRANSFER_SIZE 300

typedef struct
{
    TaskHandle_t task_hdl;
    esp_hidd_dev_t *hid_dev;
    uint8_t protocol_mode;
    uint8_t *buffer;
} local_param_t;

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
static local_param_t s_ble_hid_param = {0};

// Using sensor descriptor from sensor_descriptor.h
#if CONFIG_EXAMPLE_HID_DEVICE_ROLE && CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
const unsigned char mouseReportMap[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)

    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)

    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x05,                    //     REPORT_SIZE (5)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)

    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x09, 0x38,                    //     USAGE (Wheel)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)

    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};
// send the buttons, change in x, and change in y
void send_mouse(uint8_t buttons, char dx, char dy, char wheel)
{
    static uint8_t buffer[4] = {0};
    buffer[0] = buttons;
    buffer[1] = dx;
    buffer[2] = dy;
    buffer[3] = wheel;
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 0, buffer, 4);
}

void ble_hid_demo_task_mouse(void *pvParameters)
{
    static const char* help_string = "########################################################################\n"\
    "BT hid mouse demo usage:\n"\
    "You can input these value to simulate mouse: 'q', 'w', 'e', 'a', 's', 'd', 'h'\n"\
    "q -- click the left key\n"\
    "w -- move up\n"\
    "e -- click the right key\n"\
    "a -- move left\n"\
    "s -- move down\n"\
    "d -- move right\n"\
    "h -- show the help\n"\
    "########################################################################\n";
    printf("%s\n", help_string);
    char c;
    while (1) {
        c = fgetc(stdin);
        switch (c) {
        case 'q':
            send_mouse(1, 0, 0, 0);
            break;
        case 'w':
            send_mouse(0, 0, -10, 0);
            break;
        case 'e':
            send_mouse(2, 0, 0, 0);
            break;
        case 'a':
            send_mouse(0, -10, 0, 0);
            break;
        case 's':
            send_mouse(0, 0, 10, 0);
            break;
        case 'd':
            send_mouse(0, 10, 0, 0);
            break;
        case 'h':
            printf("%s\n", help_string);
            break;
        default:
            break;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#endif

#if CONFIG_EXAMPLE_HID_DEVICE_ROLE && CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
#define CASE(a, b, c)  \
                case a: \
				buffer[0] = b;  \
				buffer[2] = c; \
                break;\

// USB keyboard codes
#define USB_HID_MODIFIER_LEFT_CTRL      0x01
#define USB_HID_MODIFIER_LEFT_SHIFT     0x02
#define USB_HID_MODIFIER_LEFT_ALT       0x04
#define USB_HID_MODIFIER_RIGHT_CTRL     0x10
#define USB_HID_MODIFIER_RIGHT_SHIFT    0x20
#define USB_HID_MODIFIER_RIGHT_ALT      0x40

#define USB_HID_SPACE                   0x2C
#define USB_HID_DOT                     0x37
#define USB_HID_NEWLINE                 0x28
#define USB_HID_FSLASH                  0x38
#define USB_HID_BSLASH                  0x31
#define USB_HID_COMMA                   0x36
#define USB_HID_DOT                     0x37

const unsigned char keyboardReportMap[] = { //7 bytes input (modifiers, resrvd, keys*5), 1 byte output
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x03,        //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection

    // 65 bytes
};

static void char_to_code(uint8_t *buffer, char ch)
{
	// Check if lower or upper case
	if(ch >= 'a' && ch <= 'z')
	{
		buffer[0] = 0;
		// convert ch to HID letter, starting at a = 4
		buffer[2] = (uint8_t)(4 + (ch - 'a'));
	}
	else if(ch >= 'A' && ch <= 'Z')
	{
		// Add left shift
		buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT;
		// convert ch to lower case
		ch = ch - ('A'-'a');
		// convert ch to HID letter, starting at a = 4
		buffer[2] = (uint8_t)(4 + (ch - 'a'));
	}
	else if(ch >= '0' && ch <= '9') // Check if number
	{
		buffer[0] = 0;
		// convert ch to HID number, starting at 1 = 30, 0 = 39
		if(ch == '0')
		{
			buffer[2] = 39;
		}
		else
		{
			buffer[2] = (uint8_t)(30 + (ch - '1'));
		}
	}
	else // not a letter nor a number
	{
		switch(ch)
		{
            CASE(' ', 0, USB_HID_SPACE);
			CASE('.', 0,USB_HID_DOT);
            CASE('\n', 0, USB_HID_NEWLINE);
			CASE('?', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_FSLASH);
			CASE('/', 0 ,USB_HID_FSLASH);
			CASE('\\', 0, USB_HID_BSLASH);
			CASE('|', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_BSLASH);
			CASE(',', 0, USB_HID_COMMA);
			CASE('<', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_COMMA);
			CASE('>', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_COMMA);
			CASE('@', USB_HID_MODIFIER_LEFT_SHIFT, 31);
			CASE('!', USB_HID_MODIFIER_LEFT_SHIFT, 30);
			CASE('#', USB_HID_MODIFIER_LEFT_SHIFT, 32);
			CASE('$', USB_HID_MODIFIER_LEFT_SHIFT, 33);
			CASE('%', USB_HID_MODIFIER_LEFT_SHIFT, 34);
			CASE('^', USB_HID_MODIFIER_LEFT_SHIFT,35);
			CASE('&', USB_HID_MODIFIER_LEFT_SHIFT, 36);
			CASE('*', USB_HID_MODIFIER_LEFT_SHIFT, 37);
			CASE('(', USB_HID_MODIFIER_LEFT_SHIFT, 38);
			CASE(')', USB_HID_MODIFIER_LEFT_SHIFT, 39);
			CASE('-', 0, 0x2D);
			CASE('_', USB_HID_MODIFIER_LEFT_SHIFT, 0x2D);
			CASE('=', 0, 0x2E);
			CASE('+', USB_HID_MODIFIER_LEFT_SHIFT, 39);
			CASE(8, 0, 0x2A); // backspace
			CASE('\t', 0, 0x2B);
			default:
				buffer[0] = 0;
				buffer[2] = 0;
		}
	}
}

void send_keyboard(char c)
{
    static uint8_t buffer[8] = {0};
    char_to_code(buffer, c);
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
    /* send the keyrelease event with sufficient delay */
    vTaskDelay(50 / portTICK_PERIOD_MS);
    memset(buffer, 0, sizeof(uint8_t) * 8);
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
}

void ble_hid_demo_task_kbd(void *pvParameters)
{
    static const char* help_string = "########################################################################\n"\
                                      "BT hid keyboard demo usage:\n"\
                                      "########################################################################\n";
                                    /* TODO : Add support for function keys and ctrl, alt, esc, etc. */
    printf("%s\n", help_string);
    char c;
    while (1) {
        c = fgetc(stdin);

        if(c != 255) {
            send_keyboard(c);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
#endif
// Sensor orientation descriptor with quaternion data (byte-aligned)
const unsigned char sensorReportMap[] = {
    0x05, 0x20,             /* UsagePage (Sensor)                    */
    0x09, 0x80,             /* Usage     (Orientation)               */
    0xA1, 0x01,             /* Collection (Application)              */

      0x85, 0x01,           /*   Report ID (1)                       */

      /* --- quaternion : 4 × 16-bit -------------------------------------- */
      0x0A, 0x83, 0x04,     /*   Usage 0x0483 – Quaternion           */
      0x75, 0x10,           /*   ReportSize 16                       */
      0x95, 0x04,           /*   ReportCount 4                       */
      0x17, 0x00,0x00,0x00,0x00, /* Logical Min 0                    */
      0x27, 0xFF,0xFF,0x00,0x00, /* Logical Max 65535                */
      0x81, 0x02,           /*   Input (Data,Var,Abs)                */

      /* --- 8 button bits to make total 72 bits (9 bytes) --------------- */
      0x05, 0x09,           /*   UsagePage (Button)                  */
      0x19, 0x01,           /*   Usage Minimum (Button 1)            */
      0x29, 0x08,           /*   Usage Maximum (Button 8)            */
      0x15, 0x00,           /*   Logical Minimum (0)                 */
      0x25, 0x01,           /*   Logical Maximum (1)                 */
      0x75, 0x01,           /*   Report Size (1)                     */
      0x95, 0x08,           /*   Report Count (8)                    */
      0x81, 0x02,           /*   Input (Data,Var,Abs)                */
      
      0xC0                   /* End Collection                        */
};

static esp_hid_raw_report_map_t ble_report_maps[] = {
#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_EXAMPLE_HID_DEVICE_ROLE == 1
    /* This block is compiled for bluedroid as well */
    {
        .data = sensorReportMap,
        .len = sizeof(sensorReportMap)
    }
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE && CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
    {
        .data = keyboardReportMap,
        .len = sizeof(keyboardReportMap)
    },
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE && CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
    {
        .data = mouseReportMap,
        .len = sizeof(mouseReportMap)
    },
#endif
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id          = 0xE1D0,  // Eidon AI vendor ID
    .product_id         = 0x0002,  // Eidon Tracker product ID
    .version            = 0x0100,
#if CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
    .device_name        = "ESP Keyboard",
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
    .device_name        = "ESP Mouse",
#else
    .device_name        = NULL,  // Will be set dynamically
#endif
    .manufacturer_name  = "Eidon AI",
    .serial_number      = NULL,  // Will be set dynamically
    .report_maps        = ble_report_maps,
    .report_maps_len    = 1
};

#define HID_CC_RPT_MUTE                 1
#define HID_CC_RPT_POWER                2
#define HID_CC_RPT_LAST                 3
#define HID_CC_RPT_ASSIGN_SEL           4
#define HID_CC_RPT_PLAY                 5
#define HID_CC_RPT_PAUSE                6
#define HID_CC_RPT_RECORD               7
#define HID_CC_RPT_FAST_FWD             8
#define HID_CC_RPT_REWIND               9
#define HID_CC_RPT_SCAN_NEXT_TRK        10
#define HID_CC_RPT_SCAN_PREV_TRK        11
#define HID_CC_RPT_STOP                 12

#define HID_CC_RPT_CHANNEL_UP           0x10
#define HID_CC_RPT_CHANNEL_DOWN         0x30
#define HID_CC_RPT_VOLUME_UP            0x40
#define HID_CC_RPT_VOLUME_DOWN          0x80

// HID Consumer Control report bitmasks
#define HID_CC_RPT_NUMERIC_BITS         0xF0
#define HID_CC_RPT_CHANNEL_BITS         0xCF
#define HID_CC_RPT_VOLUME_BITS          0x3F
#define HID_CC_RPT_BUTTON_BITS          0xF0
#define HID_CC_RPT_SELECTION_BITS       0xCF

// Macros for the HID Consumer Control 2-byte report
#define HID_CC_RPT_SET_NUMERIC(s, x)    (s)[0] &= HID_CC_RPT_NUMERIC_BITS;   (s)[0] = (x)
#define HID_CC_RPT_SET_CHANNEL(s, x)    (s)[0] &= HID_CC_RPT_CHANNEL_BITS;   (s)[0] |= ((x) & 0x03) << 4
#define HID_CC_RPT_SET_VOLUME_UP(s)     (s)[0] &= HID_CC_RPT_VOLUME_BITS;    (s)[0] |= 0x40
#define HID_CC_RPT_SET_VOLUME_DOWN(s)   (s)[0] &= HID_CC_RPT_VOLUME_BITS;    (s)[0] |= 0x80
#define HID_CC_RPT_SET_BUTTON(s, x)     (s)[1] &= HID_CC_RPT_BUTTON_BITS;    (s)[1] |= (x)
#define HID_CC_RPT_SET_SELECTION(s, x)  (s)[1] &= HID_CC_RPT_SELECTION_BITS; (s)[1] |= ((x) & 0x03) << 4

// HID Consumer Usage IDs (subset of the codes available in the USB HID Usage Tables spec)
#define HID_CONSUMER_POWER          48  // Power
#define HID_CONSUMER_RESET          49  // Reset
#define HID_CONSUMER_SLEEP          50  // Sleep

#define HID_CONSUMER_MENU           64  // Menu
#define HID_CONSUMER_SELECTION      128 // Selection
#define HID_CONSUMER_ASSIGN_SEL     129 // Assign Selection
#define HID_CONSUMER_MODE_STEP      130 // Mode Step
#define HID_CONSUMER_RECALL_LAST    131 // Recall Last
#define HID_CONSUMER_QUIT           148 // Quit
#define HID_CONSUMER_HELP           149 // Help
#define HID_CONSUMER_CHANNEL_UP     156 // Channel Increment
#define HID_CONSUMER_CHANNEL_DOWN   157 // Channel Decrement

#define HID_CONSUMER_PLAY           176 // Play
#define HID_CONSUMER_PAUSE          177 // Pause
#define HID_CONSUMER_RECORD         178 // Record
#define HID_CONSUMER_FAST_FORWARD   179 // Fast Forward
#define HID_CONSUMER_REWIND         180 // Rewind
#define HID_CONSUMER_SCAN_NEXT_TRK  181 // Scan Next Track
#define HID_CONSUMER_SCAN_PREV_TRK  182 // Scan Previous Track
#define HID_CONSUMER_STOP           183 // Stop
#define HID_CONSUMER_EJECT          184 // Eject
#define HID_CONSUMER_RANDOM_PLAY    185 // Random Play
#define HID_CONSUMER_SELECT_DISC    186 // Select Disk
#define HID_CONSUMER_ENTER_DISC     187 // Enter Disc
#define HID_CONSUMER_REPEAT         188 // Repeat
#define HID_CONSUMER_STOP_EJECT     204 // Stop/Eject
#define HID_CONSUMER_PLAY_PAUSE     205 // Play/Pause
#define HID_CONSUMER_PLAY_SKIP      206 // Play/Skip

#define HID_CONSUMER_VOLUME         224 // Volume
#define HID_CONSUMER_BALANCE        225 // Balance
#define HID_CONSUMER_MUTE           226 // Mute
#define HID_CONSUMER_BASS           227 // Bass
#define HID_CONSUMER_VOLUME_UP      233 // Volume Increment
#define HID_CONSUMER_VOLUME_DOWN    234 // Volume Decrement

#define HID_RPT_ID_CC_IN        3   // Consumer Control input report ID
#define HID_CC_IN_RPT_LEN       2   // Consumer Control input report Len
void esp_hidd_send_consumer_value(uint8_t key_cmd, bool key_pressed)
{
    uint8_t buffer[HID_CC_IN_RPT_LEN] = {0, 0};
    if (key_pressed) {
        switch (key_cmd) {
        case HID_CONSUMER_CHANNEL_UP:
            HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_UP);
            break;

        case HID_CONSUMER_CHANNEL_DOWN:
            HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_DOWN);
            break;

        case HID_CONSUMER_VOLUME_UP:
            HID_CC_RPT_SET_VOLUME_UP(buffer);
            break;

        case HID_CONSUMER_VOLUME_DOWN:
            HID_CC_RPT_SET_VOLUME_DOWN(buffer);
            break;

        case HID_CONSUMER_MUTE:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_MUTE);
            break;

        case HID_CONSUMER_POWER:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_POWER);
            break;

        case HID_CONSUMER_RECALL_LAST:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_LAST);
            break;

        case HID_CONSUMER_ASSIGN_SEL:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_ASSIGN_SEL);
            break;

        case HID_CONSUMER_PLAY:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PLAY);
            break;

        case HID_CONSUMER_PAUSE:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PAUSE);
            break;

        case HID_CONSUMER_RECORD:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_RECORD);
            break;

        case HID_CONSUMER_FAST_FORWARD:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_FAST_FWD);
            break;

        case HID_CONSUMER_REWIND:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_REWIND);
            break;

        case HID_CONSUMER_SCAN_NEXT_TRK:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_NEXT_TRK);
            break;

        case HID_CONSUMER_SCAN_PREV_TRK:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_PREV_TRK);
            break;

        case HID_CONSUMER_STOP:
            HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_STOP);
            break;

        default:
            break;
        }
    }
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_CC_IN, buffer, HID_CC_IN_RPT_LEN);
    return;
}

#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_EXAMPLE_HID_DEVICE_ROLE == 1

// Sensor task to send quaternion data via HID
void ble_hid_sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");
    
    // Wait for BNO085 to initialize
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    while (1) {
        if (s_ble_hid_param.hid_dev && esp_hidd_dev_connected(s_ble_hid_param.hid_dev)) {
            ESP_LOGI(TAG, "HID device connected and ready for sensor data");
            break;
        } else {
            ESP_LOGI(TAG, "Waiting for HID connection...");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // The actual sensor data sending is handled by the bno085_task
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#if 0
void ble_hid_sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting sensor task");
    
    // Wait a bit for system to stabilize
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Initializing BNO085 on I2C addr 0x%02X, SDA=%d, SCL=%d", 
             I2C_ADDR, I2C_SDA, I2C_SCL);
    
    // Initialize BNO085
    esp_err_t ret = bno085_init(&bno085_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BNO085: %s", esp_err_to_name(ret));
        // Don't delete task - continue without sensor
        // vTaskDelete(NULL);
        // return;
        
        // Send dummy data instead
        ESP_LOGW(TAG, "Running in demo mode without BNO085");
    }
    
    // Enable game rotation vector (no magnetometer)
    ret = bno085_enable_game_rotation_vector(20); // 20ms = 50Hz
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Game rotation vector enabled");
    
    // Give the sensor time to start producing data
    ESP_LOGI(TAG, "Waiting for sensor to start producing data...");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
    
    sensor_report_t report = {0};
    // report.report_id = 1;
    report.buttons = 0; // No buttons pressed initially
    
    bno085_quaternion_t quat;
    bool sensor_available = (ret == ESP_OK);
    float demo_angle = 0.0f;
    
    while (1) {
        if (sensor_available) {
            // Check if new data is available
            if (bno085_data_available()) {
                // Get quaternion data
                if (bno085_get_quaternion(&quat) == ESP_OK) {
                    // Convert to HID format using temporary array
                    uint16_t temp_quaternion[4];
                    bno085_quaternion_to_hid(&quat, temp_quaternion);
                    
                    // Copy to packed struct
                    memcpy(report.quaternion, temp_quaternion, sizeof(temp_quaternion));
                    
                    // Send HID report
                    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, 
                                         (uint8_t*)&report, sizeof(report));
                    
                    ESP_LOGD(TAG, "Quat: w=%.3f, x=%.3f, y=%.3f, z=%.3f", 
                            quat.real, quat.i, quat.j, quat.k);
                }
            }
        } else {
            // Demo mode - send rotating quaternion
            demo_angle += 0.01f; // Slow rotation
            if (demo_angle > 2 * M_PI) demo_angle -= 2 * M_PI;
            
            // Create rotation quaternion around Z axis
            quat.real = cosf(demo_angle / 2.0f);
            quat.i = 0.0f;
            quat.j = 0.0f;
            quat.k = sinf(demo_angle / 2.0f);
            
            // Convert to HID format
            uint16_t temp_quaternion[4];
            bno085_quaternion_to_hid(&quat, temp_quaternion);
            memcpy(report.quaternion, temp_quaternion, sizeof(temp_quaternion));
            
            // Send HID report
            esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, 
                                 (uint8_t*)&report, sizeof(report));
            
            ESP_LOGD(TAG, "Demo Quat: w=%.3f, x=%.3f, y=%.3f, z=%.3f", 
                    quat.real, quat.i, quat.j, quat.k);
        }
        
        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hz update rate
    }
}

// Keep the old demo task for compatibility
void ble_hid_demo_task(void *pvParameters)
{
    ble_hid_sensor_task(pvParameters);
}
#endif  // #if 0
#endif  // #if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_EXAMPLE_HID_DEVICE_ROLE == 1



// HID report structure for sensor data (exactly 9 bytes)
typedef struct {
    // uint8_t report_id;      // Report ID (1)
    uint16_t quaternion[4]; // 4 quaternion components (w, x, y, z) as 16-bit values
    uint8_t buttons;        // 8 button bits (packed into 1 byte)
} __attribute__((packed)) sensor_hid_report_t;

// BNO085 task to read sensor data
static void bno085_task(void *pvParameters)
{
    ESP_LOGI(TAG, "BNO085 task started");
    
    // Initialize BNO085 with Adafruit-style wrapper
    esp_err_t ret = adafruit_bno08x_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BNO085");
        vTaskDelete(NULL);
        return;
    }
    
    // Enable game rotation vector at 50Hz (20ms = 20000us)
    ret = adafruit_bno08x_enable_game_rotation_vector(20000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable game rotation vector");
        adafruit_bno08x_deinit();
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "BNO085 initialized successfully with Adafruit-style wrapper, starting sensor loop");
    
    sensor_hid_report_t report = {0};
    // report.report_id = 1;
    report.buttons = 0;  // No buttons pressed
    
    while (1) {
        // Service the sensor (handles SHTP communication)
        ret = adafruit_bno08x_service();
        if (ret == ESP_OK && adafruit_bno08x_has_new_quaternion()) {
            sh2_RotationVector_t quat;
            ret = adafruit_bno08x_get_quaternion(&quat);
            if (ret == ESP_OK) {
                // Apply coordinate system transformation to fix yaw/pitch swapping
                adafruit_bno08x_transform_coordinate_system(&quat);
                
                // Convert float quaternion to uint16_t for HID report
                // Scale from [-1, 1] to [0, 65535]
                report.quaternion[0] = (uint16_t)((quat.i + 1.0f) * 32767.5f);     // x
                report.quaternion[1] = (uint16_t)((quat.j + 1.0f) * 32767.5f);     // y
                report.quaternion[2] = (uint16_t)((quat.k + 1.0f) * 32767.5f);     // z
                report.quaternion[3] = (uint16_t)((quat.real + 1.0f) * 32767.5f);  // w

                // Send HID report if connected (try both BOOT and REPORT modes)
                if (s_ble_hid_param.hid_dev) {
                    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, INPUT_REPORT_ID, (uint8_t*)&report, sizeof(report));
                    
                    ESP_LOGI(TAG, "HID Sensor Report sent: Quat: w=%.3f, x=%.3f, y=%.3f, z=%.3f mode=%d", 
                             quat.real, quat.i, quat.j, quat.k, s_ble_hid_param.protocol_mode);
                } else {
                    ESP_LOGW(TAG, "HID device not connected");
                }
            }
        }
        
        // Small delay to prevent hogging CPU
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ble_hid_task_start_up(void)
{
    ESP_LOGI(TAG, "ble_hid_task_start_up called");
    
    if (s_ble_hid_param.task_hdl) {
        // Task already exists
        ESP_LOGI(TAG, "Task already exists");
        return;
    }
#if !CONFIG_BT_NIMBLE_ENABLED || CONFIG_EXAMPLE_HID_DEVICE_ROLE == 1
    /* Executed for bluedroid and nimble sensor mode */
    ESP_LOGI(TAG, "Creating sensor task");
    xTaskCreate(ble_hid_sensor_task, "ble_hid_sensor_task", 4 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);

#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
    /* Nimble Specific */
    xTaskCreate(ble_hid_demo_task_kbd, "ble_hid_demo_task_kbd", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
    /* Nimble Specific */
    xTaskCreate(ble_hid_demo_task_mouse, "ble_hid_demo_task_mouse", 3 * 1024, NULL, configMAX_PRIORITIES - 3,
                &s_ble_hid_param.task_hdl);
#endif
}

void ble_hid_task_shut_down(void)
{
    if (s_ble_hid_param.task_hdl) {
        vTaskDelete(s_ble_hid_param.task_hdl);
        s_ble_hid_param.task_hdl = NULL;
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
    static const char *TAG = "HID_DEV_BLE";

    switch (event) {
    case ESP_HIDD_START_EVENT: {
        ESP_LOGI(TAG, "START");
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_CONNECT_EVENT: {
        ESP_LOGI(TAG, "CONNECT");
        s_ble_hid_param.protocol_mode = 0; // Initialize to BOOT mode, will be updated by protocol mode event
        ble_hid_task_start_up();
        break;
    }
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index, param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
        s_ble_hid_param.protocol_mode = param->protocol_mode.protocol_mode;
        ESP_LOGI(TAG, "Protocol mode updated to: %d", s_ble_hid_param.protocol_mode);
        break;
    }
    case ESP_HIDD_CONTROL_EVENT: {
        ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index, param->control.control ? "EXIT_" : "");
        if (param->control.control)
        {
            // exit suspend
            // ble_hid_task_start_up(); // Already started on connect
        } else {
            // suspend
            ble_hid_task_shut_down();
        }
    break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        ESP_LOGI(TAG, "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index, esp_hid_usage_str(param->output.usage), param->output.report_id, param->output.length);
        ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
        break;
    }
    case ESP_HIDD_FEATURE_EVENT: {
        ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:", param->feature.map_index, esp_hid_usage_str(param->feature.usage), param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
        ESP_LOGI(TAG, "DISCONNECT: %s", esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        ble_hid_task_shut_down();
        esp_hid_ble_gap_adv_start();
        break;
    }
    case ESP_HIDD_STOP_EVENT: {
        ESP_LOGI(TAG, "STOP");
        break;
    }
    default:
        break;
    }
    return;
}
#endif

#if CONFIG_BT_NIMBLE_ENABLED
void ble_hid_device_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}
void ble_store_config_init(void);
#endif

// Function to generate unique serial number from MAC address
static void generate_unique_serial_number(char *serial_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // Use WiFi STA MAC address (unique per chip)
    // Format as EIDON-GLOVE-XXXXXXXX where X is hex digit from MAC
    snprintf(serial_buffer, buffer_size, "EIDON-GLOVE-%02X%02X%02X%02X", 
             mac[2], mac[3], mac[4], mac[5]);
}

// Function to generate unique device name with MAC suffix
static void generate_unique_device_name(char *name_buffer, size_t buffer_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // Format as "Eidon Tracker-XXXX" where XXXX is last 4 hex digits of MAC
    snprintf(name_buffer, buffer_size, "Eidon Tracker-%02X%02X", 
             mac[4], mac[5]);
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main() started");
    
    esp_err_t ret;
#if HID_DEV_MODE == HIDD_IDLE_MODE
    ESP_LOGE(TAG, "Please turn on BT HID device or BLE!");
    return;
#endif
    
    ESP_LOGI(TAG, "Initializing NVS...");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
    ret = esp_hid_gap_init(HID_DEV_MODE);
    ESP_ERROR_CHECK( ret );

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
    // Generate unique device name with MAC suffix
    static char unique_device_name[32];
    generate_unique_device_name(unique_device_name, sizeof(unique_device_name));
    ble_hid_config.device_name = unique_device_name;
    ESP_LOGI(TAG, "Generated unique device name: %s", unique_device_name);
    
#if CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, ble_hid_config.device_name);
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE, ble_hid_config.device_name);
#else
    ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC, ble_hid_config.device_name);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hid_ble_gap_adv_init failed: %s", esp_err_to_name(ret));
        return;
    }
#if CONFIG_BT_BLE_ENABLED
    if ((ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler)) != ESP_OK) {
        ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
        return;
    }
#endif
    // Generate unique serial number from MAC address
    static char unique_serial[32];
    generate_unique_serial_number(unique_serial, sizeof(unique_serial));
    ble_hid_config.serial_number = unique_serial;
    ESP_LOGI(TAG, "Generated unique serial number: %s", unique_serial);
    
    // Set DIS serial number for use in event handler - DISABLED FOR NOW
    /*
    strncpy(dis_serial, unique_serial, sizeof(dis_serial)-1);
    dis_serial[sizeof(dis_serial)-1] = '\0';
    */

    // Register the DIS GATT server - DISABLED FOR NOW
    /*
    ret = esp_ble_gatts_register_callback(dis_gatts_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DIS GATT server: %s", esp_err_to_name(ret));
    }
    ret = esp_ble_gatts_app_register(0xA0A0); // Arbitrary app ID for DIS
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to app register DIS: %s", esp_err_to_name(ret));
    }
    */
    
    ESP_LOGI(TAG, "setting ble device");
    ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_ble_hid_param.hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BLE HID device initialized successfully");
    
    // Start BNO085 test task
    ESP_LOGI(TAG, "Starting BNO085 test task");
    xTaskCreate(bno085_task, "bno085_task", 4096, NULL, 5, NULL);
#endif

#if CONFIG_BT_HID_DEVICE_ENABLED
    ESP_LOGI(TAG, "setting device name");
    esp_bt_gap_set_device_name(bt_hid_config.device_name);
    ESP_LOGI(TAG, "setting cod major, peripheral");
    esp_bt_cod_t cod = {0};
    cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
    cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_POINTING;
    esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "setting bt device");
    ESP_ERROR_CHECK(
        esp_hidd_dev_init(&bt_hid_config, ESP_HID_TRANSPORT_BT, bt_hidd_event_callback, &s_bt_hid_param.hid_dev));
#if CONFIG_BT_SDP_COMMON_ENABLED
    ESP_ERROR_CHECK(esp_sdp_register_callback(esp_sdp_cb));
    ESP_ERROR_CHECK(esp_sdp_init());
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif /* CONFIG_BT_HID_DEVICE_ENABLED */
#if CONFIG_BT_NIMBLE_ENABLED
    /* XXX Need to have template for store */
    ble_store_config_init();

    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
	/* Starting nimble task after gatts is initialized*/
    ret = esp_nimble_enable(ble_hid_device_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }
#endif
}
