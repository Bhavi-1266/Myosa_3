/*
  Enhanced Health Monitoring System with OLED Display
  Features: Step Counting, Seizure Detection, Fall Detection
  Uses MPU6050 accelerometer/gyroscope data with OLED feedback
  Based on MYOSA platform working sensor code
  Optimized with scientific thresholds but easier triggering for testing
*/

#include <Wire.h>
#include <oled.h>
#include <math.h>


#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>


// ------------- Firebase stuff -----------------


/* 1. Define the WiFi credentials */
#define WIFI_SSID "BHAVY2"
#define WIFI_PASSWORD "Bms@12666"

// For the following credentials, see examples/Authentications/SignInAsUser/EmailPassword/EmailPassword.ino

/* 2. Define the API Key */
#define API_KEY "AIzaSyByS6w7S7xJol6hBMkEH6QK0DNHsa6c5CU"

/* 3. Define the RTDB URL */
#define DATABASE_URL "https://myosa-3-default-rtdb.firebaseio.com" //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app

/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "Bhavy_mr@cs.iitr.ac.in"
#define USER_PASSWORD "Bhavy@2006"

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;

unsigned long count = 0;


//-----------------------------------------------


// MPU6050 I2C address (MYOSA platform uses 0x69)
#define MPU6050_ADDRESS 0x69

// MPU6050 register addresses
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H  0x43
#define MPU6050_WHO_AM_I     0x75

// Create OLED display object
oLed display(SCREEN_WIDTH, SCREEN_HEIGHT);

// Constants for health monitoring (Made easier to trigger for testing)
#define BUZZER_PIN 12

// STEP COUNTING PARAMETERS (Based on scientific research)
#define STEP_THRESHOLD 1.15        // Reduced from 1.25g for easier testing
#define STEP_MIN_PERIOD 250        // 250ms minimum between steps (instead of 300ms)
#define STEP_MAX_PERIOD 2000       // 2 seconds maximum between steps
#define STEP_VARIANCE_THRESHOLD 0.008  // Reduced variance threshold



// SEIZURE DETECTION PARAMETERS (Easier triggering)
#define SEIZURE_THRESHOLD 2.7   // Reduced from 2.0g for easier testing
#define SEIZURE_DURATION 700       // Reduced from 400ms to 300ms
#define SEIZURE_BURST_COUNT 15      // Number of consecutive bursts needed
#define SEIZURE_RESET_TIME 1000    // Reset if no activity for 1 second

// FALL DETECTION PARAMETERS (Easier triggering)
#define FALL_FREE_FALL_THRESHOLD 0.563   // Increased from 0.563g (easier to trigger)
#define FALL_IMPACT_THRESHOLD 1.5     // Reduced from 2.5g
#define FALL_ORIENTATION_THRESHOLD 35   // Reduced from 30 degrees
#define FALL_STILLNESS_TIME 200       // Reduced from 1000ms
#define FALL_STILLNESS_THRESHOLD 1.1   // Increased from 0.5g
#define FALL_STILLNESS_THRESHOLD_min 0.9 // min acc to start stillness

// Kalman Filter State Variables
float filteredAx = 0, filteredAy = 0, filteredAz = 0;
float filteredGx = 0, filteredGy = 0, filteredGz = 0;
float kalmanQ = 0.01;
float kalmanR = 0.5;
float P_ax = 1.0, P_ay = 1.0, P_az = 1.0;
float P_gx = 1.0, P_gy = 1.0, P_gz = 1.0;

////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////


// Seizure detection variables
unsigned long seizure_start_time = 0;
bool seizure_detected = false;
int seizure_burst_count = 0;
unsigned long last_seizure_activity = 0;

// Fall detection variables
bool fall_free_fall_detected = false;
bool fall_impact_detected = false;
int fall_stillness_comfirmed =0;
bool fall_orientation_changed = false;
unsigned long fall_detection_start = 0;
unsigned long fall_stillness_start = 0;
bool fall_detected = false;
float initial_orientation[3] = {0, 0, 0};
// These go after your existing global step counting variables


//step variable over
int stepCount =0;
bool step_L_peak_achieved=false;
bool step_H_peak_achieved=false;
float threshold_step = 0.850;
bool step_detect = false;
Firebase.setBool(fbdo, "/StepDetect", step_detect);


// System variables
enum SystemMode { MODE_MONITORING, MODE_STEP_COUNT, MODE_SEIZURE_ALERT, MODE_FALL_ALERT };
SystemMode currentMode = MODE_MONITORING;
unsigned long lastDisplayUpdate = 0;
unsigned long alertStartTime = 0;

// Function prototypes
bool testMPU6050Connection();
void writeMPU6050(uint8_t reg, uint8_t data);
int16_t readMPU6050(uint8_t reg);
float simpleKalmanFilter(float measurement, float estimate, float& P);
void calibrateInitialOrientation();
float calculateOrientationChange();
void processStepCounting();
void processSeizureDetection();
void processFallDetection();
void triggerSeizureAlert();
void triggerFallAlert();
void resetSeizureDetection();
void resetFallDetection();
void updateDisplay();
void showStartupScreen();
void showErrorScreen(const char* error);
void buzzAlert(int numBuzz, int duration);
void logHealthData();
void readSensorData();

////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
    Serial.begin(115200);
    Serial.println("Enhanced Health Monitoring System - MYOSA Platform");
    Serial.println("Features: Step Count | Seizure Detection | Fall Detection");
    Serial.println("=====================================================");
    
    // Initialize I2C communication
    Wire.begin();
    Wire.setClock(400000);
    
    // Initialize OLED display
    if(!display.begin()) {
        Serial.println("SSD1306 allocation failed");
    } else {
        Serial.println("OLED Display initialized");
        showStartupScreen();
    }
    
    // Initialize MPU6050
    if (testMPU6050Connection()) {
        Serial.println("✓ MPU6050 found at address 0x69!");
        
        // Configure MPU6050 for optimal performance
        writeMPU6050(MPU6050_PWR_MGMT_1, 0x00);  // Wake up
        writeMPU6050(0x1C, 0x00);  // Accelerometer ±2g range
        writeMPU6050(0x1B, 0x00);  // Gyroscope ±250°/s range
        writeMPU6050(0x1A, 0x03);  // Low pass filter ~44Hz
        delay(100);
        
        Serial.println("✓ MPU6050 initialized successfully!");
        calibrateInitialOrientation();
        
    } else {
        Serial.println("✗ MPU6050 not found!");
        showErrorScreen("MPU6050 ERROR!");
        while(1);
    }

    //wifi 

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();


    //firebase signup

    /* Assign the api key (required) */
    config.api_key = API_KEY;

    /* Assign the user sign in credentials */
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    /* Assign the RTDB URL (required) */
    config.database_url = DATABASE_URL;

    config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

    Firebase.reconnectNetwork(true);
    fbdo.setBSSLBufferSize(4096 , 1024 );

    Firebase.begin(&config, &auth);

    Firebase.setDoubleDigits(5);

    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("Starting comprehensive health monitoring...");
}

void loop() {

    if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)){

        //firebase stuff
        sendDataPrevMillis = millis();
        step_detect = Firebase.getBool(fbdo, "/StepDetect");
        if(!step_detect){
            Firebase.set(fbdo, "/stepCount", stepCount);
        }
    }
        // Read sensor data
        readSensorData();
        
        // Process health monitoring functions
        if(step_detect){
            processStepCounting();
        }else{
            stepCount = 0; // Reset step count if not in step counting mode
            step_L_peak_achieved = false;
        }
        processStepCounting();
        processSeizureDetection();
        processFallDetection();
        
        // Update display based on current mode
        if (millis() - lastDisplayUpdate > 20) {  // Update every 200ms
            updateDisplay();
            lastDisplayUpdate = millis();
        }
        
        // Log data for debugging
        logHealthData();
        
        delay(20);  // 20Hz sampling rate
    
}

void readSensorData() {
    // Read accelerometer data
    int16_t accelX_raw = readMPU6050(MPU6050_ACCEL_XOUT_H);
    int16_t accelY_raw = readMPU6050(MPU6050_ACCEL_XOUT_H + 2);
    int16_t accelZ_raw = readMPU6050(MPU6050_ACCEL_XOUT_H + 4);
    
    // Read gyroscope data
    int16_t gyroX_raw = readMPU6050(MPU6050_GYRO_XOUT_H);
    int16_t gyroY_raw = readMPU6050(MPU6050_GYRO_XOUT_H + 2);
    int16_t gyroZ_raw = readMPU6050(MPU6050_GYRO_XOUT_H + 4);
    
    // Convert to g units and degrees/second
    float rawAx = accelX_raw / 16384.0;
    float rawAy = accelY_raw / 16384.0;
    float rawAz = accelZ_raw / 16384.0;
    float rawGx = gyroX_raw / 131.0;
    float rawGy = gyroY_raw / 131.0;
    float rawGz = gyroZ_raw / 131.0;
    
    // // Apply Kalman filtering
    // filteredAx = simpleKalmanFilter(rawAx, filteredAx, P_ax);
    // filteredAy = simpleKalmanFilter(rawAy, filteredAy, P_ay);
    // filteredAz = simpleKalmanFilter(rawAz, filteredAz, P_az);
    // filteredGx = simpleKalmanFilter(rawGx, filteredGx, P_gx);
    // filteredGy = simpleKalmanFilter(rawGy, filteredGy, P_gy);
    // filteredGz = simpleKalmanFilter(rawGz, filteredGz, P_gz);


    filteredAx = rawAx ;
    filteredAy = rawAy ;
    filteredAz = rawAz ;
    filteredGx = rawGx ;
    filteredGy = rawGy ;
    filteredGz = rawGz ;
    
}

void processStepCounting(){
     float accelMagnitude = sqrt(filteredAx * filteredAx + 
                               filteredAy * filteredAy + 
                               filteredAz * filteredAz);
    // Serial.println("accelMagnitude:");
    // Serial.println(accelMagnitude);
    // Serial.println("step:");
    // Serial.println(stepCount);
    // if(accelMagnitude>1+threshold_step){
    //     step_H_peak_achieved=true;
    // }
    if(accelMagnitude<threshold_step){
        step_L_peak_achieved=true;
    }

    if(accelMagnitude>0.95 && accelMagnitude<1.05 ){
        if(step_L_peak_achieved){
            stepCount+=2;
            step_H_peak_achieved=false;
            step_L_peak_achieved=false;
        }
    }
}


void processSeizureDetection() {
    float accelMagnitude = sqrt(filteredAx * filteredAx + 
                               filteredAy * filteredAy + 
                               filteredAz * filteredAz);
    
    unsigned long currentTime = millis();
    
    // Check for seizure-like acceleration bursts
    if (accelMagnitude > SEIZURE_THRESHOLD) {
        last_seizure_activity = currentTime;
        
        if (!seizure_detected) {
            seizure_start_time = currentTime;
            seizure_detected = true;
            seizure_burst_count = 1;
        } else {
            seizure_burst_count++;
            
            // Check if we have enough consecutive bursts over the duration
            if ((currentTime - seizure_start_time) >= SEIZURE_DURATION && 
                seizure_burst_count >= SEIZURE_BURST_COUNT) {
                
                triggerSeizureAlert();
                seizure_burst_count = 1;
                return;
            }
        }
    } else {
        // Reset if no activity for reset time
        if (seizure_detected && (currentTime - last_seizure_activity) > SEIZURE_RESET_TIME) {
            resetSeizureDetection();
            seizure_burst_count = 1;

        }
    }
}

void processFallDetection() {
    float accelMagnitude = sqrt(filteredAx * filteredAx + 
                               filteredAy * filteredAy + 
                               filteredAz * filteredAz);
    
    float gyroMagnitude = sqrt(filteredGx * filteredGx + 
                              filteredGy * filteredGy + 
                              filteredGz * filteredGz);
    
    unsigned long currentTime = millis();
    
    float reset_time_thresholds =200;
    // Phase 1: Free fall detection
    if (!fall_free_fall_detected && accelMagnitude < FALL_FREE_FALL_THRESHOLD) {
        fall_free_fall_detected = true;
        fall_detection_start = currentTime;
        Serial.println("Free fall phase detected!");
    }
    
    // Phase 2: Impact detection (after free fall)
    if (fall_free_fall_detected && !fall_impact_detected && 
        accelMagnitude > FALL_IMPACT_THRESHOLD) {
        unsigned long currentTime_2 = millis();
        if(currentTime_2-fall_detection_start>=reset_time_thresholds){
            fall_impact_detected = true;
            fall_stillness_start = currentTime;
            Serial.println("Impact phase detected!");
        }

    }
    
    // Phase 3: Orientation change detection
    // if (fall_impact_detected && !fall_orientation_changed) {
    //     float orientationChange = calculateOrientationChange();
        
    //     if (orientationChange > FALL_ORIENTATION_THRESHOLD) {
    //         fall_orientation_changed = true;
    //         fall_stillness_start = currentTime;
    //         Serial.println("Orientation change detected!");
    //     }
    // }
    
    // Phase 4: Stillness confirmation
    if (fall_impact_detected) {
        if (accelMagnitude < FALL_STILLNESS_THRESHOLD && (currentTime - fall_stillness_start) > FALL_STILLNESS_TIME) {
            if(accelMagnitude>FALL_STILLNESS_THRESHOLD_min){
                fall_stillness_comfirmed++;
            }
        }
    }

    // if (!fall_detected) {
    //     if (accelMagnitude < FALL_STILLNESS_THRESHOLD && 
    //         (currentTime - fall_stillness_start) > FALL_STILLNESS_TIME) {
            
    //         triggerFallAlert();
    //         return;
    //     }
    // }
    
    if (fall_free_fall_detected && fall_impact_detected && fall_stillness_comfirmed>10) { 
        triggerFallAlert();

    }

    // Reset fall detection if too much time has passed
    if (fall_free_fall_detected && (currentTime - fall_detection_start) < reset_time_thresholds) {
        if(accelMagnitude > FALL_IMPACT_THRESHOLD){
            resetFallDetection();
        }  
    }else if (currentTime - fall_detection_start>1300){
        resetFallDetection();
    }

}

float calculateOrientationChange() {
    float currentOrientation[3] = {
        atan2(filteredAy, filteredAz) * 180.0 / PI,
        atan2(-filteredAx, sqrt(filteredAy * filteredAy + filteredAz * filteredAz)) * 180.0 / PI,
        atan2(filteredAy, filteredAx) * 180.0 / PI
    };
    
    float maxChange = 0;
    for (int i = 0; i < 3; i++) {
        float change = abs(currentOrientation[i] - initial_orientation[i]);
        if (change > maxChange) {
            maxChange = change;
        }
    }
    
    return maxChange;
}

void calibrateInitialOrientation() {
    delay(1000);  // Wait for sensor to stabilize
    
    // Read current sensor data for calibration
    readSensorData();
    
    initial_orientation[0] = atan2(filteredAy, filteredAz) * 180.0 / PI;
    initial_orientation[1] = atan2(-filteredAx, sqrt(filteredAy * filteredAy + filteredAz * filteredAz)) * 180.0 / PI;
    initial_orientation[2] = atan2(filteredAy, filteredAx) * 180.0 / PI;
    
    Serial.println("Initial orientation calibrated");
}

void triggerSeizureAlert() {
    currentMode = MODE_SEIZURE_ALERT;
    alertStartTime = millis();
    
    Serial.println("*** SEIZURE DETECTED! ALERT! ***");
    buzzAlert(5, 200);  // 5 buzzes, 200ms each
    
    resetSeizureDetection();
}

void triggerFallAlert() {
    currentMode = MODE_FALL_ALERT;
    alertStartTime = millis();
    fall_detected = true;
    
    Serial.println("*** FALL DETECTED! ALERT! ***");
    buzzAlert(3, 100);  // 10 buzzes, 100ms each

    resetFallDetection();
}

void resetSeizureDetection() {
    seizure_start_time = 0;
    seizure_detected = false;
    seizure_burst_count = 0;
    last_seizure_activity = 0;
}

void resetFallDetection() {
    fall_free_fall_detected = false;
    fall_impact_detected = false;
    fall_orientation_changed = false;
    fall_detection_start = 0;
    fall_stillness_start = 0;
    fall_detected = false;
    fall_stillness_comfirmed=0;
}

void updateDisplay() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    switch (currentMode) {
        case MODE_MONITORING: {
            display.print("Health Monitor");
            display.setCursor(0, 12);
            display.print("Steps: ");
            display.print(stepCount);
            
            display.setCursor(0, 24);
            float currentAccel = sqrt(filteredAx * filteredAx + filteredAy * filteredAy + filteredAz * filteredAz);
            display.print("Accel: ");
            display.print(currentAccel, 1);
            display.print("g");
            
            display.setCursor(0, 36);
            display.print("Status: NORMAL");
            
            display.setCursor(0, 48);
            display.print("Monitoring...");
            break;
        }
            
        case MODE_SEIZURE_ALERT: {
            display.setTextSize(2);
            display.print("SEIZURE");
            display.setCursor(0, 20);
            display.print("ALERT!");
            display.setTextSize(1);
            display.setCursor(0, 40);
            display.print("Contact: Emergency");
            display.setCursor(0, 52);
            display.print("Steps today: ");
            display.print(stepCount);
            
            // Return to monitoring after 10 seconds
            if (millis() - alertStartTime > 10000) {
                currentMode = MODE_MONITORING;
            }
            break;
        }
            
        case MODE_FALL_ALERT: {
            display.setTextSize(2);
            display.print("FALL");
            display.setCursor(0, 20);
            display.print("DETECTED");
            display.setTextSize(1);
            display.setCursor(0, 40);
            display.print("Emergency Alert!");
            display.setCursor(0, 52);
            display.print("Steps: ");
            display.print(stepCount);
            
            // Return to monitoring after 15 seconds
            if (millis() - alertStartTime > 15000) {
                currentMode = MODE_MONITORING;
            }
            break;
        }
    }
    
    display.display();
}

void showStartupScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Health Monitor v2.0");
    display.setCursor(0, 12);
    display.print("Step Counter");
    display.setCursor(0, 24);
    display.print("Seizure Detection");
    display.setCursor(0, 36);
    display.print("Fall Detection");
    display.setCursor(0, 52);
    display.print("Initializing...");
    display.display();
    delay(3000);
}

void showErrorScreen(const char* error) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print(error);
    display.display();
}

void buzzAlert(int numBuzz, int duration) {
    for (int i = 0; i < numBuzz; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(duration);
        digitalWrite(BUZZER_PIN, LOW);
        delay(duration);
    }
}

void logHealthData() {
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 1000) {  // Log every second
        float accelMag = sqrt(filteredAx * filteredAx + filteredAy * filteredAy + filteredAz * filteredAz);
        Serial.print("Steps: ");
        Serial.print(stepCount);
        Serial.print(" | Accel: ");
        Serial.print(accelMag, 2);
        Serial.print("g | Mode: ");
        
        switch (currentMode) {
            case MODE_MONITORING: Serial.print("Monitor"); break;
            case MODE_SEIZURE_ALERT: Serial.print("Seizure"); break;
            case MODE_FALL_ALERT: Serial.print("Fall"); break;
        }
        Serial.println();
        
        lastLog = millis();
    }
}

// Function to test MPU6050 connection
bool testMPU6050Connection() {
    Wire.beginTransmission(MPU6050_ADDRESS);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

// Function to write to MPU6050 register
void writeMPU6050(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    Wire.write(data);
    Wire.endTransmission();
}

// Function to read 16-bit data from MPU6050
int16_t readMPU6050(uint8_t reg) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    
    Wire.requestFrom(MPU6050_ADDRESS, 2);
    
    int16_t value = 0;
    if (Wire.available() >= 2) {
        value = Wire.read() << 8;  // High byte
        value |= Wire.read();      // Low byte
    }
    
    return value;
}

// Simple Kalman filter implementation for scalar data smoothing
float simpleKalmanFilter(float measurement, float estimate, float& P) {
    float K = P / (P + kalmanR); // Kalman gain
    estimate = estimate + K * (measurement - estimate);
    P = (1 - K) * P + kalmanQ; // Update the error covariance
    return estimate;
}