#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define MOTOR 8
#define ESPNOW_CHANNEL 6
#define MAX_SYNC_COUNT 3

int bpm = 120;
int previousBpm = 0;
int interval = 500;

int strongPulse = 0;
int weakPulse = 0;

int syncCount = 0;

bool synced = false;
bool metronomeRunning = false;

unsigned long lastBeat = 0;
unsigned long motorStart = 0;

int motorPulseTime = 0;
int beatNumber = 1;

bool motorOn = false;

typedef struct {
  int sync;
  int bpm;
  int running;
} Message;

Message received;

void calculatePulseTimes() {
  strongPulse = 300 - bpm;
  weakPulse = 80 - ((bpm - 80) * 30 / 70);
}

void startMetronomeBeatOne() {
  Serial.println("1");

  digitalWrite(MOTOR, HIGH);
  motorOn = true;

  motorStart = millis();
  motorPulseTime = strongPulse;

  beatNumber = 2;
  lastBeat = millis();

  synced = true;
}

void stopMetronome() {
  metronomeRunning = false;
  synced = false;
  motorOn = false;

  digitalWrite(MOTOR, LOW);

  Serial.println("METRONOME OFF");
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {

  if (len != sizeof(Message)) {
    return;
  }

  memcpy(&received, data, sizeof(received));

  // ================= START / STOP COMMAND =================
  if (received.running == 1) {
    metronomeRunning = true;

    Serial.println("METRONOME ON");

    calculatePulseTimes();
    startMetronomeBeatOne();

    return;
  }

  if (received.running == 0) {
    stopMetronome();
    return;
  }

  // ================= BPM SYNC COMMAND =================
  if (received.sync == 1) {

    int newBpm = received.bpm;

    if (newBpm < 80 || newBpm > 150) {
      return;
    }

    if (newBpm != previousBpm) {
      syncCount = 0;
      previousBpm = newBpm;
      synced = false;
      motorOn = false;
      digitalWrite(MOTOR, LOW);
    }

    if (syncCount >= MAX_SYNC_COUNT) {
      return;
    }

    bpm = newBpm;
    interval = 60000 / bpm;

    calculatePulseTimes();

    syncCount++;

    Serial.print("BPM RECEIVED = ");
    Serial.println(bpm);

    if (!metronomeRunning) {
      return;
    }

    if (syncCount == 1 || syncCount == 2) {
      digitalWrite(MOTOR, HIGH);
      motorOn = false;
    }

    else if (syncCount == 3) {
      startMetronomeBeatOne();
    }
  }
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  pinMode(MOTOR, OUTPUT);
  digitalWrite(MOTOR, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  delay(100);

  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  delay(100);

  uint8_t primaryChan;
  wifi_second_chan_t secondChan;

  esp_wifi_get_channel(&primaryChan, &secondChan);

  Serial.print("C3 CHANNEL = ");
  Serial.println(primaryChan);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);

  calculatePulseTimes();

  Serial.println("C3 READY");
}

void loop() {

  unsigned long now = millis();

  if (metronomeRunning && synced && now - lastBeat >= interval) {

    lastBeat = now;

    digitalWrite(MOTOR, HIGH);
    motorOn = true;

    if (beatNumber == 1) {
      motorPulseTime = strongPulse;
      Serial.println("1");
    } else {
      motorPulseTime = weakPulse;
      Serial.println(beatNumber);
    }

    motorStart = now;

    beatNumber++;

    if (beatNumber > 4) {
      beatNumber = 1;
    }
  }

  if (motorOn && now - motorStart >= motorPulseTime) {
    digitalWrite(MOTOR, LOW);
    motorOn = false;
  }
}
