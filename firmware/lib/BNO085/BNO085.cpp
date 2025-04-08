#include "BNO085.h"

Adafruit_BNO08x bno08x;
sh2_SensorValue_t sensorValue;

float quaternion_x = 0;
float quaternion_y = 0;
float quaternion_z = 0;
float quaternion_w = 1;

// Define the actual variable here
euler_t ypr = {0, 0, 0};

// Add these globals near the top with other variables
float linear_x = 0;
float linear_y = 0;
float linear_z = 0;

void printBNO085Values() {
    // Serial.println("Quaternion Values:");
    // Serial.print("X: "); Serial.print(quaternion_x, 4);
    // Serial.print(" Y: "); Serial.print(quaternion_y, 4);
    // Serial.print(" Z: "); Serial.print(quaternion_z, 4);
    // Serial.print(" W: "); Serial.println(quaternion_w, 4);
    
    // Print Euler angles and status
    Serial.print("Status: "); Serial.print(sensorValue.status); Serial.print("\t");
    Serial.print("Yaw: "); Serial.print(ypr.yaw);
    Serial.print(" Pitch: "); Serial.print(ypr.pitch);
    Serial.print(" Roll: "); Serial.println(ypr.roll);

    // // Print linear acceleration values
    // Serial.println("Linear Acceleration Values:");
    // Serial.print("X: "); Serial.print(linear_x);
    // Serial.print(" Y: "); Serial.print(linear_y);
    // Serial.print(" Z: "); Serial.println(linear_z);
}

void setReports() {
    // ARVR stabilized rotation vector at 5ms interval (200Hz)
    if (!bno08x.enableReport(SH2_ARVR_STABILIZED_RV, 1)) {
        Serial.println("Could not enable stabilized rotation vector");
    }

    // // Use regular accelerometer at 2.5ms interval (400Hz)
    // // Changed from LINEAR_ACCELERATION to regular ACCELEROMETER
    // if (!bno08x.enableReport(SH2_ACCELEROMETER)) {
    //     Serial.println("Could not enable accelerometer");
    // }

    // // Add linear acceleration reporting
    // if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION)) {
    //     Serial.println("Could not enable linear acceleration");
    // }
}

void setupBNO085() {
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Try to initialize the sensor
    if (!bno08x.begin_I2C(0x4B)) {
        Serial.println("Failed to find BNO085 chip");
        while (1) { delay(10); }
    }
    
    Serial.println("BNO085 Found!");
    setReports();
}

void updateBNO085() {
    static unsigned long lastPrint = 0;
    const unsigned long PRINT_INTERVAL = 100; // Print every 100ms

    if (bno08x.wasReset()) {
        Serial.println("BNO085 was reset");
        setReports();
    }
    
    if (bno08x.getSensorEvent(&sensorValue)) {
        switch (sensorValue.sensorId) {
            case SH2_ARVR_STABILIZED_RV:
                quaternion_x = sensorValue.un.arvrStabilizedRV.j;
                quaternion_y = sensorValue.un.arvrStabilizedRV.k;
                quaternion_z = sensorValue.un.arvrStabilizedRV.i;
                quaternion_w = sensorValue.un.arvrStabilizedRV.real;
                
                quaternionToEuler();
                break;
                
            case SH2_ACCELEROMETER:
                linear_x = sensorValue.un.accelerometer.x;
                linear_y = sensorValue.un.accelerometer.y;
                linear_z = sensorValue.un.accelerometer.z;
                break;
            
            case SH2_LINEAR_ACCELERATION:
                linear_x = sensorValue.un.linearAcceleration.x;
                linear_y = sensorValue.un.linearAcceleration.y;
                linear_z = sensorValue.un.linearAcceleration.z;
                break;
        }

        // Only print every PRINT_INTERVAL milliseconds
        if (millis() - lastPrint >= PRINT_INTERVAL) {
            printBNO085Values();
            lastPrint = millis();
        }
    }
}

void quaternionToEuler() {
    float sqr = sq(quaternion_w);
    float sqi = sq(quaternion_x);
    float sqj = sq(quaternion_y);
    float sqk = sq(quaternion_z);

    ypr.pitch = atan2(2.0 * (quaternion_x * quaternion_y + quaternion_z * quaternion_w),
                    (sqi - sqj - sqk + sqr));
    ypr.yaw = asin(-2.0 * (quaternion_x * quaternion_z - quaternion_y * quaternion_w) /
                     (sqi + sqj + sqk + sqr));
    ypr.roll = atan2(2.0 * (quaternion_y * quaternion_z + quaternion_x * quaternion_w),
                     (-sqi - sqj + sqk + sqr));

    // Convert to degrees
    ypr.yaw = ypr.yaw * RAD_TO_DEG;
    ypr.pitch = -ypr.pitch * RAD_TO_DEG;
    ypr.roll = ypr.roll * RAD_TO_DEG;

    // Shift the values by 180 degrees
    if (ypr.yaw >= 0) {
        ypr.yaw -= 180;
    } else {
        ypr.yaw += 180;
    }

    if (ypr.pitch >= 0) {
        ypr.pitch -= 180;
    } else {
        ypr.pitch += 180;
    }

    if (ypr.roll >= 0) {
        ypr.roll -= 180;
    } else {
        ypr.roll += 180;
    }
}
