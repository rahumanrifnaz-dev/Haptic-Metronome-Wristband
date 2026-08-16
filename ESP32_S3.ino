#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_now.h>
#include <IRremote.hpp>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= WIFI DETAILS =================
const char* ssid = "M13";
const char* password = "rrpr@123";

// ================= OLED =================
#define SDA_PIN 8
#define SCL_PIN 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= SPEAKER =================
#define SPEAKER 35

// ================= IR REMOTE =================
#define IR_PIN 4

#define BTN_UP      24
#define BTN_DOWN    82
#define BTN_OK      28
#define BTN_ZERO    25
#define BTN_STAR    22

// ================= UDP =================
const int udpPort = 4210;

// ================= ESP-NOW RECEIVER MACS =================
uint8_t receiver1[] = {0xB0, 0xA6, 0x04, 0x04, 0x55, 0x48};
uint8_t receiver2[] = {0xB0, 0xA6, 0x04, 0x07, 0xA3, 0x88};

// ================= SYNC SETTINGS =================
#define MAX_SYNC_SENDS 3
#define BPM_CHANGE_SYNC_INTERVAL 500

// Small two-tock sound during BPM change sync period
#define SYNC_TOCK_GAP 140

// ================= DATA STRUCTURE =================
typedef struct {
  int sync;
  int bpm;
  int running;
} Message;

Message data;
WiFiUDP udp;

bool espNowStarted = false;
bool sendingSyncs = false;

bool metronomeRunning = false;
bool speakerOn = false;

int currentBPM = 120;
int syncSendCount = 0;

unsigned long lastSyncTime = 0;
unsigned long lastButtonTime = 0;

unsigned long beatInterval = 500;
unsigned long nextBeat = 0;
int beatNumber = 1;

// ================= BPM CHANGE TOCK STATE =================
bool syncTocksActive = false;
int syncTocksPlayed = 0;
unsigned long nextSyncTockTime = 0;


// ================= SPEAKER ICON =================
void drawSpeakerIcon(bool on) {
  display.fillRect(88, 0, 30, 12, SSD1306_BLACK);

  if (!on) return;

  display.drawCircle(94, 6, 2, SSD1306_WHITE);

  display.drawPixel(99, 3, SSD1306_WHITE);
  display.drawPixel(100, 4, SSD1306_WHITE);
  display.drawPixel(100, 5, SSD1306_WHITE);
  display.drawPixel(100, 6, SSD1306_WHITE);
  display.drawPixel(100, 7, SSD1306_WHITE);
  display.drawPixel(99, 8, SSD1306_WHITE);

  display.drawPixel(104, 1, SSD1306_WHITE);
  display.drawPixel(105, 2, SSD1306_WHITE);
  display.drawPixel(106, 3, SSD1306_WHITE);
  display.drawPixel(106, 4, SSD1306_WHITE);
  display.drawPixel(106, 5, SSD1306_WHITE);
  display.drawPixel(106, 6, SSD1306_WHITE);
  display.drawPixel(106, 7, SSD1306_WHITE);
  display.drawPixel(105, 8, SSD1306_WHITE);
  display.drawPixel(104, 9, SSD1306_WHITE);
}


// ================= OLED UPDATE =================
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(4);
  display.setCursor(0, 2);
  display.print(currentBPM);

  display.setTextSize(2);
  display.setCursor(90, 16);
  display.print(metronomeRunning ? "ON" : "OFF");

  drawSpeakerIcon(speakerOn);

  display.display();
}


// ================= STOP SPEAKER =================
void stopSpeakerSound() {
  ledcWrite(SPEAKER, 0);
}


// ================= WOODEN TICK =================
void playTick(int beat) {
  int freq = (beat == 1) ? 1500 : 1000;
  int vol  = (beat == 1) ? 160  : 90;

  ledcWriteTone(SPEAKER, freq);

  ledcWrite(SPEAKER, vol);
  delay(3);

  ledcWrite(SPEAKER, (int)(vol * 0.6));
  delay(4);

  ledcWrite(SPEAKER, (int)(vol * 0.35));
  delay(5);

  ledcWrite(SPEAKER, (int)(vol * 0.15));
  delay(6);

  ledcWrite(SPEAKER, 0);
}


// ================= SMALL BPM CHANGE TOCK =================
void playSyncTock() {
  ledcWriteTone(SPEAKER, 1200);

  ledcWrite(SPEAKER, 75);
  delay(3);

  ledcWrite(SPEAKER, 35);
  delay(4);

  ledcWrite(SPEAKER, 0);
}


// ================= START TWO SMALL TOCKS =================
void startSyncTocks() {
  if (!speakerOn) {
    syncTocksActive = false;
    return;
  }

  syncTocksActive = true;
  syncTocksPlayed = 0;
  nextSyncTockTime = millis();
}


// ================= HANDLE TWO SMALL TOCKS =================
void handleSyncTocks() {
  if (!syncTocksActive) return;

  if (!speakerOn) {
    syncTocksActive = false;
    stopSpeakerSound();
    return;
  }

  unsigned long now = millis();

  if ((long)(now - nextSyncTockTime) >= 0) {
    playSyncTock();

    syncTocksPlayed++;

    if (syncTocksPlayed >= 2) {
      syncTocksActive = false;
    } else {
      nextSyncTockTime = millis() + SYNC_TOCK_GAP;
    }
  }
}


// ================= ESP-NOW =================
void addPeer(uint8_t *receiver) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiver, 6);

  peerInfo.channel = WiFi.channel();
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("ESP-NOW peer added");
  } else {
    Serial.println("Failed to add ESP-NOW peer");
  }
}


void startEspNow() {
  if (espNowStarted) return;

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  addPeer(receiver1);
  addPeer(receiver2);

  espNowStarted = true;
}


void sendToBothC3s() {
  esp_now_send(receiver1, (uint8_t *)&data, sizeof(data));
  esp_now_send(receiver2, (uint8_t *)&data, sizeof(data));
}


// ================= FINISH BPM SYNC PERIOD =================
void finishBpmSyncPeriod() {
  sendingSyncs = false;
  syncTocksActive = false;

  // After the 1 second sync period, restart the local master beat clock.
  // This makes the S3 speaker line up with the C3 wristbands after BPM changes.
  beatNumber = 1;
  nextBeat = millis();

  Serial.println("BPM sync period finished. Local speaker clock restarted at beat 1.");
}


void sendSync() {
  data.sync = 1;
  data.bpm = currentBPM;
  data.running = -1;

  sendToBothC3s();

  syncSendCount++;

  Serial.print("SYNC ");
  Serial.print(syncSendCount);
  Serial.print("/3 sent to both C3s | BPM = ");
  Serial.println(currentBPM);

  lastSyncTime = millis();

  if (syncSendCount >= MAX_SYNC_SENDS) {
    Serial.println("3 sync pulses sent. Stopped ESP-NOW sending.");
    finishBpmSyncPeriod();
  }
}


void sendRunningCommand() {
  startEspNow();
  if (!espNowStarted) return;

  data.sync = 0;
  data.bpm = currentBPM;
  data.running = metronomeRunning ? 1 : 0;

  sendToBothC3s();

  // When metronome starts, all devices begin from beat 1.
  // When metronome stops, speaker standalone mode also starts cleanly from beat 1.
  beatNumber = 1;
  nextBeat = millis();

  Serial.println(metronomeRunning ? "Metronome ON" : "Metronome OFF");

  updateDisplay();
}


void triggerThreeSyncs() {
  startEspNow();

  syncSendCount = 0;
  sendingSyncs = true;

  // During BPM change sync period, normal speaker ticks stop.
  // Instead, speaker gives two small tocks as BPM-change indication.
  startSyncTocks();

  if (!espNowStarted) {
    Serial.println("ESP-NOW not started. Skipping sync pulses.");
    finishBpmSyncPeriod();
    return;
  }

  sendSync();
}


// ================= BPM UPDATE =================
void updateBPM(int newBPM, const char* source) {
  if (newBPM < 80 || newBPM > 150) {
    Serial.print("Invalid BPM ignored: ");
    Serial.println(newBPM);
    return;
  }

  currentBPM = newBPM;
  beatInterval = 60000UL / currentBPM;

  Serial.print(strcmp(source, "LABVIEW") == 0 ? "Valid BPM received from LabVIEW: " : "Remote BPM: ");
  Serial.println(currentBPM);

  updateDisplay();

  // Start 1 second BPM sync period.
  // During this time the speaker gives two small tocks, not normal beats.
  triggerThreeSyncs();
}


// ================= REMOTE =================
void handleRemote(int command) {
  if (command == BTN_UP) {
    updateBPM(currentBPM + 5, "REMOTE");
  }

  else if (command == BTN_DOWN) {
    updateBPM(currentBPM - 5, "REMOTE");
  }

  else if (command == BTN_ZERO) {
    updateBPM(120, "REMOTE");
  }

  else if (command == BTN_OK) {
    metronomeRunning = !metronomeRunning;
    sendRunningCommand();
  }

  else if (command == BTN_STAR) {
    speakerOn = !speakerOn;

    if (speakerOn) {
      Serial.println("Speaker enabled");

      if (metronomeRunning) {
        // IMPORTANT:
        // Do NOT reset beatNumber here.
        // The speaker must join the already-running metronome beat clock.
        Serial.print("Speaker locked to current metronome beat. Next beat = ");
        Serial.println(beatNumber);
      } 
      else {
        // If metronome is OFF, speaker works alone from beat 1.
        beatNumber = 1;
        nextBeat = millis();

        Serial.println("Speaker standalone mode started from beat 1");
      }

      if (sendingSyncs) {
        startSyncTocks();
      }
    } 
    else {
      Serial.println("Speaker disabled");
      syncTocksActive = false;
      stopSpeakerSound();
    }

    updateDisplay();
  }
}


// ================= MASTER BEAT CLOCK =================
void handleBeatClock() {
  // During the BPM-change sync period, normal beats are muted.
  // Only the two small sync tocks are allowed.
  if (sendingSyncs) return;

  // Clock should run if:
  // 1. metronome is ON, even if speaker is OFF
  // 2. speaker is ON by itself while metronome is OFF
  if (!metronomeRunning && !speakerOn) return;

  unsigned long now = millis();

  if ((long)(now - nextBeat) >= 0) {
    int thisBeat = beatNumber;

    if (speakerOn) {
      playTick(thisBeat);
    }

    nextBeat += beatInterval;

    beatNumber++;
    if (beatNumber > 4) {
      beatNumber = 1;
    }

    // Safety correction in case something blocks too long.
    // Prevents old beats from being fired rapidly.
    if ((long)(millis() - nextBeat) > (long)beatInterval) {
      nextBeat = millis() + beatInterval;
    }
  }
}


// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
  } else {
    Serial.println("OLED ready");
    updateDisplay();
  }

  IrReceiver.begin(IR_PIN);

  ledcAttach(SPEAKER, 1500, 8);
  ledcWrite(SPEAKER, 0);

  beatInterval = 60000UL / currentBPM;
  nextBeat = millis();

  WiFi.mode(WIFI_STA);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");

  Serial.print("ESP32-S3 IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());

  udp.begin(udpPort);

  Serial.print("Listening for LabVIEW UDP BPM on port ");
  Serial.println(udpPort);

  data.sync = 0;
  data.bpm = currentBPM;
  data.running = -1;

  updateDisplay();
}


// ================= LOOP =================
void loop() {
  // ---------- IR REMOTE ----------
  if (IrReceiver.decode()) {
    int command = IrReceiver.decodedIRData.command;

    if (millis() - lastButtonTime > 300) {
      handleRemote(command);
      lastButtonTime = millis();
    }

    IrReceiver.resume();
  }

  // ---------- UDP BPM FROM LABVIEW ----------
  int packetSize = udp.parsePacket();

  if (packetSize) {
    char packetBuffer[32];

    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    packetBuffer[len] = '\0';

    int receivedBPM = atoi(packetBuffer);
    updateBPM(receivedBPM, "LABVIEW");
  }

  // ---------- SEND 3 BPM SYNC PULSES ----------
  if (sendingSyncs && espNowStarted) {
    unsigned long now = millis();

    if (now - lastSyncTime >= BPM_CHANGE_SYNC_INTERVAL) {
      sendSync();
    }
  }

  // ---------- TWO SMALL BPM-CHANGE TOCKS ----------
  handleSyncTocks();

  // ---------- MASTER BEAT CLOCK ----------
  handleBeatClock();
}
