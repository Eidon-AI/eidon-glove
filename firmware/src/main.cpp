#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <NimBLEHIDDevice.h>
#include <NimBLECharacteristic.h>
#include "BNO085.h"
#include "FingerTracking.h"
#include "HallEffectSensors.h"
#include <Preferences.h>

// Set to true for left hand, false for right hand
#define IS_LEFT_HAND 0

// Vendor and Product IDs
#define VENDOR_ID  0xE1D0 // Eidon AI vendor ID
#define PRODUCT_ID 0x0001 // Eidon Glove v1 product ID

// Define the number of axes we'll use
#define NUM_JOINTS 16  // We want all 16 joints

// Define the button pin for the Xiao ESP32-C3
#define BUTTON_BOOT_PIN  9 // user button on Xiao
#define BUTTON_MODE_PIN  10 // button on pcb

// Define the LED pin
#define LED_PIN  5

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

// LED status variables
unsigned long ledLastUpdate = 0;
// LED patterns
enum LEDPattern {
    LED_ADVERTISING,  // Strobing brightness when advertising
    LED_CONNECTED,    // Fast blinking when connected
    LED_IMU_RESET     // Solid LED when resetting IMU
};
LEDPattern currentLEDPattern = LED_ADVERTISING;
unsigned long imuResetStartTime = 0;
const unsigned long IMU_RESET_DURATION = 300; // Solid LED duration in ms
int ledBrightness = 0;
bool ledState = false;

// Simple timer for debugging LED
unsigned long debugLedTimer = 0;
const unsigned long LED_FLASH_INTERVAL = 50; // Very slow flash for debugging

// LED brightness settings
#define LED_DIM_BRIGHTNESS 50  // Dim brightness level (0-255) when connected

// Button hold calibration reset constants
#define CALIBRATION_RESET_HOLD_TIME 3000  // Hold for 3 seconds to reset calibration
bool buttonHoldInProgress = false;
unsigned long buttonHoldStartTime = 0;
bool calibrationResetTriggered = false;

/* One top-level application collection, Usage = Gamepad                    */
/*  ├─ Input  (Button/Flags, Finger Angle Bytes, Quaternion)                */
/*  ├─ Output (Vendor byte)                                                 */
/*  └─ Feature(RGB)                                                         */

const uint8_t hid_report_descriptor[] = {

    /* -----------------------------------------------------------------------
     * Top-level collection : sensor orientation + vendor channel, ID = 1
     * ---------------------------------------------------------------------*/
    0x05, 0x01,            // UsagePage (Generic Desktop)
    0x09, 0x05,            // Usage (Gamepad)
    0xA1, 0x01,            // Collection (Application)

        0x85, 0x01,        //   Report ID (1)

        /* --- button flags : 16-bits ------------------------------------ */
        0x05, 0x09,        // Usage Page (Button)
        0x19, 0x01,        // Usage Minimum (Button 1)
        0x29, 0x10,        // Usage Maximum (Button 16)
        0x15, 0x00,        // Logical Minimum (0)
        0x25, 0x01,        // Logical Maximum (1)
        0x75, 0x01,        // Report Size (1)
        0x95, 0x10,        // Report Count (16)
        0x81, 0x02,        // Input (Data, Variable, Absolute)
        
        /* --- finger angles : 16 8-bit axes ------------------------------ */
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x30,        // Usage (X)
        0x09, 0x31,        // Usage (Y)
        0x09, 0x32,        // Usage (Z)
        0x09, 0x33,        // Usage (Rx)
        0x09, 0x34,        // Usage (Ry)
        0x09, 0x35,        // Usage (Rz)
        0x09, 0x36,        // Usage (Slider)
        0x09, 0x37,        // Usage (Dial)
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
        0x95, 0x10,        // Report Count (16)
        0x81, 0x02,        // Input (Data, Variable, Absolute)

        /* --- quaternion orientation (Sensor page) ------------------------ */
        0x05, 0x20,                    // Usage Page (Sensor)
        0x09, 0x80,                    // Usage (Orientation)
        /* --- 4×16-bit quaternion components (i, j, k, real) -------------- */
        0x0A, 0x83, 0x04,              // Usage 0x0483 – Data Field: Quaternion
        0x75, 0x10,                    // Report Size (16)
        0x95, 0x04,                    // Report Count (4)
        0x17, 0x00, 0x00, 0x00, 0x00,  // Logical Minimum 0 (32-bit)
        0x27, 0xFF, 0xFF, 0x00, 0x00,  // Logical Maximum 65535 (32-bit)
        0x81, 0x02,                    // Input (Data,Var,Abs)

        /* ------------------------------------------------------------------
        * Vendor-defined channel : Output (1 byte) – command interface
        * ---------------------------------------------------------------- */
        0x06, 0x00, 0xFF,     // Usage Page (Vendor 0xFF00)
        0x09, 0x01,           // Usage 1
        0x15, 0x00,           // Logical Minimum (0)
        0x26, 0xFF, 0x00,     // Logical Maximum (255)
        0x75, 0x08,           // Report Size (8)
        0x95, 0x01,           // Report Count (1)
        0x91, 0x02,           // Output (Data,Var,Abs)

        /* ------------------------------------------------------------------
        * Vendor-defined Feature report : saved RGB (3 bytes)
        * ---------------------------------------------------------------- */
        0x09, 0x02,           // Usage (Vendor 2)
        0x15, 0x00,           // Logical Minimum (0)
        0x26, 0xFF, 0x00,     // Logical Maximum (255)
        0x95, 0x03,           // Report Count 3
        0x75, 0x08,           // Report Size 8
        0xB1, 0x02,           // Feature (Data,Var,Abs)

    0xC0                  // End Application Collection
};

// Variables to store joint values and button state
// uint8_t reportData[NUM_JOINTS + 2] = {0}; // +1 for button state

// BLE objects
NimBLEServer* pServer = nullptr;
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputGamepad = nullptr;
NimBLECharacteristic* outputGamepad = nullptr;
NimBLECharacteristic* featureColor = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Persistent storage
Preferences prefs;

// Device color (RGB)
uint8_t deviceColor[3] = {0xFF, 0xFF, 0xFF};

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

  bool cfgbit0 : 1; // isLeftHand
  bool cfgbit1 : 1;
  bool cfgbit2 : 1;
  bool cfgbit3 : 1;
  bool cfgbit4 : 1;
  bool cfgbit5 : 1;
  bool cfgbit6 : 1;
  bool cfgbit7 : 1;

  // All 23 axes
  uint8_t axes[16]; // Updated to 23 total axes

  // 4 values for quaternion (w, x, y, z)
  uint16_t quaternion[4];
} GamepadReport;

// Create an instance of the gamepad report
GamepadReport gamepadReport = {0};

// Add function to convert quaternion to gamepad axis value
uint16_t quaternionToAxis(float quat_val) {
    // Map -1.0 to 1.0 to 0-65535
    return (uint16_t)((quat_val + 1.0f) * 32767.5f);
}

// Add this function before setup()
void printHIDDescriptor() {
    Serial.println("HID Report Descriptor:");
    for (size_t i = 0; i < sizeof(hid_report_descriptor); i++) {
        if (hid_report_descriptor[i] < 16) Serial.print("0");
        Serial.print(hid_report_descriptor[i], HEX);
        Serial.print(" ");
        if ((i + 1) % 8 == 0) Serial.println();
    }
    Serial.println();
    Serial.print("Total descriptor size: ");
    Serial.println(sizeof(hid_report_descriptor));
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

// Helper to generate unique BLE name "Eidon Glove-XXXX" using low 2 bytes of the MAC
static std::string generateUniqueName() {
    NimBLEAddress addr = NimBLEDevice::getAddress(); // e.g. "aa:bb:cc:dd:ee:ff"
    std::string mac = addr.toString();
    // Extract last 4 hex characters (low 16-bits) ignoring ':'
    std::string suffix;
    for (int i = mac.size() - 2; i >= 0 && suffix.size() < 4; --i) {
        if (mac[i] != ':') suffix.insert(suffix.begin(), (char)toupper(mac[i]));
    }
    char name[32];
    snprintf(name, sizeof(name), "Eidon Glove-%s", suffix.c_str());
    return std::string(name);
}

// Callback to handle data sent from host (Output report)
class OutputReportCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (!value.empty()) {
            uint8_t cmd = static_cast<uint8_t>(value[0]);
            Serial.print("Output report received, cmd=0x");
            Serial.println(cmd, HEX);

            if (cmd == 0x01) {
                if (isBNO085Available()) {
                    Serial.println("Reset command: resetting BNO085");
                    resetBNO085();
                } else {
                    Serial.println("Reset command received, but BNO085 is not available");
                }
            }
            // Additional commands can be added here
        }
    }
};

// Callback for Feature report read/write (RGB color)
class FeatureReportCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* pChar) override {
        // Manually prepend report ID 1 to the color data
        uint8_t reportData[4];
        reportData[0] = 0x01;  // Report ID
        memcpy(reportData + 1, deviceColor, 3);
        pChar->setValue(reportData, 4);
    }
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        size_t len = value.size();
        if (len == 4) {
            // Host included Report ID as first byte – skip it
            memcpy(deviceColor, value.data() + 1, 3);
        } else if (len >= 3) {
            memcpy(deviceColor, value.data(), 3);
        } else {
            return; // invalid length
        }

        // Persist to NVS
        prefs.putBytes("color", deviceColor, 3);
        Serial.printf("Color updated to %02X %02X %02X and saved to flash\n", deviceColor[0], deviceColor[1], deviceColor[2]);
    }
};

// Function to update LED status based on current state
void updateLEDStatus() {
    unsigned long currentTime = millis();
    
    // Special test mode for connected state LED
    if (deviceConnected) {
        // SIMPLIFIED: Just toggle LED every LED_FLASH_INTERVAL ms when connected
        if (currentTime - debugLedTimer >= LED_FLASH_INTERVAL) {
            debugLedTimer = currentTime;
            // Toggle between full on and full off for debugging
            ledState = !ledState;
            
            if (ledState) {
                // Serial.println("TEST MODE: LED ON");
                digitalWrite(LED_PIN, HIGH); // Full ON for testing
            } else {
                // Serial.println("TEST MODE: LED OFF");
                digitalWrite(LED_PIN, LOW);  // Full OFF
            }
        }
        return; // Skip normal LED logic when connected
    }
    
    // Handle IMU reset pattern with priority
    if (currentLEDPattern == LED_IMU_RESET) {
        digitalWrite(LED_PIN, HIGH); // Solid ON during IMU reset
        
        // Check if IMU reset period is over
        if (currentTime - imuResetStartTime >= IMU_RESET_DURATION) {
            // Return to appropriate pattern based on connection state
            currentLEDPattern = deviceConnected ? LED_CONNECTED : LED_ADVERTISING;
            ledLastUpdate = currentTime; // Reset timer to start new pattern immediately
        }
        return;
    }
    
    // Only handle advertising when not connected
    if (currentLEDPattern == LED_ADVERTISING) {
        // Strobing brightness pattern (sine wave)
        if (currentTime - ledLastUpdate >= 20) { // Update every 20ms for smooth animation
            ledLastUpdate = currentTime;
            // Create a sine wave brightness pattern (0-255)
            ledBrightness = 128 + 127 * sin(currentTime / 500.0);
            analogWrite(LED_PIN, ledBrightness);
        }
    }
}

// Function to trigger IMU reset LED pattern
void startIMUResetPattern() {
    currentLEDPattern = LED_IMU_RESET;
    imuResetStartTime = millis();
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial time to connect
    
    Serial.println("\n\n----- Eidon Glove Starting -----");
    
    // Setup LED pin
    pinMode(LED_PIN, OUTPUT);
    
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
    
    // Initialize NimBLE stack first (device name can be empty for now)
    NimBLEDevice::init("");

    // Generate a unique device name based on MAC address
    std::string deviceName = generateUniqueName();
    NimBLEDevice::setDeviceName(deviceName);

    Serial.print("Advertising as: ");
    Serial.println(deviceName.c_str());
    
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
    inputGamepad = hid->inputReport(1); // Report ID 1 (Input)
    outputGamepad = hid->outputReport(1); // Report ID 1 (Output)
    outputGamepad->setCallbacks(new OutputReportCallbacks());
    
    // Open NVS and load saved color
    prefs.begin("glove", false);
    if (prefs.getBytes("color", deviceColor, 3) != 3) {
        // Default color if nothing stored
        deviceColor[0] = 0xFF;
        deviceColor[1] = 0xFF;
        deviceColor[2] = 0xFF;
    }

    // Feature report for RGB color
    featureColor = hid->featureReport(1); // Report ID 1 (Feature)
    featureColor->setValue(deviceColor, 3);
    featureColor->setCallbacks(new FeatureReportCallbacks());
    
    // Set consistent manufacturer name
    hid->manufacturer()->setValue("ESP32-C3");
    
    // Use consistent VID/PID
    hid->pnp(0x01, VENDOR_ID, PRODUCT_ID, 0x0110);
    hid->hidInfo(0x00, 0x01);
    
    // Set report descriptor
    hid->reportMap((uint8_t*)hid_report_descriptor, sizeof(hid_report_descriptor));
    
    // Print the HID descriptor for debugging
    // printHIDDescriptor();
    
    // Start the HID device
    hid->startServices();
    
    // Configure advertising with consistent settings
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(HID_GAMEPAD);
    pAdvertising->addServiceUUID(hid->hidService()->getUUID());
    pAdvertising->setScanResponse(true);
    pAdvertising->setName(deviceName);
    
    // Start advertising
    pAdvertising->start();
    
    Serial.println("BT Gamepad initialized!");
    Serial.print("Device name: Hand Tracker (");
    Serial.print(IS_LEFT_HAND ? "Left" : "Right");
    Serial.println(")");
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
    // Update LED status first
    updateLEDStatus();
    
    // Check for calibration reset button hold (independent of connection state)
    bool bootButtonPressed = !digitalRead(BUTTON_BOOT_PIN);
    
    if (bootButtonPressed) {
        if (!buttonHoldInProgress) {
            // Button just pressed, start timing
            buttonHoldInProgress = true;
            buttonHoldStartTime = millis();
            calibrationResetTriggered = false;
        } else {
            // Button is being held, check if it's been long enough
            unsigned long holdDuration = millis() - buttonHoldStartTime;
            
            if (holdDuration >= CALIBRATION_RESET_HOLD_TIME && !calibrationResetTriggered) {
                // Button held long enough, reset calibration
                Serial.println("Button held for 3+ seconds - resetting hall effect calibration!");
                clearHallEffectCalibration();
                calibrationResetTriggered = true;
                
                // Flash LED rapidly to indicate reset
                for (int i = 0; i < 6; i++) {
                    digitalWrite(LED_PIN, HIGH);
                    delay(100);
                    digitalWrite(LED_PIN, LOW);
                    delay(100);
                }
            }
        }
    } else {
        if (buttonHoldInProgress) {
            // Button was released
            unsigned long holdDuration = millis() - buttonHoldStartTime;
            if (holdDuration < CALIBRATION_RESET_HOLD_TIME && !calibrationResetTriggered) {
                Serial.print("Button held for ");
                Serial.print(holdDuration);
                Serial.println("ms (need 3000ms for calibration reset)");
            }
            buttonHoldInProgress = false;
        }
    }
    
    // Handle connection state changes
    if (deviceConnected && !oldDeviceConnected) {
        // Just connected
        Serial.println("Connected - starting to send data");
        oldDeviceConnected = deviceConnected;
        
        // Force reset LED state and start fresh pattern
        digitalWrite(LED_PIN, LOW);  // Start with LED OFF
        ledState = false;
        ledLastUpdate = 0; // Force immediate update
        currentLEDPattern = LED_CONNECTED;
        Serial.println("Switching to CONNECTED LED pattern - should flash dimmed");
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        // Just disconnected
        Serial.println("Disconnected - restarting advertising");
        // delay(500); // Give BLE stack time to get ready
        NimBLEDevice::startAdvertising();
        Serial.println("Advertising restarted");
        oldDeviceConnected = deviceConnected;
        currentLEDPattern = LED_ADVERTISING; // Switch to advertising LED pattern
    }
    
    // Update finger tracking data
    calcFingerAngles();

    // Update BNO085 data
    updateBNO085();

    // Send data if connected
    if (deviceConnected) {
        // Read the button state from the Xiao ESP32-C3 (for mode switching, separate from calibration reset)
        int buttonsState = !digitalRead(BUTTON_BOOT_PIN) || !digitalRead(BUTTON_MODE_PIN) << 1;
        
        // Toggle between modes on button release (but only if it wasn't a long hold for calibration reset)
        static int lastButtonState = 0;
        if (!buttonsState && lastButtonState && !calibrationResetTriggered) {  // Button was released and wasn't a calibration reset
            // cycleToNextMode();
            if (isBNO085Available()) {
                resetBNO085();
                startIMUResetPattern(); // Start IMU reset LED pattern
            }
        }
        lastButtonState = buttonsState;
        
        // Update the gamepad report structure
        // Clear all buttons first
        memset(&gamepadReport, 0, sizeof(GamepadReport));

        // Set button1 based on the physical button
        gamepadReport.button1 = buttonsState & 0x01;
        gamepadReport.button2 = buttonsState & 0x02;
        
        // Set button9 to indicate left/right hand
        gamepadReport.cfgbit0 = IS_LEFT_HAND;
        
        // Set buttons to indicate current mode (optional)
        // gamepadReport.button1 = (currentMode == GAME_MODE);
        // gamepadReport.button2 = (currentMode == RAW_ANGLES_MODE);
        
        // Process data based on the current mode
        switch (currentMode) {
            case GAME_MODE:
                // GAME MODE: Use mapped controls for gameplay

                // Update finger button states based on position changes
                updateFingerButtons();
                
                // Only update quaternion if BNO085 is available
                if (isBNO085Available()) {
                    quaternionToEuler();
                    // Map roll angle to X-axis (left/right movement)
                    gamepadReport.axes[0] = constrain(map(ypr.roll, -45, 45, 0, 255), 0, 255);
                } else {
                    // Fallback: center the axis when IMU is not available
                    gamepadReport.axes[0] = 127;  // Center position
                }

                // Set button states based on detected gestures
                gamepadReport.button1 = fingerButtons[0].isPressed; // Thumb
                gamepadReport.button2 = fingerButtons[1].isPressed; // Pinky finger
                gamepadReport.button3 = fingerButtons[2].isPressed; // Ring finger
                gamepadReport.button4 = fingerButtons[3].isPressed; // Middle finger
                gamepadReport.button5 = fingerButtons[4].isPressed; // Index finger

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

                // Store quaternion values directly as 16-bit values (if BNO085 available)
                if (isBNO085Available()) {
                    gamepadReport.quaternion[0] = quaternionToAxis(quaternion_x);
                    gamepadReport.quaternion[1] = quaternionToAxis(quaternion_y);
                    gamepadReport.quaternion[2] = quaternionToAxis(quaternion_z);
                    gamepadReport.quaternion[3] = quaternionToAxis(quaternion_w);
                } else {
                    // Fallback: identity quaternion when IMU is not available
                    gamepadReport.quaternion[0] = quaternionToAxis(0.0f);  // x
                    gamepadReport.quaternion[1] = quaternionToAxis(0.0f);  // y  
                    gamepadReport.quaternion[2] = quaternionToAxis(0.0f);  // z
                    gamepadReport.quaternion[3] = quaternionToAxis(1.0f);  // w (identity)
                }
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
