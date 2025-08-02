#include <AccelAndGyro.h>
#include <Wire.h>
#include <math.h>
#include <oled.h>


/* Creating Objects */
AccelAndGyro Ag;
oLed display(SCREEN_WIDTH, SCREEN_HEIGHT);
/* Constants */
#define NOISE_THRESHOLD 0.5
#define MAX_HAND_ACCEL 30.0
#define SEIZURE_THRESHOLD 15.0
#define SEIZURE_DURATION 400 // Time in ms for continuous abnormal acceleration
#define BUZZER_PIN 12 // Pin connected to buzzer

/* Kalman Filter State Variables */
float filteredAx = 0, filteredAy = 0, filteredAz = 0;
float kalmanQ = 0.01; // Process noise covariance
float kalmanR = 0.5;  // Measurement noise covariance
float P_ax = 1.0, P_ay = 1.0, P_az = 1.0; // Error covariance for X, Y, Z axes

/* Seizure detection variables */
unsigned long seizure_start_time = 0;
bool seizure_detected = false;

void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(100000);

    if(!display.begin())
{
  Serial.println("SSD1306 allocation failed");
}
else
{
  // Draw a single pixel in white
  display.drawPixel(10, 10, SSD1306_WHITE);

  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();
  delay(500);
  display.clearDisplay();
}

    // Initialize the sensor
    while (!Ag.begin()) {
        Serial.println("Accelerometer Sensor is disconnected!");
        delay(500);
    }
    Serial.println("Accelerometer Sensor is connected");

    // Setup the buzzer pin
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is off at the start
    display.setCursor(0, 0);
        display.print("No seziure");
        display.display();
    
}

void loop() {
    // Check if the sensor is connected
    if (Ag.ping()) {

        // Get raw accelerometer data
        float rawAx = Ag.getAccelX(false);
        float rawAy = Ag.getAccelY(false);
        float rawAz = Ag.getAccelZ(false);

        // Apply simple scalar Kalman filter smoothing
        filteredAx = simpleKalmanFilter(rawAx / 16384.0, filteredAx, P_ax);
        filteredAy = simpleKalmanFilter(rawAy / 16384.0, filteredAy, P_ay);
        filteredAz = simpleKalmanFilter(rawAz / 16384.0, filteredAz, P_az);

        // Calculate the resultant acceleration
        float resultantAcc = 140 * sqrt(filteredAx * filteredAx + filteredAy * filteredAy + filteredAz * filteredAz);
        logSeizureData(filteredAx, filteredAy, filteredAz, resultantAcc);

        // Filter out noise and extreme values
        if (resultantAcc < NOISE_THRESHOLD || resultantAcc > MAX_HAND_ACCEL) {
            resetSeizureDetection();
        } else {
        
            // Check for seizure-like motion
            if (resultantAcc > SEIZURE_THRESHOLD) {
                if (!seizure_detected) {
                    seizure_start_time = millis();
                    seizure_detected = true;
                } else if (millis() - seizure_start_time >= SEIZURE_DURATION) {
                    // Seizure confirmed
                    Serial.println("Seizure Detected! Alert!");
                    buzzBuzzer(10);
                     // Trigger the buzzer to buzz 10 times
                    resetSeizureDetection();
                }
            } else {
                resetSeizureDetection();
            }
        }
    }

    delay(100); // Update rate
}

/*
* A simple Kalman filter implementation for scalar data smoothing
*/
float simpleKalmanFilter(float measurement, float estimate, float& P) {
    float K = P / (P + kalmanR); // Kalman gain
    estimate = estimate + K * (measurement - estimate);
    P = (1 - K) * P + kalmanQ; // Update the error covariance
    return estimate;
}

/* Reset seizure detection logic */
void resetSeizureDetection() {
    seizure_start_time = 0;
    seizure_detected = false;
}

/* Log accelerometer data when a seizure is detected */
void logSeizureData(float ax, float ay, float az, float resultantAcc) {
    Serial.print("Filtered ax: ");
    Serial.print(ax);
    Serial.print("\tFiltered ay: ");
    Serial.print(ay);
    Serial.print("\tFiltered az: ");
    Serial.print(az);
    Serial.print("\tResultant Acc: ");
    Serial.println(resultantAcc);
}

/* Buzz the buzzer 10 times */
void buzzBuzzer(int numBzzz) {
  display.clearDisplay();
  display.setTextSize(3);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);
  display.print("seziure ALERT");
  display.display();
  delay(1000);
  display.clearDisplay();
  display.display();
  display.setCursor(0, 3);
  display.setTextSize(1);
  display.print("CONTACT -- 7574842021  BLOOD GROUP -- b+   ALLERGIES--PEANUTS   [PREVIOUS SURGERY]       [QR CODE ]");
  display.display();

    for (int i = 0; i < numBzzz; i++) {
        digitalWrite(BUZZER_PIN, HIGH); // Turn on the buzzer
        delay(100);                      // Keep it on for 100ms
        digitalWrite(BUZZER_PIN, LOW);  // Turn off the buzzer
        delay(100);                      // Wait for 100ms
    }
}
