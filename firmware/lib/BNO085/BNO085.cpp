#include "BNO085.h"

Adafruit_BNO08x bno08x;
sh2_SensorValue_t sensorValue;

float quaternion_x = 0;
float quaternion_y = 0;
float quaternion_z = 0;
float quaternion_w = 1;

euler_t ypr = {0, 0, 0};
bool bno085_available = false;  // Track if BNO085 is working

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

// Add interrupt flag for faster sensor reading
volatile bool sensorDataReady = false;

// Interrupt service routine
void sensorISR() {
    sensorDataReady = true;
}

void setupBNO085() {
    Wire.setPins(I2C_SDA, I2C_SCL);
    Wire.begin();
    
    Serial.println("Initializing BNO085...");
    
    // Keep trying to initialize the sensor until it's found
    int attempt = 1;
    
    while (!bno08x.begin_I2C(I2C_ADDR)) {
        Serial.print("BNO085 initialization attempt ");
        Serial.print(attempt);
        Serial.println(" failed, retrying in 1 second...");
        delay(1000);  // Wait 1 second before trying again
        attempt++;
        
        // Optional: Add a maximum retry limit if you want
        // if (attempt > 30) {
        //     Serial.println("BNO085 failed after 30 attempts, continuing without IMU...");
        //     bno085_available = false;
        //     return;  // Continue without BNO085
        // }
    }

    // pinMode(I2C_INT, INPUT_PULLUP);
    // attachInterrupt(digitalPinToInterrupt(I2C_INT), sensorISR, FALLING);

    Serial.print("BNO085 Found on attempt ");
    Serial.print(attempt);
    Serial.println("!");
    bno085_available = true;  // Mark sensor as available
    setReports();
}

void updateBNO085() {
    // Don't try to update if BNO085 is not available
    if (!bno085_available) {
        return;
    }
    
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

bool isBNO085Available() {
    return bno085_available;
}
