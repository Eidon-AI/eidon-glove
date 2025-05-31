#include "HallEffectSensors.h"

ResponsiveAnalogRead analog(A2, true);
Preferences calibPrefs;  // Preferences instance for calibration storage

int32_t rawVals[SENSOR_COUNT];
float proto_angles[SENSOR_COUNT];
float min_angles[SENSOR_COUNT];
float max_angles[SENSOR_COUNT];

// Track when calibration was last saved to avoid excessive writes
unsigned long lastCalibSave = 0;
const unsigned long CALIB_SAVE_INTERVAL = 5000; // Save every 5 seconds max

float polyVals[16][3] = {
    {-0.000087481431887,0.549306011516565,-709.440158534950912},  //thumb 0
    {0.000043188683603,-0.288631646308703,+518.26001946236131},   //thumb 1
    {0.000081944493116,-0.48715545187493,753.215310445897971},    //thumb 2
    {0.000029299702805,-0.235958663536102,473.892439373476074},   //thumb 3
    {-0.000005288207298,0.121258593336859,-147.245901639344262},  //pointer 4
    {-0.000135942468348,0.817456387325188,-1033.093236601650843}, //pointer 5
    {0.000101643291297,-0.646716069346575,1031.971761445997989},  //pointer 6
    {-0.000041474654378,0.275529953917051,-295.161290322580645},  //middle 7
    {-0.000155663598998,0.846081469596033,-994.321241823930591},  //middle 8
    {0.000170233984067,-1.128460118194487,1768.951835332448657},  //middle 9
    {-0.000041474654378,0.275529953917051,-295.161290322580645},  //ring 10
    {-0.000155663598998,0.846081469596033,-994.321241823930591},  //ring 11
    {0.000170233984067,-1.128460118194487,1768.951835332448657},  //ring 12
    {-0.000050156739812,0.308087774294671,-325.54858934169279},   //pinkie 13
    {-0.000204869267408,1.180238586110067,-1522.071698458919325}, //pinkie 14
    {0.00009027900176,-0.57849114376526,925.953643298021097},     //pinkie 15
};

void hallEffectSensorsSetup(){
    analogReadResolution(12);

    //pinMode(D2, OUTPUT);

    pinMode(S0, OUTPUT);
    pinMode(S1, OUTPUT);
    pinMode(S2, OUTPUT);
    pinMode(S3, OUTPUT);

    //digitalWrite(D2, LOW);

    // Try to load saved calibration data
    if (loadHallEffectCalibration()) {
        Serial.println("Loaded saved hall effect calibration");
    } else {
        Serial.println("No saved calibration found, using defaults");
        // Initialize with default values if no saved calibration
        for (uint8_t i = 0; i < SENSOR_COUNT; i++){
            min_angles[i] = 10000;
            max_angles[i] = -10000;
        }
    }
}

float poly(double x, double a,double b,double c){
    return a*pow(x,2)+b*x+c;
}

void calibrateHallEffectSensors(){
    bool calibrationChanged = false;
    
    for (uint8_t i = 0; i < SENSOR_COUNT; i++){
        if(proto_angles[i] < min_angles[i]){
            min_angles[i] = proto_angles[i];
            calibrationChanged = true;
        } else if(proto_angles[i] > max_angles[i]){
            max_angles[i] = proto_angles[i];
            calibrationChanged = true;
        }
    }
    
    // Save calibration periodically if it has changed
    if (calibrationChanged && (millis() - lastCalibSave > CALIB_SAVE_INTERVAL)) {
        saveHallEffectCalibration();
        lastCalibSave = millis();
    }
}

void measureHallEffectSensors()
{
    for (uint8_t i = 0; i < SENSOR_COUNT; i++){
        digitalWrite(S0, i & 0b1);
        digitalWrite(S1, (i>>1) & 0b1);
        digitalWrite(S2, (i>>2) & 0b1);
        digitalWrite(S3, (i>>3) & 0b1);

        delay(1); //not sure if this is necessary

        analog.update();
        int32_t rawVal = analog.getRawValue();
        rawVals[i] = rawVal;
     }

    for (uint8_t i = 0; i < SENSOR_COUNT; i++){
        float angle = poly(rawVals[i],polyVals[i][0],polyVals[i][1],polyVals[i][2]);
        proto_angles[i] = angle;
    }
    //jank solution to having the angles for the thumb backwards
    //TODO remove with glove v2
    // proto_angles[12] = 150-proto_angles[12];
    // proto_angles[13] = 150-proto_angles[12];
    // proto_angles[14] = 150-proto_angles[14];
    // proto_angles[15] = 150-proto_angles[15];
}

void resetHallEffectCalibration(){
    for (uint8_t i = 0; i < SENSOR_COUNT; i++){
        min_angles[i] = 10000;
        max_angles[i] = -10000;
    }
}

bool loadHallEffectCalibration(){
    calibPrefs.begin("hall_calib", true); // Read-only mode
    
    // Check if calibration data exists
    if (!calibPrefs.isKey("min_angles") || !calibPrefs.isKey("max_angles")) {
        calibPrefs.end();
        return false;
    }
    
    // Load min_angles array
    size_t minSize = calibPrefs.getBytesLength("min_angles");
    if (minSize != sizeof(min_angles)) {
        Serial.println("Calibration data size mismatch for min_angles");
        calibPrefs.end();
        return false;
    }
    calibPrefs.getBytes("min_angles", min_angles, sizeof(min_angles));
    
    // Load max_angles array
    size_t maxSize = calibPrefs.getBytesLength("max_angles");
    if (maxSize != sizeof(max_angles)) {
        Serial.println("Calibration data size mismatch for max_angles");
        calibPrefs.end();
        return false;
    }
    calibPrefs.getBytes("max_angles", max_angles, sizeof(max_angles));
    
    calibPrefs.end();
    
    Serial.println("Hall effect calibration loaded successfully");
    return true;
}

void saveHallEffectCalibration(){
    calibPrefs.begin("hall_calib", false); // Read-write mode
    
    // Save both arrays
    calibPrefs.putBytes("min_angles", min_angles, sizeof(min_angles));
    calibPrefs.putBytes("max_angles", max_angles, sizeof(max_angles));
    
    calibPrefs.end();
    
    Serial.println("Hall effect calibration saved");
}

void clearHallEffectCalibration(){
    calibPrefs.begin("hall_calib", false); // Read-write mode
    
    // Clear stored calibration
    calibPrefs.clear();
    calibPrefs.end();
    
    // Reset to default values
    resetHallEffectCalibration();
    
    Serial.println("Hall effect calibration cleared and reset to defaults");
}

