// Firebase imports
import { initializeApp } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-app.js";
import { getDatabase, ref, set, get, child } from "https://www.gstatic.com/firebasejs/11.0.1/firebase-database.js";

// Firebase config
const firebaseConfig = {
  apiKey: "AIzaSyByS6w7S7xJol6hBMkEH6QK0DNHsa6c5CU",
  authDomain: "myosa-3.firebaseapp.com",
  databaseURL: "https://myosa-3-default-rtdb.firebaseio.com/",
  projectId: "myosa-3",
  storageBucket: "myosa-3.firebasestorage.app",
  messagingSenderId: "663467455606",
  appId: "1:663467455606:web:92de401cd3e832310d1b57",
};

// Init
const app = initializeApp(firebaseConfig);
const database = getDatabase(app);
const dbRef = ref(database);

// Write data helper
function writeData(path, data) {
  set(ref(database, path), data)
    .then(() => console.log("Data saved at:", path))
    .catch((error) => console.error("Write failed:", error));
}

// Read once helper
function readDataOnce(path) {
  get(child(dbRef, path))
    .then((snapshot) => {
      if (snapshot.exists()) {
        console.log("Data:", snapshot.val());
      } else {
        console.log("No data available at", path);
      }
    })
    .catch((error) => console.error("Read failed:", error));
}

// Buzz button logic
const buzzButton = document.getElementById("buzzButton");
buzzButton.addEventListener("click", () => {
  let value = buzzButton.value === "true"; // convert to boolean
  value = !value; // toggle

  // Update button style
  if (value) {
    buzzButton.style.backgroundColor = "green";
    buzzButton.style.color = "white";
  } else {
    buzzButton.style.backgroundColor = "red";
    buzzButton.style.color = "black";
  }

  buzzButton.value = value; // store as string for next toggle
  writeData("/FindMy", value); // Save boolean to Firebase

  console.log("Buzz button clicked:", value);
});


// from here experimetal chart stuff