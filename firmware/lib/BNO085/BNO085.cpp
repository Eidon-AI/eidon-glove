#include "BNO085.h"

Adafruit_BNO08x bno08x;
sh2_SensorValue_t sensorValue;

float quaternion_x = 0;
float quaternion_y = 0;
float quaternion_z = 0;
float quaternion_w = 1;

euler_t ypr = {0, 0, 0};

void printBNO085Values() {
    // Serial.println("Quaternion Values:");
    Serial.print("X: "); Serial.print(quaternion_x, 4);
    Serial.print(" Y: "); Serial.print(quaternion_y, 4);
    Serial.print(" Z: "); Serial.print(quaternion_z, 4);
    Serial.print(" W: "); Serial.println(quaternion_w, 4);
}

void setReports() {
    // Use GAME_ROTATION_VECTOR instead of ARVR_STABILIZED_RV for no magnetic north reference
    if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 5000)) { // 5ms (200Hz)
        Serial.println("Could not enable rotation vector");
    }
}

void resetBNO085() {
    // Hardware reset requires a gpio wired to the reset pin on the BNO085
    // bno08x.hardwareReset();

    // Soft-reset triggers initialization sequence calibration
    bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 0);
    setReports();
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
    // static unsigned long lastPrint = 0;
    // const unsigned long PRINT_INTERVAL = 100; // Print every 100ms

    if (bno08x.wasReset()) {
        Serial.println("BNO085 was reset");
        setReports();
    }
    
    if (bno08x.getSensorEvent(&sensorValue)) {
        switch (sensorValue.sensorId) {
            case SH2_GAME_ROTATION_VECTOR:
                quaternion_x = sensorValue.un.rotationVector.i;
                quaternion_y = sensorValue.un.rotationVector.j;
                quaternion_z = sensorValue.un.rotationVector.k;
                quaternion_w = sensorValue.un.rotationVector.real;
                break;
        }

        // Only print every PRINT_INTERVAL milliseconds
        // if (millis() - lastPrint >= PRINT_INTERVAL) {
        //     printBNO085Values();
        //     lastPrint = millis();
        // }
    }
}

void quaternionToEuler() {
    float sqr = sq(quaternion_w);
    float sqi = sq(quaternion_x);
    float sqj = sq(quaternion_y);
    float sqk = sq(quaternion_z);

    ypr.yaw = asin(-2.0 * (quaternion_x * quaternion_z - quaternion_y * quaternion_w) /
                     (sqi + sqj + sqk + sqr));
    ypr.pitch = atan2(2.0 * (quaternion_x * quaternion_y + quaternion_z * quaternion_w),
                    (sqi - sqj - sqk + sqr));
    ypr.roll = atan2(2.0 * (quaternion_y * quaternion_z + quaternion_x * quaternion_w),
                     (-sqi - sqj + sqk + sqr));

    // Convert to degrees
    ypr.yaw = ypr.yaw * RAD_TO_DEG;
    ypr.pitch = ypr.pitch * RAD_TO_DEG;
    ypr.roll = ypr.roll * RAD_TO_DEG;

    // Shift the values by 180 degrees
    // if (ypr.yaw >= 0) {
    //     ypr.yaw -= 180;
    // } else {
    //     ypr.yaw += 180;
    // }

    // ypr.pitch = -ypr.pitch;
    // if (ypr.pitch >= 0) {
    //     ypr.pitch -= 180;
    // } else {
    //     ypr.pitch += 180;
    // }

    // if (ypr.roll >= 0) {
    //     ypr.roll -= 180;
    // } else {
    //     ypr.roll += 180;
    // }
}
