# Myosa_3

## Overview
MYOSA is an ESP32-based wearable health monitor with:
- Step counting
- Seizure detection
- Fall detection
- Real-time data upload to Firebase Realtime Database
- A web dashboard (Chart.js) to visualize sessions and control the device (buzzer, step counting)

The project contains firmware for ESP32 and a static web app that can be served locally or hosted on Firebase Hosting.

## Repository structure
```text
AppCode/
  firebase.json            # Firebase Hosting config (public directory)
  public/
    index.html             # Dashboard UI (Chart.js + controls)
    script.js              # Frontend logic (Firebase client, charts, controls)
    style.css              # Dashboard styles
  y/
    index.html, 404.html   # Alt/static pages (unused by Hosting config)

Project_code/
  Main_code/
    Main_code.ino          # Primary ESP32 firmware (WiFi + Firebase RTDB + sensors)
  claude/claude.ino        # Earlier firmware variant
  peprpexlity.ino          # Minimal motion-detection prototype
  proejctcode_building.ino # Intermediate experiment

Old_reference.ino          # Legacy prototype – useful for reference
```

## Hardware
- ESP32 DevKit (WiFi capable)
- MPU6050 accelerometer/gyroscope (I2C)
- 0.96" OLED display compatible with `oled.h` driver (SSD1306 class interface)
- Buzzer on GPIO 12
- Jumper wires and breadboard

### Wiring
- I2C: `MPU6050.SDA → ESP32 GPIO 21`, `MPU6050.SCL → ESP32 GPIO 22` (default Wire pins)
- VCC/GND: match board requirements (typically 3.3V to both OLED and MPU6050)
- OLED: I2C shared with MPU6050
- Buzzer: `+ → ESP32 GPIO 12`, `- → GND` (use appropriate resistor/transistor if needed)

## Cloud prerequisites (Firebase)
1. Create a Firebase project.
2. Enable Realtime Database (in test mode for development only). Region can be default.
3. Create an Email/Password user if using authenticated RTDB writes from the device.
4. Note the following values for the firmware:
   - API key
   - Database URL (e.g. `https://<project-id>-default-rtdb.firebaseio.com`)
   - Email and password for the device user

### RTDB paths used by the firmware
- `/initialSessionID` (int) → firmware reads/increments and builds a path like `/DataCollection/Session_<n>`
- `/DataCollection/Session_<n>/sensorData` (object of `{timestamp_ms: net_accel_g}`)
- `/DataCollection/Session_<n>/stepCount` (int)
- `/StepDetect` (bool) → toggles local step counting mode
- `/FindMy` (bool) → when true, device buzzes and resets the flag to false

Security note: For production, lock down RTDB rules. Do not keep API keys or credentials in public repos.

## Firmware (ESP32) setup
Target sketch: `Project_code/Main_code/Main_code.ino`

### Arduino IDE configuration
- Board: ESP32 Dev Module (install ESP32 core via Boards Manager)
- Upload speed: 921600 (or reliable default)
- Partition scheme: Default
- PSRAM: Disabled (unless your board has it)

### Libraries (Library Manager or GitHub)
- `FirebaseESP32` (Mobizt)
- `WiFi` (bundled with ESP32 core)
- `Wire` (bundled)
- `oled` driver compatible with `SSD1306` interface used in code as `oLed` and `SSD1306_WHITE`
- `ArduinoJson` (indirectly used via Firebase JSON helpers; typically bundled with FirebaseESP32)

### Configure credentials
Open `Project_code/Main_code/Main_code.ino` and set:
```c++
#define WIFI_SSID "<your-ssid>"
#define WIFI_PASSWORD "<your-password>"
#define API_KEY "<your-firebase-api-key>"
#define DATABASE_URL "https://<your-project-id>-default-rtdb.firebaseio.com"
#define USER_EMAIL "<device-user@email>"
#define USER_PASSWORD "<device-user-password>"
```

Replace any hardcoded placeholders currently present. Never commit real secrets.

### Flash and run
1. Connect ESP32 via USB.
2. Select the correct COM port.
3. Upload `Main_code.ino`.
4. Open Serial Monitor at 115200 baud to verify:
   - WiFi connected
   - Firebase initialized
   - Session path created
   - Periodic uploads every ~5s

### Runtime behavior
- Reads MPU6050 at ~50 Hz, computes net acceleration, buffers up to 300 points, pushes a JSON batch to `/DataCollection/Session_<n>/sensorData`.
- Sends `/DataCollection/Session_<n>/stepCount` periodically.
- Listens for `/StepDetect` and `/FindMy` flags.
- Buzzer on GPIO 12 for alerts and Find-My.
- OLED shows live status and alerts.

## Web Dashboard
Location: `AppCode/public/`

Features:
- Lists available sessions
- Shows line chart of sensor data with Chart.js
- Displays step count, data points, duration
- Control buttons for "Find My" (buzzer) and toggling step counting

### Live site
- Primary: `https://myosa-3.web.app`
- Alternate (auth domain): `https://myosa-3.firebaseapp.com`

If your hosting target uses a custom domain, add it here as well.

### How to use the website (controls and displays)
- Sessions list:
  - Autoloads from `/DataCollection` and renders buttons named like `Session_<n>`.
  - Click a session to load its data; the active button is highlighted.
  - The page refreshes the selected session every ~1s.
- Controls:
  - Buzzer button (`Find My`): toggles `/FindMy` true/false. Device buzzes when true, then resets to false.
  - Step counting button: toggles `/StepDetect` true/false to enable on-device step counting.
- Stats shown:
  - `Total Steps`: reads `stepCount` from the selected session.
  - `Data Points`: total samples processed in the selected session.
  - `Session Duration`: difference between first and last sensor timestamps.
  - `Active Session`: the currently selected session name.
- Chart:
  - Plots scaled acceleration derived from session `sensorData` values.
  - Scaling: `(value - 1.0) * 1000` for visibility around 1g baseline.
  - X-axis uses human-readable times from timestamps; updates on each refresh.

### Local development options
Any static server works (module scripts require HTTP, not file://):

- Python
  ```bash
  cd AppCode/public
  python -m http.server 5000
  # visit http://localhost:5000
  ```

- Node (serve)
  ```bash
  npx serve AppCode/public -p 5000
  # visit http://localhost:5000
  ```

Ensure `script.js` has your Firebase Web SDK config and points to the same RTDB paths used by the firmware.

### Firebase Hosting (optional)
Prereq: Firebase CLI
```bash
npm i -g firebase-tools
firebase login
```

Deploy from `AppCode/` (hosting public folder is `public/` per `firebase.json`):
```bash
cd AppCode
firebase init hosting  # if not already initialized for your project
firebase deploy --only hosting
```

## Data model
```text
/
  initialSessionID: int
  StepDetect: bool
  FindMy: bool
  DataCollection/
    Session_<n>/
      sensorData/
        <timestamp_ms>: <net_accel_g>
      stepCount: int
```

## Troubleshooting
- WiFi fails to connect: check SSID/password and 2.4GHz availability.
- Firebase errors: verify API key, Database URL, and device user credentials. Check RTDB rules.
- Empty chart: confirm device is uploading under the same project and that `script.js` reads from the correct paths.
- OLED blank: confirm the `oled` library and wiring; ensure I2C address matches (MPU6050 at 0x69).
- No sensor data: check MPU6050 wiring and power; verify I2C lines and pull-ups if necessary.

## Safety and security
- Do not publish real credentials. Use environment-specific secrets.
- Tighten RTDB security rules for production.
- Thresholds for seizure/fall detection are tuned for testing; validate clinically before real-world use.

## License
Add your license of choice here.
