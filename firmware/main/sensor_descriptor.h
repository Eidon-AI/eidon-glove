#ifndef SENSOR_DESCRIPTOR_H
#define SENSOR_DESCRIPTOR_H

#include <stdint.h>

// HID Report descriptor for orientation sensor
static const uint8_t hid_sensor_descriptor[] = {
    /* -----------------------------------------------------------------------
     * Top-level collection : sensor orientation + vendor channel, ID = 1
     * ---------------------------------------------------------------------*/
    0x05, 0x20,             /* UsagePage (Sensor)                    */
    0x09, 0x80,             /* Usage     (Orientation)               */
    0xA1, 0x01,             /* Collection (Application)              */

    0x85, 0x01,             /*   Report ID (1)                       */

    /* --- quaternion : 4 × 16-bit -------------------------------------- */
    0x0A, 0x83, 0x04,       /*   Usage 0x0483 – Quaternion           */
    0x75, 0x10,             /*   ReportSize 16                       */
    0x95, 0x04,             /*   ReportCount 4                       */
    0x17, 0x00,0x00,0x00,0x00, /* Logical Min 0                    */
    0x27, 0xFF,0xFF,0x00,0x00, /* Logical Max 65535                */
    0x81, 0x02,             /*   Input (Data,Var,Abs)                */

    /* --- two switch bits on the Button page --------------------------- */
    0x05, 0x09,             /*   UsagePage (Button)                  */
    0x19, 0x01,             /*   Usage Min (Button 1)                */
    0x29, 0x02,             /*   Usage Max (Button 2)                */
    0x95, 0x02,             /*   ReportCount 2                       */
    0x75, 0x01,             /*   ReportSize 1                        */
    0x15, 0x00,             /*   Logical Min 0                       */
    0x25, 0x01,             /*   Logical Max 1                       */
    0x81, 0x02,             /*   Input (Data,Var,Abs)                */

    /* --- six padding bits --------------------------------------------- */
    0x95, 0x06,             /*   ReportCount 6                       */
    0x75, 0x01,             /*   ReportSize 1                        */
    0x81, 0x03,             /*   Input (Cnst,Var,Abs)                */

    0xC0                    /* End Collection                         */
};

// Report structure for sensor data
typedef struct __attribute__((packed)) {
    // uint8_t report_id;      // Report ID = 1
    uint16_t quaternion[4]; // w, x, y, z components (16-bit each)
    uint8_t buttons;        // Bits 0-1: buttons, Bits 2-7: padding
} sensor_report_t;

#endif // SENSOR_DESCRIPTOR_H 