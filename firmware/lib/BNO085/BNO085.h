#ifndef BNO085_H
#define BNO085_H

#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <Wire.h>

// Define I2C pins
#define I2C_SDA 21
#define I2C_SCL 20

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
void printBNO085Values();
void quaternionToEuler();

// Declare external variables to store sensor data
extern float quaternion_x;
extern float quaternion_y;
extern float quaternion_z;
extern float quaternion_w;

extern float linear_x;
extern float linear_y;
extern float linear_z;

#endif 