/**
 * ESP32 Health Monitor Web Dashboard
 * 
 * Features:
 * - Fetch all session IDs from Firebase Realtime Database
 * - Display sensor data graphs with scaling and sampling
 * - Control ESP32 "FindMy" buzzer remotely
 * - Real-time data visualization using Chart.js
 */

// ============================================================================
// FIREBASE CONFIGURATION AND INITIALIZATION
// ============================================================================

// Firebase imports
import { initializeApp } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-app.js";
import { getDatabase, ref, set, get, child } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-database.js";

// Configuration constants
const samplingRate = 100; // Take every 100th data point for performance

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
        
        // Create a button for each session ID
        sessionIDs.forEach((sessionID) => {
          const sessionButton = document.createElement("button");
          sessionButton.textContent = `Session: ${sessionID}`;
          sessionButton.className = "button";
          sessionButton.setAttribute("id", `session-${sessionID}`);
          sessionButton.setAttribute("value", sessionID);
          
          // Add click handler to load session data
          sessionButton.onclick = () => {
            const currentSession = sessionButton.value;
            console.log("Loading session:", currentSession);
            readSensorData(`/DataCollection/${sessionID}`);
          };
          
          // Add button to the UI
          document.getElementById("listSessionID").appendChild(sessionButton);
        });
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

      const sensorData = allData.sensorData;
      if (!sensorData) {
        console.log('No sensorData node found in session');
        return;
      }

      // Clear previous chart data
      labels.length = 0;
      dataValues.length = 0;

      let pointIndex = 0;
      const baseline = 1;  // Baseline for scaling (sensor values around 1.0)

      // Process each batch of sensor data
      Object.keys(sensorData).forEach(batchKey => {
        const batch = sensorData[batchKey];

        // Process each timestamp-value pair in the batch
        Object.keys(batch).forEach(timestampKey => {
          // Apply sampling: only use every Nth point
          if (pointIndex % samplingRate === 0) {
            const rawValue = batch[timestampKey];
            
            // Add timestamp to labels (could convert to readable time)
            labels.push(timestampKey);
            
            // Scale the value: subtract baseline and multiply for visibility
            const scaledValue = 1000 * (rawValue - baseline);
            dataValues.push(scaledValue);
          }
          pointIndex++;
        });
      });

      console.log(`Processed ${labels.length} sampled points from ${pointIndex} total points`);
      console.log('Data range:', Math.min(...dataValues).toFixed(2), 'to', Math.max(...dataValues).toFixed(2));

      // Update the chart with new data
      lineChart.data.labels = labels;
      lineChart.data.datasets[0].data = dataValues;
      lineChart.update();

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
  if (isActive) {
    buzzButton.style.backgroundColor = "green";
    buzzButton.style.color = "white";
    buzzButton.textContent = "Buzzer ON";
  } else {
    buzzButton.style.backgroundColor = "red";
    buzzButton.style.color = "white";
    buzzButton.textContent = "Buzzer OFF";
  }

  // Save new state
  buzzButton.value = isActive;
  writeData("/FindMy", isActive);

  console.log("Buzzer state changed to:", isActive);
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
const chartConfig = {
  type: 'line',
  data: chartData,
  options: {
    responsive: true,
    animation: false, // Disable animation for better performance with large datasets
    scales: {
      x: {
        display: true,
        title: {
          display: true,
          text: 'Time (Unix Timestamp)'
        }
      },
      y: {
        display: true,
        title: {
          display: true,
          text: 'Scaled Acceleration ((value-1)*1000)'
        },
        suggestedMin: -2000, // Adjust these based on your expected data range
        suggestedMax: 2000
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
      }
    }
  }
};

// Create the chart
const ctx = document.getElementById('lineChart').getContext('2d');
const lineChart = new Chart(ctx, chartConfig);

// ============================================================================
// APPLICATION INITIALIZATION
// ============================================================================

/**
 * Initialize the application when the page loads
 */
window.addEventListener("load", () => {
  console.log("ESP32 Health Monitor Dashboard loaded");
  console.log(`Using sampling rate: 1 out of every ${samplingRate} points`);
  
  // Load available sessions
  getSessionID();
  
  // Initialize buzzer button state
  buzzButton.value = "false";
  buzzButton.style.backgroundColor = "red";
  buzzButton.style.color = "white";
  buzzButton.textContent = "Buzzer OFF";
});

// ============================================================================
// END OF SCRIPT
// ============================================================================
