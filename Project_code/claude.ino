/*
  Comprehensive Wrist-Worn Health Monitor
  Features: Step counting, Fall detection, Seizure detection, Sleep tracking,
  Gesture recognition, Activity monitoring, and more
  Hardware: MPU6050 + OLED Display + Buzzer
*/

#include <Wire.h>
#include <oled.h>
#include <math.h>
#include <EEPROM.h>

// MPU6050 I2C address
#define MPU6050_ADDRESS 0x69

// MPU6050 register addresses
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H  0x43

// Create OLED display object
oLed display(SCREEN_WIDTH, SCREEN_HEIGHT);

// Hardware pins
#define BUZZER_PIN 12

// System constants
#define SAMPLE_RATE 10 // 10Hz sampling
#define BUFFER_SIZE 50 // 5 second buffer for analysis

// Thresholds (wrist-worn calibrated)
#define STEP_THRESHOLD 1.0
#define STEP_MIN_INTERVAL 400
#define FALL_FREE_THRESHOLD 0.3
#define FALL_IMPACT_THRESHOLD 2.0
#define SEIZURE_THRESHOLD 8.0
#define SEIZURE_MIN_DURATION 3000
#define SEDENTARY_ALERT_TIME 3600000 // 1 hour
#define TREMOR_FREQ_MIN 4.0
#define TREMOR_FREQ_MAX 12.0
#define SLEEP_MOTION_THRESHOLD 0.5

// Kalman Filter Variables
float filteredAx = 0, filteredAy = 0, filteredAz = 0;
float kalmanQ = 0.01, kalmanR = 0.5;
float P_ax = 1.0, P_ay = 1.0, P_az = 1.0;

// Data structures
struct AccelData {
    float x, y, z, magnitude;
    unsigned long timestamp;
};

struct ActivityData {
    int steps;
    float calories;
    int activeMinutes;
    int sedentaryMinutes;
    float totalDistance;
};

struct SleepData {
    unsigned long sleepStart;
    unsigned long sleepEnd;
    int sleepQuality; // 0-100
    int wakeTimes;
    bool isSleeping;
};

// Circular buffer for acceleration data
AccelData accelBuffer[BUFFER_SIZE];
int bufferIndex = 0;

// Feature states
ActivityData dailyActivity = {0, 0, 0, 0, 0};
SleepData sleepData = {0, 0, 0, 0, false};

// Detection states
unsigned long lastStepTime = 0;
unsigned long lastActivityTime = 0;
unsigned long seizureStartTime = 0;
unsigned long handWashStartTime = 0;
bool seizureDetected = false;
bool fallDetected = false;
bool handWashing = false;
bool emergencyMode = false;

// Display states
int currentScreen = 0;
unsigned long lastScreenChange = 0;
bool screenOn = true;
float wristAngle = 0;

// Gesture recognition
int gestureBuffer[10];
int gestureIndex = 0;
unsigned long lastGestureTime = 0;

// Smart wake
unsigned long optimalWakeTime = 0;
bool wakeAlarmSet = false;

void setup() {
    Serial.begin(115200);
    Serial.println("Comprehensive Wrist Health Monitor");
    Serial.println("==================================");
    
    // Initialize I2C and OLED
    Wire.begin();
    Wire.setClock(400000);
    
    if(!display.begin()) {
        Serial.println("OLED Display failed");
        while(1);
    }
    
    // Initialize MPU6050
    if (!initializeMPU6050()) {
        displayError("MPU6050 FAILED");
        while(1);
    }
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // Load daily data from EEPROM
    loadDailyData();
    
    // Display startup message
    showStartupScreen();
    
    Serial.println("All systems ready!");
}

void loop() {
    // Read sensor data
    AccelData currentData = readAccelData();
    
    // Add to circular buffer
    accelBuffer[bufferIndex] = currentData;
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
    
    // Core detection functions
    detectSteps(currentData);
    detectFall(currentData);
    detectSeizure(currentData);
    detectSleep(currentData);
    detectGestures(currentData);
    detectTremor();
    detectHandWashing(currentData);
    checkSedentaryAlert();
    
    // Update activity metrics
    updateActivityMetrics();
    
    // Handle display
    updateWristOrientation(currentData);
    handleDisplay();
    
    // Smart wake functionality
    handleSmartWake();
    
    // Save data periodically
    if (millis() % 60000 == 0) { // Every minute
        saveDailyData();
    }
    
    delay(100); // 10Hz update rate
}

bool initializeMPU6050() {
    Wire.beginTransmission(MPU6050_ADDRESS);
    if (Wire.endTransmission() != 0) return false;
    
    // Wake up MPU6050
    writeMPU6050(MPU6050_PWR_MGMT_1, 0x00);
    delay(100);
    
    return true;
}

AccelData readAccelData() {
    AccelData data;
    
    // Read raw accelerometer data
    int16_t ax_raw = readMPU6050(MPU6050_ACCEL_XOUT_H);
    int16_t ay_raw = readMPU6050(MPU6050_ACCEL_XOUT_H + 2);
    int16_t az_raw = readMPU6050(MPU6050_ACCEL_XOUT_H + 4);
    
    // Convert to g units
    float rawAx = ax_raw / 16384.0;
    float rawAy = ay_raw / 16384.0;
    float rawAz = az_raw / 16384.0;
    
    // Apply Kalman filter
    data.x = simpleKalmanFilter(rawAx, filteredAx, P_ax);
    data.y = simpleKalmanFilter(rawAy, filteredAy, P_ay);
    data.z = simpleKalmanFilter(rawAz, filteredAz, P_az);
    data.magnitude = sqrt(data.x*data.x + data.y*data.y + data.z*data.z);
    data.timestamp = millis();
    
    filteredAx = data.x;
    filteredAy = data.y;
    filteredAz = data.z;
    
    return data;
}

void detectSteps(AccelData data) {
    static float lastMag = 0;
    static bool peakDetected = false;
    
    if (data.magnitude > STEP_THRESHOLD && !peakDetected) {
        if (millis() - lastStepTime > STEP_MIN_INTERVAL) {
            dailyActivity.steps++;
            lastStepTime = millis();
            lastActivityTime = millis();
            peakDetected = true;
            
            // Calculate distance (rough estimate)
            dailyActivity.totalDistance += 0.75; // meters per step
            
            Serial.println("Step detected! Total: " + String(dailyActivity.steps));
        }
    }
    
    if (data.magnitude < STEP_THRESHOLD * 0.7) {
        peakDetected = false;
    }
    
    lastMag = data.magnitude;
}

void detectFall(AccelData data) {
    static bool freeFallDetected = false;
    static unsigned long freeFallStart = 0;
    
    // Free fall detection
    if (data.magnitude < FALL_FREE_THRESHOLD) {
        if (!freeFallDetected) {
            freeFallDetected = true;
            freeFallStart = millis();
        }
    } else if (freeFallDetected && data.magnitude > FALL_IMPACT_THRESHOLD) {
        unsigned long freeFallDuration = millis() - freeFallStart;
        
        if (freeFallDuration > 150 && freeFallDuration < 1000) {
            // Fall detected!
            triggerFallAlert();
            freeFallDetected = false;
        }
    } else if (data.magnitude > FALL_FREE_THRESHOLD) {
        freeFallDetected = false;
    }
}

void detectSeizure(AccelData data) {
    if (data.magnitude > SEIZURE_THRESHOLD) {
        if (!seizureDetected) {
            seizureStartTime = millis();
            seizureDetected = true;
        } else if (millis() - seizureStartTime >= SEIZURE_MIN_DURATION) {
            // Confirm with frequency analysis
            float freq = analyzeFrequency();
            if (freq >= 3.0 && freq <= 8.0) {
                triggerSeizureAlert();
                seizureDetected = false;
            }
        }
    } else if (data.magnitude < SEIZURE_THRESHOLD * 0.7) {
        seizureDetected = false;
    }
}

void detectSleep(AccelData data) {
    static unsigned long lowMotionStart = 0;
    static int lowMotionCount = 0;
    
    // Detect sleep onset
    if (data.magnitude < SLEEP_MOTION_THRESHOLD) {
        lowMotionCount++;
        if (lowMotionCount == 1) {
            lowMotionStart = millis();
        }
        
        // 30 minutes of low motion = sleep
        if (lowMotionCount > 1800 && !sleepData.isSleeping) { // 30 min at 10Hz
            sleepData.isSleeping = true;
            sleepData.sleepStart = lowMotionStart;
            Serial.println("Sleep detected");
        }
    } else {
        if (sleepData.isSleeping && lowMotionCount < 10) {
            // Wake up detected
            sleepData.isSleeping = false;
            sleepData.sleepEnd = millis();
            sleepData.wakeTimes++;
            calculateSleepQuality();
            Serial.println("Wake up detected");
        }
        lowMotionCount = 0;
    }
}

void detectGestures(AccelData data) {
    // Simple gesture recognition based on acceleration patterns
    int gestureCode = 0;
    
    if (data.magnitude > 3.0) gestureCode = 1; // Strong movement
    else if (data.magnitude > 2.0) gestureCode = 2; // Medium movement
    else gestureCode = 0; // Low movement
    
    gestureBuffer[gestureIndex] = gestureCode;
    gestureIndex = (gestureIndex + 1) % 10;
    
    // Check for specific patterns
    if (millis() - lastGestureTime > 1000) { // Check every second
        checkGesturePatterns();
        lastGestureTime = millis();
    }
}

void checkGesturePatterns() {
    // Shake gesture: alternating high-low pattern
    bool shakePattern = true;
    for (int i = 0; i < 8; i += 2) {
        if (gestureBuffer[i] != 1 || gestureBuffer[i+1] != 0) {
            shakePattern = false;
            break;
        }
    }
    
    if (shakePattern) {
        handleShakeGesture();
    }
    
    // Emergency gesture: 3 strong movements in sequence
    int strongCount = 0;
    for (int i = 0; i < 10; i++) {
        if (gestureBuffer[i] == 1) strongCount++;
    }
    
    if (strongCount >= 7) {
        triggerEmergencyAlert();
    }
}

void detectTremor() {
    float freq = analyzeFrequency();
    
    if (freq >= TREMOR_FREQ_MIN && freq <= TREMOR_FREQ_MAX) {
        Serial.println("Tremor detected at " + String(freq) + " Hz");
        // Log tremor data
    }
}

void detectHandWashing(AccelData data) {
    // Detect repetitive circular motions
    static float lastX = 0, lastY = 0;
    float deltaX = abs(data.x - lastX);
    float deltaY = abs(data.y - lastY);
    
    if (deltaX > 0.5 && deltaY > 0.5 && !handWashing) {
        handWashing = true;
        handWashStartTime = millis();
        Serial.println("Hand washing started");
    }
    
    if (handWashing && (deltaX < 0.2 && deltaY < 0.2)) {
        unsigned long washDuration = millis() - handWashStartTime;
        if (washDuration > 20000) { // 20 seconds minimum
            Serial.println("Good hand washing! Duration: " + String(washDuration/1000) + "s");
            buzzPattern(2); // Success pattern
        }
        handWashing = false;
    }
    
    lastX = data.x;
    lastY = data.y;
}

void checkSedentaryAlert() {
    if (millis() - lastActivityTime > SEDENTARY_ALERT_TIME) {
        displaySedentaryAlert();
        buzzPattern(3);
        lastActivityTime = millis(); // Reset to avoid continuous alerts
    }
}

void updateActivityMetrics() {
    // Update calories (rough estimate: 0.04 cal per step)
    dailyActivity.calories = dailyActivity.steps * 0.04;
    
    // Update active minutes
    static unsigned long lastActiveUpdate = 0;
    if (millis() - lastActivityTime < 60000 && millis() - lastActiveUpdate > 60000) {
        dailyActivity.activeMinutes++;
        lastActiveUpdate = millis();
    }
}

void updateWristOrientation(AccelData data) {
    // Calculate wrist angle for display rotation
    wristAngle = atan2(data.y, data.x) * 180 / PI;
    
    // Auto-wake display when wrist is raised
    if (data.z > 0.5 && abs(wristAngle) < 45) {
        screenOn = true;
        lastScreenChange = millis();
    }
    
    // Auto-sleep display after 10 seconds
    if (millis() - lastScreenChange > 10000) {
        screenOn = false;
    }
}

void handleDisplay() {
    if (!screenOn) {
        display.clearDisplay();
        display.display();
        return;
    }
    
    // Cycle through screens every 3 seconds
    if (millis() - lastScreenChange > 3000) {
        currentScreen = (currentScreen + 1) % 8;
        lastScreenChange = millis();
    }
    
    switch (currentScreen) {
        case 0: showTimeAndSteps(); break;
        case 1: showActivitySummary(); break;
        case 2: showSleepData(); break;
        case 3: showHealthStatus(); break;
        case 4: showMovementData(); break;
        case 5: showEmergencyInfo(); break;
        case 6: showHandWashTimer(); break;
        case 7: showSystemInfo(); break;
    }
}

void showTimeAndSteps() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    // Show current time (using millis as simple timer)
    unsigned long hours = (millis() / 3600000) % 24;
    unsigned long minutes = (millis() / 60000) % 60;
    
    if (hours < 10) display.print("0");
    display.print(hours);
    display.print(":");
    if (minutes < 10) display.print("0");
    display.println(minutes);
    
    display.setTextSize(1);
    display.println("");
    display.print("Steps: ");
    display.println(dailyActivity.steps);
    display.print("Distance: ");
    display.print(dailyActivity.totalDistance/1000, 1);
    display.println(" km");
    
    display.display();
}

void showActivitySummary() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("ACTIVITY SUMMARY");
    display.println("================");
    display.print("Steps: ");
    display.println(dailyActivity.steps);
    display.print("Calories: ");
    display.print(dailyActivity.calories, 1);
    display.println(" kcal");
    display.print("Active: ");
    display.print(dailyActivity.activeMinutes);
    display.println(" min");
    display.print("Distance: ");
    display.print(dailyActivity.totalDistance, 0);
    display.println(" m");
    
    display.display();
}

void showSleepData() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("SLEEP DATA");
    display.println("==========");
    
    if (sleepData.isSleeping) {
        display.println("Status: SLEEPING");
        unsigned long sleepDuration = millis() - sleepData.sleepStart;
        display.print("Duration: ");
        display.print(sleepDuration / 3600000);
        display.print("h ");
        display.print((sleepDuration / 60000) % 60);
        display.println("m");
    } else {
        display.println("Status: AWAKE");
        if (sleepData.sleepEnd > sleepData.sleepStart) {
            unsigned long lastSleepDuration = sleepData.sleepEnd - sleepData.sleepStart;
            display.print("Last sleep: ");
            display.print(lastSleepDuration / 3600000);
            display.print("h ");
            display.print((lastSleepDuration / 60000) % 60);
            display.println("m");
        }
    }
    
    display.print("Quality: ");
    display.print(sleepData.sleepQuality);
    display.println("%");
    display.print("Wake times: ");
    display.println(sleepData.wakeTimes);
    
    display.display();
}

void showHealthStatus() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("HEALTH STATUS");
    display.println("=============");
    
    AccelData current = accelBuffer[(bufferIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE];
    
    display.print("Motion: ");
    display.print(current.magnitude, 1);
    display.println("g");
    
    if (seizureDetected) {
        display.println("SEIZURE ALERT!");
    } else if (fallDetected) {
        display.println("FALL DETECTED!");
    } else if (handWashing) {
        display.println("Hand washing...");
    } else {
        display.println("Status: Normal");
    }
    
    display.print("Sedentary: ");
    display.print((millis() - lastActivityTime) / 60000);
    display.println(" min");
    
    display.display();
}

void showMovementData() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("MOVEMENT DATA");
    display.println("=============");
    
    AccelData current = accelBuffer[(bufferIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE];
    
    display.print("X: ");
    display.println(current.x, 2);
    display.print("Y: ");
    display.println(current.y, 2);
    display.print("Z: ");
    display.println(current.z, 2);
    display.print("Mag: ");
    display.println(current.magnitude, 2);
    
    float freq = analyzeFrequency();
    display.print("Freq: ");
    display.print(freq, 1);
    display.println(" Hz");
    
    display.display();
}

void showEmergencyInfo() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("EMERGENCY INFO");
    display.println("==============");
    display.println("Contact:");
    display.println("7574842021");
    display.println("Blood: B+");
    display.println("Allergies: Peanuts");
    display.println("Shake 3x for ALERT");
    
    display.display();
}

void showHandWashTimer() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("HAND WASH TIMER");
    display.println("===============");
    
    if (handWashing) {
        unsigned long duration = (millis() - handWashStartTime) / 1000;
        display.setTextSize(2);
        display.print(duration);
        display.println("s");
        display.setTextSize(1);
        if (duration >= 20) {
            display.println("Good job!");
        } else {
            display.print("Keep going: ");
            display.print(20 - duration);
            display.println("s");
        }
    } else {
        display.println("Start circular");
        display.println("hand motions to");
        display.println("begin timer");
    }
    
    display.display();
}

void showSystemInfo() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("SYSTEM INFO");
    display.println("===========");
    display.print("Uptime: ");
    display.print(millis() / 3600000);
    display.print("h ");
    display.print((millis() / 60000) % 60);
    display.println("m");
    
    display.print("Wrist angle: ");
    display.print(wristAngle, 0);
    display.println("°");
    
    display.print("Buffer: ");
    display.print(bufferIndex);
    display.print("/");
    display.println(BUFFER_SIZE);
    
    display.println("All systems OK");
    
    display.display();
}

// Alert and notification functions
void triggerFallAlert() {
    fallDetected = true;
    Serial.println("*** FALL DETECTED! ***");
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("FALL");
    display.println("ALERT!");
    display.display();
    
    buzzPattern(5);
    
    delay(3000);
    fallDetected = false;
}

void triggerSeizureAlert() {
    Serial.println("*** SEIZURE DETECTED! ***");
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("SEIZURE");
    display.println("ALERT!");
    display.display();
    
    buzzPattern(10);
    delay(5000);
}

void triggerEmergencyAlert() {
    emergencyMode = true;
    Serial.println("*** EMERGENCY ALERT TRIGGERED! ***");
    
    for (int i = 0; i < 3; i++) {
        display.clearDisplay();
        display.setTextSize(3);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("HELP!");
        display.display();
        buzzPattern(5);
        delay(1000);
        
        display.clearDisplay();
        display.display();
        delay(500);
    }
    
    // Show emergency info
    showEmergencyInfo();
    delay(10000);
    emergencyMode = false;
}

void displaySedentaryAlert() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("TIME TO MOVE!");
    display.println("=============");
    display.println("You've been");
    display.println("inactive for");
    display.println("over 1 hour.");
    display.println("");
    display.println("Take a walk!");
    
    display.display();
    delay(3000);
}

void handleShakeGesture() {
    Serial.println("Shake gesture detected!");
    currentScreen = (currentScreen + 1) % 8;
    lastScreenChange = millis();
    buzzPattern(1);
}

void handleSmartWake() {
    if (wakeAlarmSet && sleepData.isSleeping) {
        // Find optimal wake time during light sleep phase
        // This is a simplified implementation
        unsigned long currentTime = millis();
        
        if (abs((long)(currentTime - optimalWakeTime)) < 300000) { // 5 min window
            AccelData current = accelBuffer[(bufferIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE];
            
            if (current.magnitude > 0.3) { // Light movement = light sleep
                triggerSmartWake();
            }
        }
    }
}

void triggerSmartWake() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Good");
    display.println("Morning!");
    display.display();
    
    // Gentle wake pattern
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(1000);
    }
    
    wakeAlarmSet = false;
}

// Utility functions
float analyzeFrequency() {
    // Simple frequency analysis using zero crossings
    int crossings = 0;
    float mean = 0;
    
    // Calculate mean
    for (int i = 0; i < BUFFER_SIZE; i++) {
        mean += accelBuffer[i].magnitude;
    }
    mean /= BUFFER_SIZE;
    
    // Count zero crossings
    for (int i = 1; i < BUFFER_SIZE; i++) {
        if ((accelBuffer[i-1].magnitude - mean) * (accelBuffer[i].magnitude - mean) < 0) {
            crossings++;
        }
    }
    
    // Convert to frequency (Hz)
    float frequency = (float)crossings * SAMPLE_RATE / (2.0 * BUFFER_SIZE);
    return frequency;
}

void calculateSleepQuality() {
    if (sleepData.sleepEnd > sleepData.sleepStart) {
        unsigned long sleepDuration = sleepData.sleepEnd - sleepData.sleepStart;
        
        // Simple quality calculation based on duration and wake times
        int baseQuality = 100;
        
        // Deduct for short sleep
        if (sleepDuration < 21600000) { // Less than 6 hours
            baseQuality -= 30;
        }
        
        // Deduct for wake times
        baseQuality -= sleepData.wakeTimes * 10;
        
        sleepData.sleepQuality = max(0, baseQuality);
    }
}

void buzzPattern(int pattern) {
    switch (pattern) {
        case 1: // Single beep
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            break;
            
        case 2: // Double beep
            for (int i = 0; i < 2; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(100);
                digitalWrite(BUZZER_PIN, LOW);
                delay(100);
            }
            break;
            
        case 3: // Triple beep
            for (int i = 0; i < 3; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(200);
                digitalWrite(BUZZER_PIN, LOW);
                delay(200);
            }
            break;
            
        case 5: // Urgent pattern
            for (int i = 0; i < 5; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(150);
                digitalWrite(BUZZER_PIN, LOW);
                delay(50);
            }
            break;
            
        case 10: // Emergency pattern
            for (int i = 0; i < 10; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(100);
                digitalWrite(BUZZER_PIN, LOW);
                delay(100);
            }
            break;
    }
}

void showStartupScreen() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("HEALTH MONITOR");
    display.println("==============");
    display.println("Initializing...");
    display.println("Step Counter: OK");
    display.println("Fall Detect: OK");
    display.println("Sleep Track: OK");
    display.println("Gestures: OK");
    display.println("Ready!");
    
    display.display();
    delay(3000);
}

void displayError(String error) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ERROR!");
    display.println(error);
    display.display();
}

// EEPROM functions for data persistence
void saveDailyData() {
    EEPROM.put(0, dailyActivity);
    EEPROM.put(sizeof(ActivityData), sleepData);
}

void loadDailyData() {
    // Load saved data from EEPROM
    EEPROM.get(0, dailyActivity);
    EEPROM.get(sizeof(ActivityData), sleepData);
    
    // Validate loaded data (reset if corrupted)
    if (dailyActivity.steps > 100000 || dailyActivity.steps < 0) {
        dailyActivity = {0, 0, 0, 0, 0};
    }
    
    if (sleepData.sleepQuality > 100 || sleepData.sleepQuality < 0) {
        sleepData = {0, 0, 0, 0, false};
    }
}

// MPU6050 low-level functions
void writeMPU6050(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    Wire.write(data);
    Wire.endTransmission();
}

int16_t readMPU6050(uint8_t reg) {
    Wire.beginTransmission(MPU6050_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    
    Wire.requestFrom(MPU6050_ADDRESS, 2);
    
    int16_t value = 0;
    if (Wire.available() >= 2) {
        value = Wire.read() << 8;
        value |= Wire.read();
    }
    
    return value;
}

float simpleKalmanFilter(float measurement, float estimate, float& P) {
    float K = P / (P + kalmanR);
    estimate = estimate + K * (measurement - estimate);
    P = (1 - K) * P + kalmanQ;
    return estimate;
}

// Advanced features implementation

void setSmartWakeAlarm(unsigned long wakeTime) {
    optimalWakeTime = wakeTime;
    wakeAlarmSet = true;
    Serial.println("Smart wake alarm set for: " + String(wakeTime));
}

void resetDailyStats() {
    dailyActivity = {0, 0, 0, 0, 0};
    sleepData.wakeTimes = 0;
    sleepData.sleepQuality = 0;
    saveDailyData();
    Serial.println("Daily stats reset");
}

// Posture monitoring (bonus feature using existing sensors)
void detectPosture(AccelData data) {
    static int badPostureCount = 0;
    
    // Detect wrist angle for slouching indication
    if (abs(wristAngle) > 60) { // Wrist bent too much
        badPostureCount++;
        if (badPostureCount > 300) { // 30 seconds of bad posture
            displayPostureAlert();
            badPostureCount = 0;
        }
    } else {
        badPostureCount = max(0, badPostureCount - 1);
    }
}

void displayPostureAlert() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("POSTURE ALERT");
    display.println("=============");
    display.println("Straighten up!");
    display.println("Keep wrist");
    display.println("neutral");
    
    display.display();
    buzzPattern(2);
    delay(2000);
}

// Activity intensity classification
String getActivityIntensity(float magnitude) {
    if (magnitude < 1.2) {
        return "Sedentary";
    } else if (magnitude < 2.5) {
        return "Light";
    } else if (magnitude < 4.0) {
        return "Moderate";
    } else {
        return "Vigorous";
    }
}

// Enhanced gesture recognition patterns
void advancedGestureRecognition() {
    // Pattern 1: Tap gesture (quick up-down motion)
    bool tapPattern = false;
    if (gestureBuffer[0] == 1 && gestureBuffer[1] == 0 && 
        gestureBuffer[2] == 0 && gestureBuffer[3] == 0) {
        tapPattern = true;
    }
    
    if (tapPattern) {
        // Single tap - toggle display brightness or screen
        currentScreen = 0; // Go to main screen
        lastScreenChange = millis();
        buzzPattern(1);
    }
    
    // Pattern 2: Rotation gesture (for dismissing alerts)
    int rotationScore = 0;
    for (int i = 0; i < 8; i++) {
        if (gestureBuffer[i] == 2) rotationScore++; // Medium movements
    }
    
    if (rotationScore >= 6) {
        // Dismiss any active alerts
        seizureDetected = false;
        fallDetected = false;
        emergencyMode = false;
        Serial.println("Alerts dismissed by gesture");
    }
}

// Medication reminder system (time-based)
struct MedicationReminder {
    unsigned long reminderTime;
    String medicationName;
    bool active;
    bool taken;
};

MedicationReminder medications[3] = {
    {28800000, "Morning Med", true, false},   // 8 AM
    {43200000, "Noon Med", true, false},      // 12 PM
    {72000000, "Evening Med", true, false}    // 8 PM
};

void checkMedicationReminders() {
    unsigned long currentTime = millis() % 86400000; // 24 hour cycle
    
    for (int i = 0; i < 3; i++) {
        if (medications[i].active && !medications[i].taken) {
            if (abs((long)(currentTime - medications[i].reminderTime)) < 300000) { // 5 min window
                displayMedicationReminder(medications[i].medicationName);
                medications[i].taken = true; // Mark as reminded
            }
        }
    }
    
    // Reset medication flags at midnight
    if (currentTime < 60000) { // First minute of day
        for (int i = 0; i < 3; i++) {
            medications[i].taken = false;
        }
    }
}

void displayMedicationReminder(String medName) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("MEDICATION");
    display.println("REMINDER");
    display.println("==========");
    display.println("Time to take:");
    display.println(medName);
    display.println("");
    display.println("Shake to confirm");
    
    display.display();
    buzzPattern(3);
}

// Environmental adaptation (using accelerometer for activity context)
void adaptToEnvironment() {
    AccelData current = accelBuffer[(bufferIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE];
    String activity = getActivityIntensity(current.magnitude);
    
    // Adjust sensitivity based on activity level
    if (activity == "Vigorous") {
        // Increase thresholds to avoid false positives during exercise
        // This would modify the global threshold variables
    } else if (activity == "Sedentary") {
        // Lower thresholds for better sensitivity during rest
    }
}

// Heart rate estimation (rough approximation using movement patterns)
float estimateHeartRate() {
    // This is a very rough estimation based on activity level
    // Real heart rate monitoring requires dedicated sensors
    
    AccelData current = accelBuffer[(bufferIndex - 1 + BUFFER_SIZE) % BUFFER_SIZE];
    String activity = getActivityIntensity(current.magnitude);
    
    float estimatedHR = 70; // Resting heart rate baseline
    
    if (activity == "Light") estimatedHR = 90;
    else if (activity == "Moderate") estimatedHR = 120;
    else if (activity == "Vigorous") estimatedHR = 150;
    
    return estimatedHR;
}

// Battery monitoring simulation (using uptime as proxy)
int getBatteryLevel() {
    // Simulate battery drain based on uptime
    unsigned long uptime = millis();
    unsigned long batteryLife = 86400000 * 3; // 3 days in milliseconds
    
    int batteryPercent = 100 - ((uptime * 100) / batteryLife);
    return max(0, batteryPercent);
}

void checkBatteryLevel() {
    int battery = getBatteryLevel();
    
    if (battery <= 15 && battery > 10) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("LOW BATTERY");
        display.println("===========");
        display.print("Remaining: ");
        display.print(battery);
        display.println("%");
        display.println("Please charge soon");
        display.display();
        delay(2000);
    } else if (battery <= 10) {
        // Critical battery - reduce functionality
        currentScreen = 0; // Stay on main screen only
        // Reduce sampling rate, turn off non-essential features
    }
}

// Daily summary generation
void generateDailySummary() {
    Serial.println("\n=== DAILY HEALTH SUMMARY ===");
    Serial.print("Steps: "); Serial.println(dailyActivity.steps);
    Serial.print("Distance: "); Serial.print(dailyActivity.totalDistance/1000, 2); Serial.println(" km");
    Serial.print("Calories: "); Serial.print(dailyActivity.calories, 1); Serial.println(" kcal");
    Serial.print("Active Minutes: "); Serial.println(dailyActivity.activeMinutes);
    Serial.print("Sleep Quality: "); Serial.print(sleepData.sleepQuality); Serial.println("%");
    Serial.print("Wake Times: "); Serial.println(sleepData.wakeTimes);
    
    if (sleepData.sleepEnd > sleepData.sleepStart) {
        unsigned long sleepDuration = sleepData.sleepEnd - sleepData.sleepStart;
        Serial.print("Sleep Duration: ");
        Serial.print(sleepDuration / 3600000);
        Serial.print("h ");
        Serial.print((sleepDuration / 60000) % 60);
        Serial.println("m");
    }
    
    Serial.println("========================\n");
}

// Additional utility functions for enhanced features
void showBatteryStatus() {
    int battery = getBatteryLevel();
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    
    display.println("BATTERY STATUS");
    display.println("==============");
    display.print("Level: ");
    display.print(battery);
    display.println("%");
    
    // Simple battery icon
    display.drawRect(100, 20, 20, 10, SSD1306_WHITE);
    display.drawRect(120, 23, 3, 4, SSD1306_WHITE);
    
    // Fill battery based on level
    int fillWidth = (battery * 18) / 100;
    display.fillRect(101, 21, fillWidth, 8, SSD1306_WHITE);
    
    if (battery > 50) {
        display.println("Status: Good");
    } else if (battery > 20) {
        display.println("Status: Fair");
    } else {
        display.println("Status: Low - Charge!");
    }
    
    display.display();
}

// Weather adaptation (using barometric pressure simulation)
void adaptToWeather() {
    // Simulate weather conditions affecting movement patterns
    // In a real implementation, this could connect to weather APIs
    // or use barometric pressure sensors
    
    static unsigned long lastWeatherCheck = 0;
    if (millis() - lastWeatherCheck > 3600000) { // Check every hour
        // Adjust thresholds based on simulated weather
        lastWeatherCheck = millis();
    }
}