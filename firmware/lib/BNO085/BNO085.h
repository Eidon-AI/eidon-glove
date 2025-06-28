#ifndef BNO085_H
#define BNO085_H

#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <Wire.h>

// Define I2C pins
#define I2C_SCL 18  // D10 on XIAO ESP32-C6 (GPIO18)
#define I2C_SDA 20  // D9 on XIAO ESP32-C6 (GPIO20)
#define I2C_INT 19
#define I2C_ADDR 0x4B

// Declare the struct type
struct euler_t {
    float yaw;
    float pitch;
    float roll;
};

// Declare the variable as extern
extern euler_t ypr;

void setupBNO085();
void updateBNO085();
void resetBNO085();
void printBNO085Values();
void quaternionToEuler();

// Helper function to check if BNO085 is available
bool isBNO085Available();

// Declare external variables to store sensor data
extern float quaternion_x;
extern float quaternion_y;
extern float quaternion_z;
extern float quaternion_w;
extern bool bno085_available;

#endif 