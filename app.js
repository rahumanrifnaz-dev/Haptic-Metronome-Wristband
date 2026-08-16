// --- 1. FIREBASE SETUP ---
const firebaseConfig = {
    databaseURL: "https://hapticproject-c5aaf-default-rtdb.asia-southeast1.firebasedatabase.app/"
};
firebase.initializeApp(firebaseConfig);
const database = firebase.database();

const soundRef = database.ref('live_session/bpm');
const forceRef = database.ref('live_session/force');
const motorRef = database.ref('live_session/motor');

// --- 2. HTML ELEMENT REFERENCES ---
const bpmValueText = document.getElementById('bpm-value');
const bpmLabelText = document.getElementById('bpm-label');
const statusIndicator = document.getElementById('status-indicator');
const statusText = document.getElementById('status-text');
const pulseRing = document.getElementById('pulse-ring');

const motorInput = document.getElementById('motor-bpm-input');
const transmitBtn = document.getElementById('transmit-btn');
const currentTxDisplay = document.getElementById('current-tx-bpm');

const btnSound = document.getElementById('btn-sound');
const btnForce = document.getElementById('btn-force');

// --- 3. DUAL SENSOR STATE MANAGEMENT ---
let currentSensor = 'sound';

const sensorData = {
    sound: { bpm: "--", active: false, history: [], labels: [], color: '#38bdf8', title: 'MIC BPM', status: 'MIC ACTIVE' },
    force: { bpm: "--", active: false, history: [], labels: [], color: '#f59e0b', title: 'WRIST BPM', status: 'FORCE ACTIVE' }
};

// --- 4. CHART SETUP ---
const ctx = document.getElementById('bpmChart').getContext('2d');
const bpmChart = new Chart(ctx, {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Live BPM', data: [], borderColor: '#38bdf8', backgroundColor: 'rgba(56, 189, 248, 0.15)', borderWidth: 3, pointRadius: 0, tension: 0.4, fill: true }] },
    options: { responsive: true, maintainAspectRatio: false, scales: { y: { suggestedMin: 50, suggestedMax: 150, grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { color: '#94a3b8' } }, x: { display: false } }, plugins: { legend: { display: false } } }
});

// --- 5. UI TOGGLE LOGIC ---
btnSound.addEventListener('click', () => switchView('sound'));
btnForce.addEventListener('click', () => switchView('force'));

function switchView(sensor) {
    currentSensor = sensor;
    
    // Toggle Buttons
    btnSound.classList.toggle('active', sensor === 'sound');
    btnForce.classList.toggle('active', sensor === 'force');
    
    // Toggle Ring Theme Color
    if (sensor === 'force') {
        pulseRing.classList.add('force-theme');
    } else {
        pulseRing.classList.remove('force-theme');
    }

    // Update Chart Colors
    bpmChart.data.datasets[0].borderColor = sensorData[sensor].color;
    bpmChart.data.datasets[0].backgroundColor = sensorData[sensor].color + '25';
    
    updateDisplay();
}

// --- 6. MOTOR CONTROL LOGIC ---
transmitBtn.addEventListener('click', () => {
    const targetBpm = parseInt(motorInput.value);
    
    if (targetBpm >= 0 && targetBpm <= 250) {
        motorRef.set({ target_bpm: targetBpm })
            .then(() => {
                if (targetBpm === 0) {
                    currentTxDisplay.innerText = "STOPPED";
                    currentTxDisplay.style.color = "#ef4444"; 
                } else {
                    currentTxDisplay.innerText = targetBpm + " BPM";
                    currentTxDisplay.style.color = "#10b981"; 
                }
                
                transmitBtn.innerText = "Transmitted! ✓";
                transmitBtn.style.background = "#059669";
                
                setTimeout(() => {
                    transmitBtn.innerHTML = "<span class='btn-icon'>📡</span> Transmit";
                    transmitBtn.style.background = "#10b981";
                }, 2000);
            }).catch((error) => alert("Transmission failed: " + error));
    } else {
        alert("Please enter a valid BPM between 0 and 250.");
    }
});

motorRef.on('value', (snapshot) => {
    const data = snapshot.val();
    if(data && data.target_bpm > 0) {
        currentTxDisplay.innerText = data.target_bpm + " BPM";
        currentTxDisplay.style.color = "#10b981";
    } else {
        currentTxDisplay.innerText = "None";
        currentTxDisplay.style.color = "#f59e0b";
    }
});

// --- 7. SENSOR DATA HANDLING & BEAT ANIMATION ---
let beatInterval = null;

function updateDisplay() {
    const data = sensorData[currentSensor];
    
    bpmLabelText.innerText = data.title;
    bpmValueText.innerText = data.bpm;
    
    if (data.active) {
        statusIndicator.classList.add('active');
        statusText.innerText = data.status;
    } else {
        statusIndicator.classList.remove('active');
        statusText.innerText = "WAITING FOR HARDWARE...";
    }

    // Refresh Chart
    bpmChart.data.labels = data.labels;
    bpmChart.data.datasets[0].data = data.history;
    bpmChart.update();

    // The visual ring pulses continuously to give the metronome feel
    clearInterval(beatInterval);
    if (data.bpm > 0 && data.bpm !== "--") {
        beatInterval = setInterval(() => {
            pulseRing.classList.remove('beat');
            setTimeout(() => pulseRing.classList.add('beat'), 10);
        }, 60000 / data.bpm);
    }
}

// THIS IS THE REAL-TIME ENGINE
// It fires instantly the millisecond Firebase gets new data from LabVIEW
function processIncomingData(sensorType, snapshot) {
    const rawData = snapshot.val();
    const timeNow = new Date().toLocaleTimeString([], { hour12: false });

    if (rawData !== null && rawData !== undefined) {
        let liveBpm = typeof rawData === 'object' ? Math.round(rawData.bpm) : Math.round(rawData);
        
        sensorData[sensorType].bpm = liveBpm;
        sensorData[sensorType].active = true;
        
        // Push to individual history array instantly
        sensorData[sensorType].labels.push(timeNow);
        sensorData[sensorType].history.push(liveBpm);
        
        // Keep the chart looking clean with the last 20 points
        if (sensorData[sensorType].labels.length > 20) {
            sensorData[sensorType].labels.shift();
            sensorData[sensorType].history.shift();
        }
    } else {
        sensorData[sensorType].bpm = "--";
        sensorData[sensorType].active = false;
    }

    // Instantly update the screen if the user is looking at this sensor's tab
    if (currentSensor === sensorType) {
        updateDisplay();
    }
}

// Attach the instant listeners
soundRef.on('value', (s) => processIncomingData('sound', s));
forceRef.on('value', (s) => processIncomingData('force', s));