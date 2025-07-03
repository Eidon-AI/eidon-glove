#ifndef GLOVE_DESCRIPTOR_H
#define GLOVE_DESCRIPTOR_H

#include <stdint.h>

// HID Report descriptor for Eidon Glove (orientation sensor + vendor channels)
// TODO: Extend with additional sensors (flex sensors, pressure sensors, etc.)
static const uint8_t glove_report_map[] = {
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

      /* ------------------------------------------------------------------
       * Vendor-defined channel : Output (1 byte) - Report ID 2
       * ---------------------------------------------------------------- */
      0x85, 0x02,           /*   Report ID (2)                       */
      0x06, 0x00, 0xFF,     /*   UsagePage (Vendor 0xFF00)           */
      0x09, 0x01,           /*   Usage      (Vendor 1)               */
      0x15, 0x00,           /*   Logical Minimum (0)                 */
      0x26, 0xFF, 0x00,     /*   Logical Maximum (255)               */
      0x75, 0x08,           /*   ReportSize 8                        */
      0x95, 0x01,           /*   ReportCount 1                       */
      0x91, 0x02,           /*   Output (Data,Var,Abs)               */
      
      /* ------------------------------------------------------------------
       * Vendor-defined channel : Feature (3 bytes) - Device shell color storage
       * ---------------------------------------------------------------- */
      0x85, 0x03,           /*   Report ID (3)                       */
      0x06, 0x00, 0xFF,     /*   UsagePage (Vendor 0xFF00)           */
      0x09, 0x02,           /*   Usage      (Vendor 2)               */
      0x15, 0x00,           /*   Logical Minimum (0)                 */
      0x26, 0xFF, 0x00,     /*   Logical Maximum (255)               */
      0x75, 0x08,           /*   ReportSize 8                        */
      0x95, 0x03,           /*   ReportCount 3 (R,G,B)               */
      0xB1, 0x02,           /*   Feature (Data,Var,Abs)              */
      
      0xC0                   /* End Collection                        */
};

// Report structure for glove data (exactly 9 bytes for now, will be extended)
typedef struct {
    // uint8_t report_id;      // Report ID (1)
    uint16_t quaternion[4]; // 4 quaternion components (w, x, y, z) as 16-bit values
    uint8_t buttons;        // 8 button bits (packed into 1 byte)
    // TODO: Add additional sensor data fields here
} __attribute__((packed)) glove_hid_report_t;

#endif // GLOVE_DESCRIPTOR_H 