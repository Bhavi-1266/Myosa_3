#include <AccelAndGyro.h>
#include <Wire.h>
#include <math.h>
#include <oled.h>
#include <EEPROM.h>

/* Creating Objects */
AccelAndGyro Ag;
oLed display(SCREEN_WIDTH, SCREEN_HEIGHT);

/* Constants */
#define NOISE_THRESHOLD 0.5
#define SEIZURE_THRESHOLD 15.0
#define STEP_THRESHOLD 1.1        // g-force for step detection
#define FALL_FREE_FALL 0.4        // g (free-fall detection)
#define FALL_IMPACT 3.0           // g (impact detection)
#define FALL_IMMOBILITY_TIME 5000 // ms (post-fall no movement)
#define BUZZER_PIN 12
#define BUTTON_PIN 13             // For dismissing alerts

/* Kalman Filter */
float filteredAx = 0, filteredAy = 0, filteredAz = 0;
float kalmanQ = 0.01, kalmanR = 0.5;
float P_ax = 1.0, P_ay = 1.0, P_az = 1.0;

/* Gait Tracking */
uint32_t stepCount = 0;
float cadence = 0;
unsigned long lastStepTime = 0;
uint32_t stepTimes[20];  // For cadence calculation

/* Fall Detection */
enum FallState { NONE, FREE_FALL, IMPACT, IMMOBILE };
FallState fallState = NONE;
unsigned long fallStartTime = 0;

/* Seizure Detection */
unsigned long seizureStartTime = 0;
bool seizureDetected = false;

/* EEPROM Logging */
const int LOG_START_ADDR = 0;
struct EventLog {
  uint32_t timestamp;
  uint8_t eventType;  // 1=Seizure, 2=Fall, 3=GaitAlert
  float severity;
};
int logIndex = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(100000);

  // Initialize OLED
  if(!display.begin()) {
    Serial.println("SSD1306 allocation failed");
    while(1); // Halt if display fails
  }
  display.clearDisplay();
  display.display();
  
  // Initialize IMU
  while (!Ag.begin()) {
    Serial.println("Accelerometer Sensor is disconnected!");
    alertPattern(2);  // 2 beeps = sensor error
    delay(500);
  }

  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  displayDefaultScreen();
}

void loop() {
  // Check sensor connection
  if (Ag.ping()) {
    // Get raw sensor data
    float rawAx = Ag.getAccelX(false);
    float rawAy = Ag.getAccelY(false);
    float rawAz = Ag.getAccelZ(false);

    // Apply Kalman filter
    filteredAx = simpleKalmanFilter(rawAx / 16384.0, filteredAx, P_ax);
    filteredAy = simpleKalmanFilter(rawAy / 16384.0, filteredAy, P_ay);
    filteredAz = simpleKalmanFilter(rawAz / 16384.0, filteredAz, P_az);

    float resultantAcc = sqrt(filteredAx*filteredAx + filteredAy*filteredAy + filteredAz*filteredAz);

    //----- Detection Systems -----//
    detectSeizure(resultantAcc);
    detectSteps(filteredAy);
    detectFall(resultantAcc);

    // Update display every 200ms
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 200) {
      updateDisplay();
      lastDisplayUpdate = millis();
    }
  }

  // Handle button press (dismiss alerts)
  if (digitalRead(BUTTON_PIN) == LOW) {
    clearAlerts();
    delay(500);  // Debounce
  }

  delay(20);  // ~50Hz sampling
}

//----- Detection Algorithms -----//
void detectSeizure(float accel) {
  if (accel > SEIZURE_THRESHOLD) {
    if (!seizureDetected) {
      seizureStartTime = millis();
      seizureDetected = true;
    } else if (millis() - seizureStartTime >= 400) {
      triggerAlert("SEIZURE", 3);
      logEvent(1, accel);
      seizureDetected = false;
    }
  } else {
    seizureDetected = false;
  }
}

void detectSteps(float accelY) {
  static float lastAccel = 0;
  bool isPeak = (accelY > STEP_THRESHOLD) && (lastAccel <= STEP_THRESHOLD);
  
  if (isPeak && (millis() - lastStepTime > 300)) {
    stepCount++;
    
    // Update cadence buffer
    static uint8_t stepBufferIndex = 0;
    stepTimes[stepBufferIndex] = millis() - lastStepTime;
    stepBufferIndex = (stepBufferIndex + 1) % 20;
    
    // Calculate cadence (steps/min)
    if (stepCount % 5 == 0) {
      unsigned long avgStepTime = 0;
      for (int i = 0; i < 20; i++) avgStepTime += stepTimes[i];
      cadence = 60000.0 / (avgStepTime / 20.0);
    }
    
    lastStepTime = millis();
  }
  lastAccel = accelY;
}

void detectFall(float accel) {
  switch (fallState) {
    case NONE:
      if (accel < FALL_FREE_FALL) {
        fallState = FREE_FALL;
        fallStartTime = millis();
      }
      break;
      
    case FREE_FALL:
      if (accel > FALL_IMPACT) {
        fallState = IMPACT;
        triggerAlert("FALL DETECTED", 2);
        logEvent(2, accel);
      } else if (millis() - fallStartTime > 2000) {
        fallState = NONE;  // False alarm
      }
      break;
      
    case IMPACT:
      if (millis() - fallStartTime > FALL_IMMOBILITY_TIME) {
        if (accel < 0.8) {  // Low movement
          triggerAlert("NEED HELP?", 1);
        }
        fallState = NONE;
      }
      break;
  }
}

//----- Alert System -----//
void triggerAlert(const char* message, int severity) {
  alertPattern(severity);
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(message);
  
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Time:");
  display.print(millis() / 1000);
  display.print("s");
  
  if (severity >= 2) {
    display.setCursor(0, 35);
    display.print("Hold button to dismiss");
  }
  display.display();
}

void alertPattern(int severity) {
  for (int i = 0; i < severity; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100 * severity);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void clearAlerts() {
  displayDefaultScreen();
}

//----- Data Logging -----//
void logEvent(uint8_t type, float severity) {
  EventLog log;
  log.timestamp = millis();
  log.eventType = type;
  log.severity = severity;
  
  EEPROM.put(LOG_START_ADDR + (logIndex * sizeof(EventLog)), log);
  logIndex = (logIndex + 1) % (512 / sizeof(EventLog));
  EEPROM.commit();
}

//----- Display Functions -----//
void displayDefaultScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Steps:");
  display.print(stepCount);
  
  display.setCursor(0, 12);
  display.print("Cadence:");
  display.print(int(cadence));
  display.print("/min");
  
  display.setCursor(0, 24);
  display.print("Status:");
  display.print(fallState == NONE ? "Normal" : "Alert!");
  display.display();
}

void updateDisplay() {
  static uint8_t counter = 0;
  counter = (counter + 1) % 4;
  
  // Blink status during alerts
  if (fallState != NONE || seizureDetected) {
    display.setTextSize(1);
    display.setCursor(70, 24);
    display.print(counter < 2 ? "ALERT!" : "      ");
  }
  display.display();
}

//----- Kalman Filter -----//
float simpleKalmanFilter(float measurement, float estimate, float& P) {
  float K = P / (P + kalmanR);
  estimate = estimate + K * (measurement - estimate);
  P = (1 - K) * P + kalmanQ;
  return estimate;
}