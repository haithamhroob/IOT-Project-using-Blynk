#include <WiFi.h>
#include <PubSubClient.h>

// ====== WiFi ======
const char* ssid     = "Redmi Note 13";
const char* password = "haitham2005";

// ====== MQTT ======
const char* mqtt_server = "10.22.255.183";   // IP broker
const int   mqtt_port   = 1883;

const char* TOPIC_TEMP        = "iot/project/temperature";
const char* TOPIC_BUZZER_MODE = "iot/project/buzzer/mode";
const char* TOPIC_BUZZER_CMD  = "iot/project/buzzer/cmd";
const char* TOPIC_BUZZER_STAT = "iot/project/buzzer/state";
const char* TOPIC_SOUND       = "iot/project/sound";

WiFiClient espClient;
PubSubClient client(espClient);

// ====== Pins ======
#define SOUND_PIN  34      // حساس الصوت
#define BUZZER_PIN 32      // <<< البازر هنا كما طلبت

// ====== Logic Vars ======
float currentTemp     = 0.0;
int   buzzerMode      = 1;     // 1 = Auto, 0 = Manual
int   buzzerCmd       = 0;     // Manual command from Node1
int   lastBuzzerState = -1;

// ====== Sound sensor vars ======
int lastSoundState = -1;

// ====== Buzzer Helpers ======
void publishBuzzerState(int state) {
  if (client.connected()) {
    const char* msg = state ? "1" : "0";
    client.publish(TOPIC_BUZZER_STAT, msg, true);
  }
}

void startBuzzer() {
  if (lastBuzzerState != 1) {
    tone(BUZZER_PIN, 2000);   // صوت قوي ومستمر
    lastBuzzerState = 1;
    publishBuzzerState(1);
    Serial.println("BUZZER: ON (continuous)");
  }
}

void stopBuzzer() {
  if (lastBuzzerState != 0) {
    noTone(BUZZER_PIN);
    lastBuzzerState = 0;
    publishBuzzerState(0);
    Serial.println("BUZZER: OFF");
  }
}

// ====== Main Buzzer Logic ======
void updateBuzzer() {
  int desired;

  if (buzzerMode == 1) {
    // AUTO MODE
    desired = (currentTemp > 24.0) ? 1 : 0;
  } else {
    // MANUAL MODE
    desired = buzzerCmd ? 1 : 0;
  }

  if (desired == 1) startBuzzer();
  else              stopBuzzer();
}

// ====== MQTT callback ======
void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.print("MQTT [");
  Serial.print(t);
  Serial.print("] ");
  Serial.println(msg);

  if (t == TOPIC_TEMP) {
    currentTemp = msg.toFloat();
  }
  else if (t == TOPIC_BUZZER_MODE) {
    buzzerMode = msg.toInt();
  }
  else if (t == TOPIC_BUZZER_CMD) {
    buzzerCmd = msg.toInt();
  }
// هذا الكول باك موجود عشان استقبل الرسائل اللي جاية من البروكر وأشوف شو التوبيك واعدل البزر بناء عليه
  updateBuzzer();
}

// ====== MQTT reconnect ======
void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT...");
    if (client.connect("ESP32_Node2")) {
      Serial.println("connected");
      client.subscribe(TOPIC_TEMP);
      client.subscribe(TOPIC_BUZZER_MODE);
      client.subscribe(TOPIC_BUZZER_CMD);
    } else {
      Serial.println("retrying...");
      delay(1000);
    }
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);

  pinMode(SOUND_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);

  // TEST BEEP — للتأكد أن البازر سليم
  Serial.println("Test beep...");
  tone(BUZZER_PIN, 2000);
  delay(500);
  noTone(BUZZER_PIN);
  Serial.println("Test beep done.");

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nWiFi OK");

  // MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ====== LOOP ======
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // Sound sensor: send 0/1 when changed
  int soundVal = digitalRead(SOUND_PIN);
  if (soundVal != lastSoundState) {
    lastSoundState = soundVal;
    client.publish(TOPIC_SOUND, soundVal ? "1" : "0");
    Serial.print("Sound = ");
    Serial.println(soundVal);
  }

  // always keep buzzer updated
  updateBuzzer();
}
