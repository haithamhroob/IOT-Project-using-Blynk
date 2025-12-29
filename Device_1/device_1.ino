/*************************************************
 *  Node1 (ESP32 + DHT11 + 2 LEDs + Blynk + MQTT)
 *
 *  - Temperature:
 *      * Reads DHT11 on GPIO 25
 *      * Sends to Blynk V0
 *      * Publishes to MQTT "iot/project/temperature"
 *
 *  - LEDs (Green on 26, Yellow on 27):
 *      * On startup:
 *          Mode = AUTO
 *          Green OFF, Yellow ON
 *      * Mode via V5:
 *          0 = MANUAL:
 *              - V3 controls Green
 *              - V4 controls Yellow
 *              - sound is ignored
 *          1 = AUTO:
 *              - every sound rising edge (0→1) toggles LEDs:
 *                    (Y ON, G OFF) <-> (G ON, Y OFF)
 *              - any tap on V3/V4 is ignored and
 *                widgets are forced back to real LED state
 *
 *      * Switching MANUAL → AUTO:
 *          - reset to (Y ON, G OFF) then continue toggling on sound
 *      * Switching AUTO → MANUAL:
 *          - keep current LED state, then user controls by V3/V4
 *
 *  - Buzzer on Node2:
 *      * V12 = 0 (Manual): send V13 as "iot/project/buzzer/cmd"
 *      * V12 = 1 (Auto):  Node2 controls by temperature only
 *************************************************/
//cd "C:\Program Files\mosquitto"
//.\mosquitto.exe -c mosquitto.conf -v
//بكتب السطرين اللي فوق على الباورشيل عشان يشتغل البروكر
#define BLYNK_TEMPLATE_ID   ""
#define BLYNK_TEMPLATE_NAME ""
#define BLYNK_AUTH_TOKEN    "PUT_YOUR_TOKEN_HERE"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

// -------- WiFi --------
char ssid[] = "Redmi Note 13";        // عدّلها حسب شبكتك
char pass[] = "haitham2005";   // عدّلها حسب شبكتك

// -------- MQTT --------
const char* mqtt_server   = "10.22.255.183";   // IP اللابتوب اللي عليه Mosquitto
const int   mqtt_port     = 1883;

const char* TOPIC_TEMP        = "iot/project/temperature";
const char* TOPIC_SOUND       = "iot/project/sound";
const char* TOPIC_BUZZER_CMD  = "iot/project/buzzer/cmd";
const char* TOPIC_BUZZER_STAT = "iot/project/buzzer/state";
const char* TOPIC_BUZZER_MODE = "iot/project/buzzer/mode";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// -------- DHT --------
#define DHTPIN  25
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// -------- LEDs --------
#define GREEN_LED_PIN  26
#define YELLOW_LED_PIN 27

#define MANUAL_MODE 0
#define AUTO_MODE   1

// نبدأ في Auto mode وبالحالة الطبيعية (Yellow ON, Green OFF)
int  ledMode     = AUTO_MODE;
bool greenState  = false; // OFF
bool yellowState = true;  // ON

// -------- للصوت --------
int soundState     = 0;
int lastSoundState = 0;

// كول داون للصوت (5 ثواني بين كل trigger والتاني)
unsigned long lastSoundHandledMs = 0;
const unsigned long SOUND_COOLDOWN_MS = 5000; // 5 ثواني

// -------- بَزَر (على Node2) --------
int buzzerMode = 1;   // 1 = Auto by default, 0 = Manual
int buzzerCmd  = 0;   // 0/1 من V13 (يرسل لـ MQTT)

// -------- Blynk Timer --------
BlynkTimer timer;

// ======= HELPER: تحديث حالة الليدات + Blynk =======
void applyLedStates() {
  digitalWrite(GREEN_LED_PIN,  greenState  ? HIGH : LOW);
  digitalWrite(YELLOW_LED_PIN, yellowState ? HIGH : LOW);

  // نعكس الحالة على السويتشات في Blynk
  Blynk.virtualWrite(V3, greenState  ? 1 : 0);
  Blynk.virtualWrite(V4, yellowState ? 1 : 0);
}

// ======= إرسال الحرارة دورياً إلى Blynk + MQTT =======
void sendTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) {
    Serial.println("Failed to read from DHT!");
    return;
  }

  // إلى Blynk
  Blynk.virtualWrite(V0, t);

  // إلى MQTT
  if (mqttClient.connected()) {
    char payload[16];
    dtostrf(t, 4, 2, payload); // float → string
    mqttClient.publish(TOPIC_TEMP, payload);
  }

  Serial.print("Temp sent: ");
  Serial.println(t);
}

// ======= MQTT CALLBACK =======
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  int value = msg.toInt();

  // --- صوت من Node2 ---
  if (t == TOPIC_SOUND) {
    lastSoundState = soundState;
    soundState     = value;   // 0 or 1

    // نحدّث Blynk عادي عشان تشوف حالة الحساس
    Blynk.virtualWrite(V10, soundState);

    if (ledMode == AUTO_MODE) {
      // نشتغل فقط عند طلعة الصوت: 0 → 1
      if (lastSoundState == 0 && soundState == 1) {

        unsigned long now = millis();
        // شرط الانتظار 5 ثواني بين كل صوت وصوت
        if (now - lastSoundHandledMs >= SOUND_COOLDOWN_MS) {

          // toggle بين الليدين
          if (greenState) {
            greenState  = false;
            yellowState = true;
          } else if (yellowState) {
            greenState  = true;
            yellowState = false;
          } else {
            // لو صار أي لخبطة ورجعوا كلهم OFF نرجع للوضع الطبيعي
            greenState  = false;
            yellowState = true;
          }

          applyLedStates();
          lastSoundHandledMs = now;  // سجّل آخر مرة تعاملنا فيها مع صوت

        } else {
          Serial.println("Sound ignored (cooldown)");
        }
      }
    }
  }

  // --- حالة البازر من Node2 ---
  if (t == TOPIC_BUZZER_STAT) {
    int buzzerState = value ? 1 : 0;
    Blynk.virtualWrite(V11, buzzerState);
  }
}

// ======= إعادة اتصال MQTT =======
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32_Node1_";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe(TOPIC_SOUND);
      mqttClient.subscribe(TOPIC_BUZZER_STAT);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

// ======= BLYNK EVENTS =======

// يتنادى أول ما Node1 يتصل بـ Blynk
BLYNK_CONNECTED() {
  // نثبت القيم اللي بدنا إياها كبداية:
  ledMode     = AUTO_MODE;
  greenState  = false;
  yellowState = true;

  Blynk.virtualWrite(V5, 1);   // LED Mode = Auto
  applyLedStates();            // يحدّث V3/V4 + الهاردوير

  // نثبت مود البازر الافتراضي في V12 وننشره لـ Node2
  Blynk.virtualWrite(V12, buzzerMode);
  if (mqttClient.connected()) {
    String s = String(buzzerMode);
    mqttClient.publish(TOPIC_BUZZER_MODE, s.c_str(), true);
  }
}

// LED Mode: 0 = Manual, 1 = Auto (Sound Toggle)
BLYNK_WRITE(V5) {
  int newMode = param.asInt();   // 0 أو 1

  if (newMode == ledMode) return;  // ما تغير شيء

  int oldMode = ledMode;
  ledMode = newMode;

  Serial.print("LED Mode = ");
  Serial.println(ledMode == MANUAL_MODE ? "MANUAL" : "AUTO");

  if (oldMode == MANUAL_MODE && ledMode == AUTO_MODE) {
    // رجعنا من Manual → Auto
    // نرجع للحالة الطبيعية: أصفر ضاوي، أخضر مطفي
    greenState  = false;
    yellowState = true;
    applyLedStates();
    // من الآن فصاعدًا الصوت رح يعمل toggle من هالحالة
  }
  // من Auto → Manual:
  // ما نغيّر حالة الليدات، نخليها زي ما هي، واليوزر يتحكم من V3/V4
}

// Green LED Manual (فقط في Manual mode)
BLYNK_WRITE(V3) {
  if (ledMode == MANUAL_MODE) {
    greenState = param.asInt() ? true : false;
    applyLedStates();
  } else {
    // في Auto: منع تحريك السويتش → نرجّعه فورًا لحالة الهاردوير
    applyLedStates();
  }
}

// Yellow LED Manual (فقط في Manual mode)
BLYNK_WRITE(V4) {
  if (ledMode == MANUAL_MODE) {
    yellowState = param.asInt() ? true : false;
    applyLedStates();
  } else {
    // في Auto: منع تحريك السويتش → نرجّعه فورًا لحالة الهاردوير
    applyLedStates();
  }
}

// Buzzer Mode: 0 = Manual, 1 = Auto (by Temp on Node2)
BLYNK_WRITE(V12) {
  buzzerMode = param.asInt();
  Serial.print("Buzzer Mode = ");
  Serial.println(buzzerMode == 0 ? "MANUAL" : "AUTO");

  // نبعث المود لـ Node2 عبر MQTT
  if (mqttClient.connected()) {
    String s = String(buzzerMode);   // "0" أو "1"
    mqttClient.publish(TOPIC_BUZZER_MODE, s.c_str(), true); // retain
  }
}

// Buzzer Manual Command (V13) → نبعته إلى Node2 عبر MQTT
BLYNK_WRITE(V13) {
  buzzerCmd = param.asInt() ? 1 : 0;

  if (buzzerMode == 0) { // Manual فقط
    if (mqttClient.connected()) {
      String s = String(buzzerCmd);
      mqttClient.publish(TOPIC_BUZZER_CMD, s.c_str());
      Serial.print("Sent buzzer cmd: ");
      Serial.println(buzzerCmd);
    }
  } else {
    Serial.println("Buzzer in AUTO mode, manual cmd ignored.");
  }
}

// ======= SETUP =======
void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED_PIN,  OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);

  // الحالة الطبيعية عند البداية على الهاردوير
  digitalWrite(GREEN_LED_PIN,  LOW);
  digitalWrite(YELLOW_LED_PIN, HIGH);

  dht.begin();

  // اتصال Blynk + WiFi
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  Serial.print("WiFi connected, ESP32 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MQTT server: ");
  Serial.print(mqtt_server);
  Serial.print(":");
  Serial.println(mqtt_port);

  // إعداد MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // تايمر لقراءة الحرارة كل 2 ثانية
  timer.setInterval(2000L, sendTemperature);
}

// ======= LOOP =======
void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  Blynk.run();
  timer.run();
}
