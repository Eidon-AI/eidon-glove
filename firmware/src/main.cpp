#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <NimBLECharacteristic.h>
#include "FingerTracking.h"
#include "BNO085.h"
#include "HallEffectSensors.h"

// Define the number of axes we'll use
#define NUM_JOINTS 16  // We want all 16 joints

// Define the button pin for the Xiao ESP32-C3
#define BUTTON_PIN  9

// Add these at the top of your file with other global variables
#define BUTTON_COUNT 5                  // Number of finger buttons we're tracking
#define PRESS_THRESHOLD 150             // Absolute threshold for press detection
#define RELEASE_THRESHOLD 130           // Threshold for release detection
#define NOISE_TOLERANCE 5               // Tolerance for signal noise
#define DEBOUNCE_TIME 50                // Very short debounce time for responsiveness
#define POSITION_HISTORY_SIZE 10       // Number of samples to track for position changes
#define HISTORY_SIZE 3                  // Small history size for minimal lag

// Define deadzone parameters
#define DEADZONE 32                   // Size of the deadzone (in output units, 0-255)
#define ANALOG_CENTER 127             // Center value for analog stick

// Updated HID Report Descriptor to use more standard axis definitions
const uint8_t reportDescriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)

    // Constant value (1 byte)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Constant, Variable, Absolute)
    
    // Buttons (16 buttons)
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        // Usage Minimum (Button 1)
    0x29, 0x10,        // Usage Maximum (Button 16)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x10,        // Report Count (16)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    
    // First 8 axes - using standard axis definitions
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x09, 0x32,        // Usage (Z)
    0x09, 0x33,        // Usage (Rx)
    0x09, 0x34,        // Usage (Ry)
    0x09, 0x35,        // Usage (Rz)
    0x09, 0x36,        // Usage (Slider)
    0x09, 0x37,        // Usage (Dial)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x08,        // Report Count (8)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    
    // Second set of 8 axes - using additional standard controls
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x38,        // Usage (Wheel)
    0x09, 0x39,        // Usage (Hat switch)
    0x09, 0x3A,        // Usage (Counted Buffer)
    0x09, 0x3B,        // Usage (Byte Count)
    0x09, 0x3C,        // Usage (Motion Wakeup)
    0x09, 0x3D,        // Usage (Start)
    0x09, 0x3E,        // Usage (Select)
    0x09, 0x3F,        // Usage (Vector)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x08,        // Report Count (8)
    0x81, 0x02,        // Input (Data, Variable, Absolute)

    // Add quaternion values (4 more axes)
    0x05, 0x02,        // Usage Page (Simulation Controls)
    0x09, 0xBA,        // Usage (Rudder)
    0x09, 0xBB,        // Usage (Throttle)
    0x09, 0xC4,        // Usage (Accelerator)
    0x09, 0xC5,        // Usage (Brake)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x04,        // Report Count (4)
    0x81, 0x02,        // Input (Data, Variable, Absolute)

    // Add linear acceleration axes (3 more axes)
    0x05, 0x02,        // Usage Page (Simulation Controls)
    0x09, 0xB0,        // Usage (X-axis acceleration)
    0x09, 0xB1,        // Usage (Y-axis acceleration)
    0x09, 0xB2,        // Usage (Z-axis acceleration)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x03,        // Report Count (3)
    0x81, 0x02,        // Input (Data, Variable, Absolute)

    0xC0               // End Collection
};

// Variables to store joint values and button state
// uint8_t reportData[NUM_JOINTS + 2] = {0}; // +1 for button state

// BLE objects
NimBLEServer* pServer = nullptr;
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputGamepad = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Define an enum for the different modes
enum ControlMode {
    GAME_MODE = 0,       // Mapped controls for gameplay
    RAW_ANGLES_MODE = 1, // Show all raw angle values
    // Add more modes as needed in the future
    MODE_COUNT           // Always keep this as the last item to track the number of modes
};

// Add this at the top of your file with other global variables
ControlMode currentMode = RAW_ANGLES_MODE;
bool modeJustChanged = true;         // Flag to indicate when mode has just changed

// Structure to track finger motion for button detection
struct FingerButtonState {
    int32_t baselineAngle;    // Baseline angle (calibrated at start)
    int32_t prevAngle;        // Previous angle reading
    bool isPressed;           // Current button state
    unsigned long lastChange; // Timestamp of last state change
};

// Array to track state for each finger button
FingerButtonState fingerButtons[BUTTON_COUNT];

// Finger indices for button detection
const int fingerIndices[BUTTON_COUNT] = {2, 5, 8, 11, 14}; // Thumb, Index, Middle, Ring, Pinky

// Track recent motion history
int32_t angleHistory[BUTTON_COUNT][HISTORY_SIZE];

// Track average motion range for each finger
int32_t avgMotionRange[BUTTON_COUNT] = {0};

// Define arrays for finger-specific thresholds
const int32_t PRESS_THRESHOLDS[BUTTON_COUNT] = {
    120,  // Thumb 0 (Thumb)
    200,  // Index 0 (Index) - Standard threshold
    200,  // Middle 1 (Middle) - Higher threshold (less sensitive)
    200,  // Ring 2 (Ring) - Medium-high threshold
    200   // Pinky 3 (Pinky) - Lower threshold (more sensitive)
};

const int32_t RELEASE_THRESHOLDS[BUTTON_COUNT] = {
    110,  // Thumb 0 (Thumb)
    192,  // Index 1 (Index)
    192,  // Middle 2 (Middle)
    192,  // Ring 3 (Ring)
    192,   // Pinky 4 (Pinky)
};

// Initialize the finger button tracking
void initFingerButtons() {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        fingerButtons[i].baselineAngle = 0;  // Will be calibrated later
        fingerButtons[i].prevAngle = 0;
        fingerButtons[i].isPressed = false;
        fingerButtons[i].lastChange = 0;
        
        // Initialize history array
        for (int j = 0; j < HISTORY_SIZE; j++) {
            angleHistory[i][j] = 0;
        }
    }
}

// Server callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("Client connected!");
        deviceConnected = true;
    };

    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("Client disconnected");
        deviceConnected = false;
    };
};

// Security callback to accept pairing requests
class SecurityCallbacks : public NimBLESecurityCallbacks {
    uint32_t onPassKeyRequest() {
        Serial.println("Passkey request");
        return 123456; // Just use a simple passkey for testing
    }

    void onPassKeyNotify(uint32_t pass_key) {
        Serial.print("Passkey Notify: ");
        Serial.println(pass_key);
    }

    bool onConfirmPIN(uint32_t pass_key) {
        Serial.print("Confirm PIN: ");
        Serial.println(pass_key);
        return true;
    }

    bool onSecurityRequest() {
        Serial.println("Security Request");
        return true;
    }

    void onAuthenticationComplete(ble_gap_conn_desc* desc) {
        Serial.println("Authentication Complete");
        Serial.print("Secure: ");
        Serial.println(desc->sec_state.encrypted ? "Yes" : "No");
    }
};

// Function to map angle values to the 0-255 range needed for HID
uint8_t mapAngleToHID(int32_t angle, int32_t minAngle, int32_t maxAngle) {
    // Constrain the angle to the min-max range
    int32_t constrainedAngle = constrain(angle, minAngle, maxAngle);
    
    // Map to 0-255 range for HID
    return map(constrainedAngle, minAngle, maxAngle, 0, 255);
}

// Gamepad descriptor layout for buttons and axes
typedef struct {
  // Action buttons
  bool button1 : 1;
  bool button2 : 1;
  bool button3 : 1;
  bool button4 : 1;
  bool button5 : 1;
  bool button6 : 1;
  bool button7 : 1;
  bool button8 : 1;
  bool button9 : 1;
  bool button10 : 1;
  bool button11 : 1;
  bool button12 : 1;

  // D-pad buttons
  bool up : 1;
  bool right : 1;
  bool down : 1;
  bool left : 1;

  // All 23 axes
  uint8_t axes[23]; // Updated to 23 total axes
} GamepadReport;

// Create an instance of the gamepad report
GamepadReport gamepadReport = {0};

// Add function to convert quaternion to gamepad axis value
uint8_t quaternionToAxis(float quat_val) {
    // Map -1.0 to 1.0 to 0-255
    return (uint8_t)((quat_val + 1.0f) * 127.5f);
}

// Add this function before setup()
void printHIDDescriptor() {
    Serial.println("HID Report Descriptor:");
    for (size_t i = 0; i < sizeof(reportDescriptor); i++) {
        if (reportDescriptor[i] < 16) Serial.print("0");
        Serial.print(reportDescriptor[i], HEX);
        Serial.print(" ");
        if ((i + 1) % 8 == 0) Serial.println();
    }
    Serial.println();
    Serial.print("Total descriptor size: ");
    Serial.println(sizeof(reportDescriptor));
    Serial.print("GamepadReport struct size: ");
    Serial.println(sizeof(GamepadReport));
}

// Function to cycle to the next mode
void cycleToNextMode() {
    currentMode = static_cast<ControlMode>((currentMode + 1) % MODE_COUNT);
    modeJustChanged = true;
    
    // Print the new mode
    Serial.print("Mode changed to: ");
    switch (currentMode) {
        case GAME_MODE:
            Serial.println("Game Mode");
            break;
        case RAW_ANGLES_MODE:
            Serial.println("Raw Angles Mode");
            break;
        default:
            Serial.println("Unknown Mode");
            break;
    }
}

// Function to apply deadzone with proper rescaling
uint8_t applyDeadzone(int32_t rawValue, uint8_t deadzone) {
    // Center around zero for easier math
    int32_t centered = rawValue - ANALOG_CENTER;
    
    // Check if within deadzone
    if (abs(centered) <= deadzone/2) {
        return ANALOG_CENTER; // Return center value
    }
    
    // Rescale values outside deadzone to use full range
    // This ensures smooth transition from deadzone edge to max values
    if (centered > 0) {
        // Positive side (127...255)
        // Map from (deadzone/2...127) to (0...127)
        return ANALOG_CENTER + map(centered - deadzone/2, 
                                  0, 
                                  127 - deadzone/2,
                                  0, 
                                  127);
    } else {
        // Negative side (0...127)
        // Map from (-127...-deadzone/2) to (-127...0)
        return ANALOG_CENTER + map(centered + deadzone/2, 
                                  -127 + deadzone/2, 
                                  0,
                                  -127, 
                                  0);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial time to connect
    
    Serial.println("\n\n----- Eidon Glove Starting -----");
    Serial.println("Initializing finger tracking...");
    
    // Define which sensors have inverted magnets
    bool invertedSensors[SENSOR_COUNT] = {
        false, false, false, false,  // Thumb (0-3)
        false, true, false,           // Index (4-6)
        false, true, false,           // Middle (7-9)
        false, true, false,          // Ring (10-12)
        false, true, false          // Pinky (13-15)
    };
    
    // Initialize the finger tracking system with inverted sensor configuration
    fingerTrackingSetup(invertedSensors);
    
    Serial.println("Initializing BLE Gamepad...");
    
    // Set a fixed device name and address for consistent pairing
    NimBLEDevice::init("Eidon Glove (Right)");
    
    // Optional: Set a fixed MAC address (uncomment if needed)
    // uint8_t customAddress[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    // esp_base_mac_addr_set(customAddress);
    
    // Configure security for reliable pairing
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityCallbacks(new SecurityCallbacks());
    
    // Set consistent power level
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    // Create server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    // Create HID device with consistent settings
    hid = new NimBLEHIDDevice(pServer);
    inputGamepad = hid->inputReport(1); // Report ID 1
    
    // Set consistent manufacturer name
    hid->manufacturer()->setValue("ESP32-C3");
    
    // Use consistent VID/PID
    hid->pnp(0x01, 0x303A, 0xABCD, 0x0110);
    hid->hidInfo(0x00, 0x01);
    
    // Set report descriptor
    hid->reportMap((uint8_t*)reportDescriptor, sizeof(reportDescriptor));
    
    // Print the HID descriptor for debugging
    // printHIDDescriptor();
    
    // Start the HID device
    hid->startServices();
    
    // Configure advertising with consistent settings
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(HID_GAMEPAD);
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->setScanResponse(true);
    pAdvertising->setName("Hand Tracker (Right)"); // Must match the init name
    
    // Start advertising
    pAdvertising->start();
    
    Serial.println("BT Gamepad initialized!");
    Serial.println("Device name: Hand Tracker (Right)");
    Serial.println("The device should now be visible in your Bluetooth settings.");
    Serial.println("Please pair with it from your computer or mobile device.");
    Serial.println("----- Initialization Complete -----");
    
    // Initialize finger button tracking
    initFingerButtons();
    
    setupBNO085();            // New BNO085 setup
}

// Function to update finger button states based on position changes
void updateFingerButtons() {
    unsigned long currentTime = millis();
    static bool isCalibrated = false;
    
    // One-time calibration of baseline angles
    if (!isCalibrated) {
        Serial.println("Calibrating finger baseline positions...");
        // Wait a moment for sensors to stabilize
        delay(500);
        
        // Take multiple readings and average them for baseline
        const int calibrationSamples = 10;
        for (int i = 0; i < BUTTON_COUNT; i++) {
            int32_t sum = 0;
            for (int j = 0; j < calibrationSamples; j++) {
                calcFingerAngles();
                sum += angles[fingerIndices[i]];
                delay(20);
            }
            fingerButtons[i].baselineAngle = 0; // sum / calibrationSamples;
            fingerButtons[i].prevAngle = fingerButtons[i].baselineAngle;
            
            // Serial.print("Finger ");
            // Serial.print(i);
            // Serial.print(" baseline: ");
            // Serial.print(fingerButtons[i].baselineAngle);
            // Serial.print(" (Press threshold: ");
            // Serial.print(PRESS_THRESHOLDS[i]);
            // Serial.print(", Release threshold: ");
            // Serial.print(RELEASE_THRESHOLDS[i]);
            // Serial.println(")");
        }
        isCalibrated = true;
        Serial.println("Calibration complete!");
    }
    
    // Debug output - print values periodically
    static unsigned long lastDebugTime = 0;
    bool shouldPrintDebug = false; //(millis() - lastDebugTime > 500);
    
    if (shouldPrintDebug) {
        lastDebugTime = millis();
        Serial.println("Finger position values:");
    }
    
    for (int i = 0; i < BUTTON_COUNT; i++) {
        // Get current angle for this finger
        int32_t currentAngle = angles[fingerIndices[i]];
        
        // Calculate distance from baseline (rest position)
        int32_t distanceFromBaseline = currentAngle - fingerButtons[i].baselineAngle;
        
        // Print debug info
        if (shouldPrintDebug) {
            Serial.print("Finger ");
            Serial.print(i);
            Serial.print(": Angle=");
            Serial.print(currentAngle);
            Serial.print(" Baseline=");
            Serial.print(fingerButtons[i].baselineAngle);
            Serial.print(" Distance=");
            Serial.print(distanceFromBaseline);
            Serial.print(" State=");
            Serial.println(fingerButtons[i].isPressed ? "PRESSED" : "released");
        }
        
        // Very simple state machine based on absolute position relative to baseline
        if (!fingerButtons[i].isPressed) {
            // Check for press - need to exceed finger-specific threshold
            if (distanceFromBaseline > PRESS_THRESHOLDS[i] && 
                (currentTime - fingerButtons[i].lastChange > DEBOUNCE_TIME)) {
                
                fingerButtons[i].isPressed = true;
                fingerButtons[i].lastChange = currentTime;
                
                Serial.print("BUTTON ");
                Serial.print(i + 1);
                Serial.print(" PRESSED! (Distance: ");
                Serial.print(distanceFromBaseline);
                Serial.print(", Threshold: ");
                Serial.print(PRESS_THRESHOLDS[i]);
                Serial.println(")");
            }
        } else {
            // Check for release - need to return close to baseline
            if (distanceFromBaseline < RELEASE_THRESHOLDS[i] && 
                (currentTime - fingerButtons[i].lastChange > DEBOUNCE_TIME)) {
                
                fingerButtons[i].isPressed = false;
                fingerButtons[i].lastChange = currentTime;
                
                Serial.print("BUTTON ");
                Serial.print(i + 1);
                Serial.print(" RELEASED! (Distance: ");
                Serial.print(distanceFromBaseline);
                Serial.print(", Threshold: ");
                Serial.print(RELEASE_THRESHOLDS[i]);
                Serial.println(")");
            }
        }
        
        // Store current angle for next iteration
        fingerButtons[i].prevAngle = currentAngle;
    }
}

void loop() {
    // Print connection status every 3 seconds
    // static unsigned long lastStatusTime = 0;
    // if (millis() - lastStatusTime > 3000) {
    //     lastStatusTime = millis();
    //     Serial.print("BLE connection status: ");
    //     Serial.println(deviceConnected ? "Connected" : "Waiting for connection...");
        
    //     // Print more detailed connection info
    //     if (pServer != nullptr) {
    //         Serial.print("Connected clients: ");
    //         Serial.println(pServer->getConnectedCount());
    //     }
    // }
    
    // Handle connection state changes
    if (deviceConnected && !oldDeviceConnected) {
        // Just connected
        Serial.println("Connected - starting to send data");
        oldDeviceConnected = deviceConnected;
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        // Just disconnected
        Serial.println("Disconnected - restarting advertising");
        // delay(500); // Give BLE stack time to get ready
        NimBLEDevice::startAdvertising();
        Serial.println("Advertising restarted");
        oldDeviceConnected = deviceConnected;
    }
    
    // Update finger tracking data
    calcFingerAngles();

    // Update BNO085 data
    updateBNO085();

    // Send data if connected
    if (deviceConnected) {
        // Read the button state from the Xiao ESP32-C3
        int buttonState = !digitalRead(BUTTON_PIN);
        
        // Toggle between modes on button release
        static bool lastButtonState = false;
        if (!buttonState && lastButtonState) {  // Button was released
            cycleToNextMode();
        }
        lastButtonState = buttonState;
        
        // Update the gamepad report structure
        // Clear all buttons first
        memset(&gamepadReport, 0, sizeof(GamepadReport));
        
        // Set button6 based on the physical button
        // gamepadReport.button6 = buttonState;
        
        // Set buttons to indicate current mode (optional)
        // gamepadReport.button1 = (currentMode == GAME_MODE);
        // gamepadReport.button2 = (currentMode == RAW_ANGLES_MODE);
        
        // Process data based on the current mode
        switch (currentMode) {
            case GAME_MODE:
                // GAME MODE: Use mapped controls for gameplay

                // Update finger button states based on position changes
                updateFingerButtons();

                // Set button states based on detected gestures
                gamepadReport.button1 = fingerButtons[0].isPressed; // Thumb
                gamepadReport.button2 = fingerButtons[1].isPressed; // Pinky finger
                gamepadReport.button3 = fingerButtons[2].isPressed; // Ring finger
                gamepadReport.button4 = fingerButtons[3].isPressed; // Middle finger
                gamepadReport.button5 = fingerButtons[4].isPressed; // Index finger

                // Map roll angle to X-axis (left/right movement)
                gamepadReport.axes[0] = constrain(map(ypr.roll, -45, 45, 0, 255), 0, 255);

                // Map pitch angle to Y-axis (up/down movement)
                // gamepadReport.axes[1] = constrain(map(ypr.pitch, -45, 45, 0, 255), 0, 255);

                // Apply deadzone to both axes
                // gamepadReport.axes[0] = applyDeadzone(gamepadReport.axes[0], DEADZONE);
                // gamepadReport.axes[1] = applyDeadzone(gamepadReport.axes[1], DEADZONE);

                // Fill remaining axes with zeros or other mapped values
                for (int i = 2; i < 16; i++) {
                    gamepadReport.axes[i] = 127;
                }
                break;
                
            case RAW_ANGLES_MODE:
                // RAW ANGLES MODE: Show all raw angle values

                // Map all raw angle values directly to axes
                for (int i = 0; i < NUM_JOINTS && i < 16; i++) {
                    gamepadReport.axes[i] = mapAngleToHID(angles[i], 0, 255);
                }

                // Add quaternion values to the next 4 axes
                gamepadReport.axes[16] = quaternionToAxis(quaternion_x);
                gamepadReport.axes[17] = quaternionToAxis(quaternion_y);
                gamepadReport.axes[18] = quaternionToAxis(quaternion_z);
                gamepadReport.axes[19] = quaternionToAxis(quaternion_w);

                // Add linear acceleration values to the next 3 axes
                // Map from typical acceleration range (-8 to +8 m/s²) to 0-255
                gamepadReport.axes[20] = constrain(map(linear_x * 16, -128, 127, 0, 255), 0, 255);
                gamepadReport.axes[21] = constrain(map(linear_y * 16, -128, 127, 0, 255), 0, 255);
                gamepadReport.axes[22] = constrain(map(linear_z * 16, -128, 127, 0, 255), 0, 255);
                break;

            default:
                // Fallback mode - just use raw angles
                for (int i = 0; i < NUM_JOINTS && i < 16; i++) {
                    gamepadReport.axes[i] = mapAngleToHID(angles[i], 0, 255);
                }
                break;
        }
        
        if (inputGamepad != nullptr) {
            // Convert the struct to a byte array for sending
            uint8_t reportBuffer[sizeof(GamepadReport)];
            memcpy(reportBuffer, &gamepadReport, sizeof(GamepadReport));
            
            // Send the report
            inputGamepad->setValue(reportBuffer, sizeof(reportBuffer));
            inputGamepad->notify();
            
            // Debug output - only show when mode changes or periodically
            static unsigned long lastDebugTime = 0;
            
            if (modeJustChanged || millis() - lastDebugTime > 100) {
                lastDebugTime = millis();
                
                // Serial.print("Current mode: ");
                switch (currentMode) {
                    // case GAME_MODE:
                    //     Serial.println("Game Mode");
                    //     Serial.println("Game controls active - mapped for gameplay");
                    //     break;
                    case RAW_ANGLES_MODE:
                        // Serial.println("Raw Angles Mode");
                        // Serial.println("Showing raw angle values on all axes");
                        // printRawAngles();
                        // printFingerAngles();
                        break;
                    default:
                        // Serial.println("Unknown Mode");
                        break;
                }
                
                // // Print a few values for verification
                // for (int i = 0; i < NUM_JOINTS; i++) {
                //     Serial.print("Angle ");
                //     Serial.print(i);
                //     Serial.print(": ");
                //     Serial.print(angles[i]);
                //     Serial.print(" -> Axis value: ");
                //     Serial.println(gamepadReport.axes[i]);
                // }
                // Serial.println("...");
                
                modeJustChanged = false;
            }
        } else {
            Serial.println("Error: inputGamepad is null");
        }
        
        // Small delay to prevent flooding
        delay(1); // High update rate
    } else {
        // Even when not connected, calculate and display angles for debugging
        // static unsigned long lastDebugTime = 0;
        // if (millis() - lastDebugTime > 1000) { // Only print once per second
        //     lastDebugTime = millis();
        //     Serial.println("Current finger angles (not connected):");
        //     printFingerAngles();
        // }
        
        // Slower update rate when not connected to save power
        // delay(100);
    }

    // In your loop function
    if (!deviceConnected && !NimBLEDevice::getAdvertising()->isAdvertising()) {
        Serial.println("Restarting advertising to reconnect...");
        NimBLEDevice::startAdvertising();
    }

    // Add this to your loop to monitor memory
    // static unsigned long lastMemCheck = 0;
    // if (millis() - lastMemCheck > 10000) {
    //     lastMemCheck = millis();
    //     Serial.print("Free heap: ");
    //     Serial.println(ESP.getFreeHeap());
    // }
}
