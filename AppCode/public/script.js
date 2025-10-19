/**
 * ESP32 Health Monitor Web Dashboard
 * 
 * Features:
 * - Fetch all session IDs from Firebase Realtime Database
 * - Display sensor data graphs with scaling and sampling
 * - Control ESP32 "FindMy" buzzer remotely
 * - Real-time data visualization using Chart.js
 * - Auto-scroll functionality for real-time updates
 */

// ============================================================================
// FIREBASE CONFIGURATION AND INITIALIZATION
// ============================================================================

// Firebase imports
import { initializeApp } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-app.js";
import { getDatabase, ref, set, get, child } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-database.js";

// Configuration constants
const samplingRate = 1; // Take every 100th data point for performance
let selectedSessionID = null;
let sessionUpdateInterval = null;

// Auto-scroll control
let autoScrollEnabled = true; // Enable auto-scroll by default
let userHasZoomed = false; // Track if user manually zoomed/panned

// Firebase project configuration
const firebaseConfig = {
  apiKey: "AIzaSyByS6w7S7xJol6hBMkEH6QK0DNHsa6c5CU",
  authDomain: "myosa-3.firebaseapp.com",
  databaseURL: "https://myosa-3-default-rtdb.firebaseio.com/",
  projectId: "myosa-3",
  storageBucket: "myosa-3.firebasestorage.app",
  messagingSenderId: "663467455606",
  appId: "1:663467455606:web:92de401cd3e832310d1b57",
};

// Initialize Firebase app and database references
const app = initializeApp(firebaseConfig);
const database = getDatabase(app);
const dbRef = ref(database);

// Additional variables for statistics tracking
let currentSessionData = null;
let totalDataPoints = 0;
const ONLINE_TIMEOUT_MS = 10000; // consider device online if last sample within this window
let lastDataTimestamp = 0; // last sensor timestamp observed (ms since epoch)

// ============================================================================
// FIREBASE DATABASE HELPER FUNCTIONS
// ============================================================================

/**
 * Write data to a specific path in Firebase Realtime Database
 * @param {string} path - Database path to write to
 * @param {any} data - Data to write
 */
function writeData(path, data) {
  set(ref(database, path), data)
    .then(() => console.log("✓ Data saved successfully at:", path))
    .catch((error) => console.error("✗ Write failed:", error));
}

/**
 * Read data once from a specific path in Firebase
 * @param {string} path - Database path to read from
 */
function readDataOnce(path) {
  get(child(dbRef, path))
    .then((snapshot) => {
      if (snapshot.exists()) {
        console.log("Data found at", path, ":", snapshot.val());
      } else {
        console.log("No data available at", path);
      }
    })
    .catch((error) => console.error("Read failed:", error));
}

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================

/**
 * Retrieve all session IDs from Firebase and create clickable buttons
 * Each button loads the corresponding session's sensor data when clicked
 */
const getSessionID = () => {
  get(child(dbRef, '/DataCollection'))
    .then((snapshot) => {
      if (snapshot.exists()) {
        const allSessions = snapshot.val();
        console.log("All Sessions found:", allSessions);
        
        const sessionIDs = Object.keys(allSessions);
        console.log("Session IDs:", sessionIDs);

        // Sort sessions by numeric suffix descending (Session_12 > Session_7)
        const sortedSessionIDs = sessionIDs.slice().sort((a, b) => {
          const an = parseInt(String(a).split('_')[1], 10);
          const bn = parseInt(String(b).split('_')[1], 10);
          if (!Number.isNaN(an) && !Number.isNaN(bn)) return bn - an;
          return String(a).localeCompare(String(b));
        });

        // Create a button for each session ID in sorted order
        sortedSessionIDs.forEach((sessionID) => {
          const sessionButton = document.createElement("button");
          sessionButton.textContent = `Session: ${sessionID}`;
          sessionButton.className = "button";
          sessionButton.setAttribute("id", `session-${sessionID}`);
          sessionButton.setAttribute("value", sessionID);
          
          // Add click handler to load session data
          sessionButton.onclick = () => {
            // Remove active class from all buttons
            document.querySelectorAll('.session-list .button').forEach(btn => {
                btn.classList.remove('active');
            });
              // Add active class to clicked button
              sessionButton.classList.add('active');

              // Track currently selected session ID
              selectedSessionID = sessionButton.value;

              // Reset auto-scroll when switching sessions
              autoScrollEnabled = true;
              userHasZoomed = false;

              // Load once immediately
              readSensorData(`/DataCollection/${selectedSessionID}`);

              // Clear previous interval if any
              if (sessionUpdateInterval) clearInterval(sessionUpdateInterval);

              // Every 1 second, update chart and step count
              sessionUpdateInterval = setInterval(() => {
                readSensorData(`/DataCollection/${selectedSessionID}`);
              }, 1000);

          };
          
          // Add button to the UI
          document.getElementById("listSessionID").appendChild(sessionButton);
        });

        // Preselect the numerically latest session (e.g., Session_123 > Session_7)
        const latestSessionId = (() => {
          let maxPair = [-Infinity, null];
          sessionIDs.forEach((id) => {
            const parts = String(id).split('_');
            const num = parts.length > 1 ? parseInt(parts[1], 10) : NaN;
            if (!Number.isNaN(num) && num > maxPair[0]) {
              maxPair = [num, id];
            }
          });
          return maxPair[1] || sessionIDs.sort().slice(-1)[0];
        })();

        if (latestSessionId) {
          const btn = document.getElementById(`session-${latestSessionId}`);
          if (btn && typeof btn.onclick === 'function') {
            btn.onclick();
          }
        }
      } else {
        console.log("No sessions found in DataCollection");
      }
    })
    .catch((error) => {
      console.error("Error reading session IDs:", error);
    });
};

// ============================================================================
// SENSOR DATA PROCESSING
// ============================================================================

// Chart data arrays (will be populated by sensor data)
const labels = [];      // X-axis labels (timestamps)
const dataValues = [];  // Y-axis values (scaled sensor readings)

/**
 * Read and process sensor data from a specific session
 * Applies sampling and scaling to make small sensor values visible
 * @param {string} path - Firebase path to session data
 */
const readSensorData = (path) => {
  get(child(dbRef, path))
    .then((snapshot) => {
      if (!snapshot.exists()) {
        console.log('No data found at', path);
        return;
      }

      const allData = snapshot.val();
      console.log('Raw session data loaded');
      
      currentSessionData = allData;

      // Update step count display
      const stepCount = allData.stepCount || 0;
      document.getElementById('stepCountDisplay').textContent = stepCount.toLocaleString();

      // Update current session display
      const sessionName = path.split('/').pop();
      document.getElementById('currentSessionDisplay').textContent = sessionName;

      const sensorData = allData.sensorData;
      if (!sensorData) {
        console.log('No sensorData node found in session');
        return;
      }

      // Clear previous chart data
      labels.length = 0;
      dataValues.length = 0;

      let pointIndex = 0;
      let totalPoints = 0;
      const baseline = 1;  // Baseline for scaling (sensor values around 1.0)
      let firstTimestamp = null;
      let lastTimestamp = null;

      // Collect all timestamp-value pairs first for sorting
      const allPoints = [];

      // Process each batch of sensor data
      Object.keys(sensorData).forEach(batchKey => {
        const batch = sensorData[batchKey];

        // Process each timestamp-value pair in the batch
        Object.keys(batch).forEach(timestampKey => {
          const timestamp = parseInt(timestampKey);
          const rawValue = batch[timestampKey];
          allPoints.push({ timestamp, rawValue });
        });
      });

      // Sort by timestamp to ensure chronological order
      allPoints.sort((a, b) => a.timestamp - b.timestamp);

      // Now process sorted points
      allPoints.forEach(point => {
        totalPoints++;
        const timestamp = point.timestamp;
        
        if (!firstTimestamp) firstTimestamp = timestamp;
        lastTimestamp = timestamp;

        // Apply sampling: only use every Nth point
        if (pointIndex % samplingRate === 0) {
          const rawValue = point.rawValue;
          
          // Convert timestamp to readable time (local timezone)
          const date = new Date(timestamp);
          const timeLabel = date.toLocaleTimeString('en-US', { 
            hour12: false,
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
          });
          labels.push(timeLabel);
          
          // Scale the value: subtract baseline and multiply for visibility
          const scaledValue = 1000 * (rawValue - baseline);
          dataValues.push(scaledValue);
        }
        pointIndex++;
      });

      // Update statistics displays
      totalDataPoints = totalPoints;
      document.getElementById('dataPointsDisplay').textContent = totalDataPoints.toLocaleString();
      
      // Online/offline detection based on the latest data timestamp
      if (lastTimestamp) {
        lastDataTimestamp = Math.max(lastDataTimestamp, lastTimestamp);
      }
      const now = Date.now();
      const isOnline = lastDataTimestamp > 0 && (now - lastDataTimestamp) <= ONLINE_TIMEOUT_MS;
      updateDeviceStatus(isOnline);
      
      // Calculate session duration
      if (firstTimestamp && lastTimestamp) {
        const duration = (lastTimestamp - firstTimestamp) / 1000; // seconds
        const minutes = Math.floor(duration / 60);
        const seconds = Math.floor(duration % 60);
        document.getElementById('sessionDurationDisplay').textContent = `${minutes}m ${seconds}s`;
      }

      console.log(`Processed ${labels.length} sampled points from ${totalPoints} total points`);
      console.log('Data range:', Math.min(...dataValues).toFixed(2), 'to', Math.max(...dataValues).toFixed(2));

      // Update the chart with new data
      lineChart.data.labels = labels;
      lineChart.data.datasets[0].data = dataValues;
      
      // Apply auto-scroll if enabled
      if (autoScrollEnabled && !userHasZoomed) {
        scrollToLatest();
      }
      
      lineChart.update('none'); // Use 'none' mode for better performance

    })
    .catch((error) => {
      console.error('Error reading sensor data:', error);
    });
};

// ============================================================================
// ESP32 REMOTE CONTROL
// ============================================================================

/**
 * Handle the "FindMy" buzzer button click
 * Toggles the buzzer state on the ESP32 device via Firebase
 */
const buzzButton = document.getElementById("buzzButton");
buzzButton.addEventListener("click", () => {
  // Parse current state and toggle
  let isActive = buzzButton.value === "true";
  isActive = !isActive;

  // Update button appearance
  const buttonText = buzzButton.querySelector('span');
  if (isActive) {
    buzzButton.style.backgroundColor = "var(--secondary-color)";
    buzzButton.classList.add('active');
    buttonText.textContent = "Buzzer ON";
  } else {
    buzzButton.style.backgroundColor = "var(--danger-color)";
    buzzButton.classList.remove('active');
    buttonText.textContent = "Buzzer OFF";
  }

  // Save new state
  buzzButton.value = isActive;
  writeData("/FindMy", isActive);

  console.log("Buzzer state changed to:", isActive);
});


/**
 * Handle the "Step Counting"  button click
 * Toggles the step counting state on the ESP32 device via Firebase
 */
const StepButton = document.getElementById("StepCountButoon");
StepButton.addEventListener("click", () => {
  // Parse current state and toggle
  let isActive = StepButton.value === "true";
  isActive = !isActive;

  // Update button appearance
  const buttonText = StepButton.querySelector('span');
  if (isActive) {
    StepButton.style.backgroundColor = "var(--secondary-color)";
    StepButton.classList.add('active');
    buttonText.textContent = "STEP COUNTING ON";
  } else {
    StepButton.style.backgroundColor = "var(--danger-color)";
    StepButton.classList.remove('active');
    buttonText.textContent = "STEP COUNTING OFF";
  }

  // Save new state
  StepButton.value = isActive;
  writeData("/StepDetect", isActive);

  console.log("Step Counting changed to:", isActive);
});



// ============================================================================
// CHART.JS CONFIGURATION AND INITIALIZATION
// ============================================================================

// Chart data structure
const chartData = {
  labels: labels,
  datasets: [{
    label: 'Accelerometer Data (scaled)',
    data: dataValues,
    fill: false,
    borderColor: 'rgba(75, 192, 192, 1)',
    backgroundColor: 'rgba(75, 192, 192, 0.4)',
    tension: 0.3,
    pointRadius: 0.3,
    pointHoverRadius: 0.7,
  }]
};

// Chart configuration
const VISIBLE_POINTS = 300; // default visible window size

const chartConfig = {
  type: 'line',
  data: chartData,
  options: {
    responsive: true,
    maintainAspectRatio: false,
    animation: false, // Disable animation for better performance with large datasets
    scales: {
      x: {
        display: true,
        title: {
          display: true,
          text: 'Time'
        },
        ticks: {
          maxRotation: 0,
          autoSkip: true,
        }
      },
      y: {
        display: true,
        title: {
          display: true,
          text: 'Scaled Acceleration ((value-1)*1000)'
        },
        suggestedMin: -25, // Adjust these based on your expected data range
        suggestedMax: 25
      }
    },
    plugins: {
      legend: {
        display: true,
        position: 'top'
      },
      tooltip: {
        enabled: true,
        callbacks: {
          // Custom tooltip to show original value
          label: function(context) {
            const scaledValue = context.parsed.y;
            const originalValue = (scaledValue / 1000) + 1;
            return `Scaled: ${scaledValue.toFixed(2)}, Original: ${originalValue.toFixed(4)}g`;
          }
        }
      },
      zoom: {
        pan: {
          enabled: true,
          mode: 'x',
          modifierKey: 'ctrl',
          onPanStart: function() {
            // User manually panned, disable auto-scroll
            userHasZoomed = true;
            autoScrollEnabled = false;
            console.log('Auto-scroll disabled: User panned');
          }
        },
        zoom: {
          wheel: {
            enabled: true
          },
          pinch: {
            enabled: true
          },
          mode: 'x',
          onZoomStart: function() {
            // User manually zoomed, disable auto-scroll
            userHasZoomed = true;
            autoScrollEnabled = false;
            console.log('Auto-scroll disabled: User zoomed');
          }
        },
        limits: {
          x: { minRange: 10 }
        }
      }
    }
  }
};

// Create the chart
const ctx = document.getElementById('lineChart').getContext('2d');
const lineChart = new Chart(ctx, chartConfig);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Scroll to the latest data in the chart
 * Works for both dense view (all data) and zoomed view (window)
 */
function scrollToLatest() {
  const total = lineChart.data.labels.length;
  if (total === 0) return;
  
  // Check current zoom level
  const xScale = lineChart.scales.x;
  const currentMin = xScale.min;
  const currentMax = xScale.max;
  
  // If user has zoomed (range is smaller than total), maintain zoom level
  if (currentMin !== undefined && currentMax !== undefined) {
    const currentRange = currentMax - currentMin;
    
    // Keep same zoom range but shift to latest data
    const newMax = total - 1;
    const newMin = Math.max(0, newMax - currentRange);
    
    xScale.options.min = newMin;
    xScale.options.max = newMax;
  } else {
    // Dense view: show last VISIBLE_POINTS
    if (total > VISIBLE_POINTS) {
      const newMin = total - VISIBLE_POINTS;
      const newMax = total - 1;
      xScale.options.min = newMin;
      xScale.options.max = newMax;
    } else {
      // Show all data if less than VISIBLE_POINTS
      xScale.options.min = undefined;
      xScale.options.max = undefined;
    }
  }
}

/**
 * Reset chart zoom to default view and re-enable auto-scroll
 */
window.resetZoom = function() {
  lineChart.resetZoom();
  userHasZoomed = false;
  autoScrollEnabled = true;
  console.log('Auto-scroll re-enabled');
};

/**
 * Public helper to snap view to the latest window and re-enable auto-scroll
 */
window.resetToLatestWindow = function() {
  userHasZoomed = false;
  autoScrollEnabled = true;
  
  const total = lineChart.data.labels.length;
  if (total > VISIBLE_POINTS) {
    const xScale = lineChart.scales.x;
    const newMin = total - VISIBLE_POINTS;
    const newMax = total - 1;
    xScale.options.min = newMin;
    xScale.options.max = newMax;
  } else {
    lineChart.resetZoom();
  }
  
  lineChart.update('none');
  console.log('Jumped to latest window, auto-scroll re-enabled');
};

function updateDeviceStatus(isOnline) {
  const dot = document.querySelector('.status-dot');
  const text = document.querySelector('.status-text');
  if (!dot || !text) return;
  if (isOnline) {
    dot.classList.remove('offline');
    dot.classList.add('online');
    text.textContent = 'Online';
  } else {
    dot.classList.remove('online');
    dot.classList.add('offline');
    text.textContent = 'Offline';
  }
}

// ============================================================================
// APPLICATION INITIALIZATION
// ============================================================================

/**
 * Initialize the application when the page loads
 */
window.addEventListener("load", () => {
  console.log("ESP32 Health Monitor Dashboard loaded");
  console.log(`Using sampling rate: 1 out of every ${samplingRate} points`);
  console.log("Auto-scroll enabled by default");
  
  // Load available sessions
  getSessionID();
  
  // Initialize buzzer button state
  buzzButton.value = "false";
  buzzButton.style.backgroundColor = "var(--danger-color)";
  buzzButton.style.color = "white";
  const buttonText = buzzButton.querySelector('span');
  buttonText.textContent = "Buzzer OFF";

  // Start status as Offline until fresh data arrives
  updateDeviceStatus(false);
});

// ============================================================================
// END OF SCRIPT
// ============================================================================
