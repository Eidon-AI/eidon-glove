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
    // Use GAME_ROTATION_VECTOR for fast quaternion updates (no magnetic north reference)
    // Reduce to 50Hz (20ms) for stability - prevents overwhelming I2C and BLE
    if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000)) { // 20ms (50Hz) - stable rate
        Serial.println("Could not enable rotation vector at 50Hz, trying 100Hz...");
        if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 10000)) { // 10ms (100Hz) fallback
            Serial.println("Could not enable rotation vector at 100Hz, trying 200Hz...");
            if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 5000)) { // 5ms (200Hz) fallback
                Serial.println("Could not enable rotation vector");
            } else {
                Serial.println("Game rotation vector enabled at 200Hz");
            }
        } else {
            Serial.println("Game rotation vector enabled at 100Hz");
        }
    } else {
        Serial.println("Game rotation vector enabled at 50Hz (stable rate)");
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

void scanI2C() {
    Serial.println("Scanning I2C bus...");
    Serial.print("SDA: GPIO"); Serial.print(I2C_SDA);
    Serial.print(", SCL: GPIO"); Serial.println(I2C_SCL);
    
    int nDevices = 0;
    for (byte address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            Serial.println(" !");
            nDevices++;
        }
        else if (error == 4) {
            Serial.print("Unknown error at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0) {
        Serial.println("No I2C devices found");
    } else {
        Serial.print("Found ");
        Serial.print(nDevices);
        Serial.println(" device(s)");
    }
    Serial.println("Scan complete\n");
}

// Add direct BNO085 communication functions
bool testBNO085Direct() {
    Serial.println("Testing direct BNO085 communication...");
    
    // BNO085 uses SHTP (Sensor Hub Transport Protocol)
    // Try to read the SHTP header to see if we can communicate
    Wire.requestFrom(I2C_ADDR, 4); // Request 4 bytes (SHTP header)
    
    if (Wire.available() >= 4) {
        byte header[4];
        for (int i = 0; i < 4; i++) {
            header[i] = Wire.read();
        }
        
        Serial.print("SHTP Header received: ");
        for (int i = 0; i < 4; i++) {
            if (header[i] < 16) Serial.print("0");
            Serial.print(header[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        
        // Check if this looks like a valid SHTP header
        uint16_t length = (header[1] << 8) | header[0];
        Serial.print("Packet length: ");
        Serial.println(length);
        
        if (length > 0 && length < 1000) { // Reasonable packet length
            Serial.println("Valid SHTP communication detected!");
            return true;
        } else {
            Serial.println("SHTP header format unexpected");
            return false;
        }
    } else {
        Serial.print("Only ");
        Serial.print(Wire.available());
        Serial.println(" bytes available from SHTP request");
        return false;
    }
}

void setupBNO085() {
    Serial.println("Setting up I2C pins for ESP32-C6...");
    Serial.print("SDA: GPIO"); Serial.print(I2C_SDA);
    Serial.print(" (D9), SCL: GPIO"); Serial.print(I2C_SCL);
    Serial.print(" (D10), INT: GPIO"); Serial.print(I2C_INT);
    Serial.println(" (D8)");
    
    Wire.setPins(I2C_SDA, I2C_SCL);
    Wire.begin();
    Wire.setClock(400000); // Set to 400kHz (standard fast mode) for stability
    
    Serial.println("I2C configured with standard speed (400kHz) and correct ESP32-C6 pins");
    
    Serial.println("I2C initialized, testing basic communication...");
    
    // Test basic I2C communication with retries
    int i2c_attempts = 0;
    bool i2c_success = false;
    
    while (!i2c_success && i2c_attempts < 5) {
        Wire.beginTransmission(I2C_ADDR);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.println("I2C communication successful!");
            i2c_success = true;
        } else {
            i2c_attempts++;
            Serial.print("I2C attempt "); Serial.print(i2c_attempts);
            Serial.print(" failed with error: "); Serial.println(error);
            if (i2c_attempts < 5) {
                Serial.println("Retrying I2C in 500ms...");
                delay(500);
            }
        }
    }
    
    if (!i2c_success) {
        Serial.println("I2C communication failed after 5 attempts, giving up on BNO085");
        bno085_available = false;
        return;
    }
    
    // Test direct BNO085 communication
    if (testBNO085Direct()) {
        Serial.println("Direct BNO085 SHTP communication successful!");
    } else {
        Serial.println("Direct BNO085 SHTP communication failed!");
        // Don't return here - still try the Adafruit library
    }
    
    Serial.println("Initializing BNO085 with Adafruit library...");
    
    // Scan the I2C bus
    scanI2C();
    
    // Try multiple initialization approaches with retries
    bool bno_initialized = false;
    int total_attempts = 0;
    const int max_attempts = 15;
    
    while (!bno_initialized && total_attempts < max_attempts) {
        total_attempts++;
        
        Serial.print("BNO085 initialization attempt "); 
        Serial.print(total_attempts); 
        Serial.print("/"); 
        Serial.println(max_attempts);
        
        // Try different initialization methods
        if (total_attempts <= 5) {
            // First 5 attempts: Try with explicit Wire object
            Serial.println("  -> Trying with explicit Wire object...");
            if (bno08x.begin_I2C(I2C_ADDR, &Wire)) {
                Serial.println("BNO085 initialized successfully with explicit Wire object!");
                bno_initialized = true;
                break;
            }
        } else if (total_attempts <= 10) {
            // Next 5 attempts: Try with default address
            Serial.println("  -> Trying with default address...");
            if (bno08x.begin_I2C()) {
                Serial.println("BNO085 initialized successfully with default address!");
                bno_initialized = true;
                break;
            }
        } else {
            // Final attempts: Try with different I2C speeds
            if (total_attempts == 11) {
                Serial.println("  -> Trying with 400kHz I2C clock...");
                // Wire.setClock(400000);
            } else if (total_attempts == 13) {
                Serial.println("  -> Trying with 50kHz I2C clock...");
                // Wire.setClock(50000);
            }
            
            Serial.println("  -> Trying standard initialization...");
            if (bno08x.begin_I2C(I2C_ADDR)) {
                Serial.println("BNO085 initialized successfully!");
                bno_initialized = true;
                break;
            }
        }
        
        // Wait between attempts, with longer waits for later attempts
        int delay_ms = (total_attempts <= 5) ? 500 : 1000;
        Serial.print("  -> Failed, waiting "); Serial.print(delay_ms); Serial.println("ms...");
        delay(delay_ms);
        
        // Rescan I2C bus every 5 attempts to verify device is still there
        if (total_attempts % 5 == 0) {
            Serial.println("  -> Rescanning I2C bus...");
            scanI2C();
            
            // Reset I2C if device is not responding
            Wire.beginTransmission(I2C_ADDR);
            if (Wire.endTransmission() != 0) {
                Serial.println("  -> Device not responding, reinitializing I2C...");
                Wire.end();
                delay(100);
                Wire.setPins(I2C_SDA, I2C_SCL);
                Wire.begin();
                // Wire.setClock(100000);
            }
        }
    }
    
    if (!bno_initialized) {
        Serial.print("BNO085 failed to initialize after ");
        Serial.print(max_attempts);
        Serial.println(" attempts. Continuing without IMU...");
        bno085_available = false;
        return;
    }

    Serial.print("BNO085 successfully initialized on attempt ");
    Serial.print(total_attempts);
    Serial.println("!");
    
    bno085_available = true;
    setReports();
    
    Serial.println("BNO085 setup complete - quaternion data should be available");
}

void updateBNO085() {
    // Don't try to update if BNO085 is not available
    if (!bno085_available) {
        return;
    }
    
    // Remove debug printing to eliminate delays
    // static unsigned long lastPrint = 0;
    // const unsigned long PRINT_INTERVAL = 100; // Print every 100ms for faster feedback

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

        // Debug printing disabled for maximum performance
        // Print quaternion values every PRINT_INTERVAL milliseconds
        // if (millis() - lastPrint >= PRINT_INTERVAL) {
        //     Serial.print("Quaternion - X: "); Serial.print(quaternion_x, 4);
        //     Serial.print(" Y: "); Serial.print(quaternion_y, 4);
        //     Serial.print(" Z: "); Serial.print(quaternion_z, 4);
        //     Serial.print(" W: "); Serial.print(quaternion_w, 4);
        //     
        //     // Also show magnitude to verify it's normalized (should be ~1.0)
        //     float magnitude = sqrt(quaternion_x*quaternion_x + quaternion_y*quaternion_y + 
        //                          quaternion_z*quaternion_z + quaternion_w*quaternion_w);
        //     Serial.print(" |Mag: "); Serial.print(magnitude, 4);
        //     Serial.println("|");
        //     
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
