#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <string.h>

// 1. KONFIGURASI WIFI WOKWI
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// 2. KONFIGURASI HIVEMQ PUBLIC BROKER
const char* MQTT_HOST = "broker.hivemq.com";
const int MQTT_PORT = 1883;

// Gunakan ID unik tim, dan samakan dengan dashboard Python.
const char* MQTT_CLIENT_ID = "esp32-sic8-navya-ah2zi6";
const char* MQTT_TOPIC = "sic8/sic8-navya-ah2zi6/gasmonitor/data";

// 3. PIN SENSOR DAN OUTPUT
#define DHTPIN 4
#define DHTTYPE DHT22

const int MQ2_AO_PIN = 34;
const int MQ2_DO_PIN = 27;

const int LED_GREEN_PIN = 18;
const int LED_YELLOW_PIN = 19;
const int LED_RED_PIN = 23;
const int BUZZER_PIN = 26;

// 4. AMBANG BATAS
const float TEMP_WARNING_C = 35.0;
const float TEMP_DANGER_C = 45.0;

const int GAS_WARNING_ADC = 1800;
const int GAS_DANGER_ADC = 2600;

// 5. OBJEK PERANGKAT
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

WiFiClient netClient;
PubSubClient mqttClient(netClient);

enum SystemState {
  SAFE_STATE,
  WARNING_STATE,
  DANGER_STATE,
  SENSOR_ERROR_STATE
};

SystemState state = SAFE_STATE;

// 6. VARIABEL SISTEM
unsigned long lastReadMs = 0;
unsigned long lastLcdMs = 0;
unsigned long lastBuzzerMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastWifiRetryMs = 0;

const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long LCD_INTERVAL_MS = 500;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long MQTT_PUBLISH_INTERVAL_MS = 2000;
const unsigned long WIFI_RETRY_MS = 10000;

bool buzzerPhase = false;

int gasAdc = 0;
int gasDigital = HIGH;

float temperatureC = NAN;
float humidity = NAN;

// 7. LED
void allLedsOff() {
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_YELLOW_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);
}

void setStateOutputs() {
  allLedsOff();
  digitalWrite(BUZZER_PIN, LOW);

  switch (state) {
    case SAFE_STATE:
      digitalWrite(LED_GREEN_PIN, HIGH);
      break;

    case WARNING_STATE:
      digitalWrite(LED_YELLOW_PIN, HIGH);
      break;

    case DANGER_STATE:
      digitalWrite(LED_RED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
      break;

    case SENSOR_ERROR_STATE:
      digitalWrite(LED_RED_PIN, HIGH);
      break;
  }
}

// 8. LABEL STATUS
const char* stateLabel() {
  switch (state) {
    case SAFE_STATE: return "SAFE";
    case WARNING_STATE: return "WARNING";
    case DANGER_STATE: return "DANGER";
    default: return "SENSOR ERROR";
  }
}

// 9. LCD
void updateLcd() {
  char line1[17];
  char line2[17];

  if (isnan(temperatureC)) {
    snprintf(line1, sizeof(line1), "Temp: --.- C");
  } else {
    snprintf(line1, sizeof(line1), "Temp:%5.1f C", temperatureC);
  }

  const char* shortStatus = "SAFE";

  if (state == WARNING_STATE) {
    shortStatus = "WARNING";
  } else if (state == DANGER_STATE) {
    shortStatus = "DANGER";
  } else if (state == SENSOR_ERROR_STATE) {
    shortStatus = "ERROR";
  }

  snprintf(line2, sizeof(line2), "Gas:%4d %-7s", gasAdc, shortStatus);

  for (int i = strlen(line1); i < 16; i++) {
    line1[i] = ' ';
  }
  line1[16] = '\0';

  for (int i = strlen(line2); i < 16; i++) {
    line2[i] = ' ';
  }
  line2[16] = '\0';

  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// 10. WIFI
void connectWiFi() {
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi terhubung!");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi belum tersambung, lanjut tanpa jaringan.");
  }
}

// 11. MQTT PUBLIC
void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  Serial.print("Menghubungkan ke HiveMQ Public Broker... ");

  // Broker publik tidak memakai username/password.
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println("berhasil!");
    Serial.print("Topic publish: ");
    Serial.println(MQTT_TOPIC);
  } else {
    Serial.print("gagal, kode: ");
    Serial.println(mqttClient.state());
  }
}

// 12. BACA SENSOR DAN TENTUKAN STATUS
void readSensors() {
  temperatureC = dht.readTemperature();
  humidity = dht.readHumidity();

  gasAdc = analogRead(MQ2_AO_PIN);
  gasDigital = digitalRead(MQ2_DO_PIN);

  if (gasAdc >= GAS_DANGER_ADC ||
      (!isnan(temperatureC) && temperatureC >= TEMP_DANGER_C)) {
    state = DANGER_STATE;
  } else if (isnan(temperatureC) || isnan(humidity)) {
    state = SENSOR_ERROR_STATE;
  } else if (gasAdc >= GAS_WARNING_ADC ||
             temperatureC >= TEMP_WARNING_C ||
             gasDigital == LOW) {
    state = WARNING_STATE;
  } else {
    state = SAFE_STATE;
  }

  setStateOutputs();

  Serial.printf(
    "Suhu: %.1f C | Kelembapan: %.1f %% | "
    "Gas ADC: %d | Gas DO: %d | Status: %s\n",
    temperatureC,
    humidity,
    gasAdc,
    gasDigital,
    stateLabel()
  );
}

// 13. KIRIM DATA KE HIVEMQ PUBLIC
void publishSensorData() {
  if (!mqttClient.connected()) {
    return;
  }

  char tempText[16];
  char humidityText[16];
  char payload[220];

  if (isnan(temperatureC)) {
    snprintf(tempText, sizeof(tempText), "null");
  } else {
    snprintf(tempText, sizeof(tempText), "%.1f", temperatureC);
  }

  if (isnan(humidity)) {
    snprintf(humidityText, sizeof(humidityText), "null");
  } else {
    snprintf(humidityText, sizeof(humidityText), "%.1f", humidity);
  }

  snprintf(
    payload,
    sizeof(payload),
    "{\"temperature_c\":%s,"
    "\"humidity_pct\":%s,"
    "\"gas_adc\":%d,"
    "\"gas_do\":%d,"
    "\"status\":\"%s\"}",
    tempText,
    humidityText,
    gasAdc,
    gasDigital,
    stateLabel()
  );

  if (mqttClient.publish(MQTT_TOPIC, payload)) {
    Serial.print("MQTT terkirim: ");
    Serial.println(payload);
  } else {
    Serial.println("Gagal mengirim data MQTT.");
  }
}

// 14. SETUP
void setup() {
  Serial.begin(115200);

  pinMode(MQ2_AO_PIN, INPUT);
  pinMode(MQ2_DO_PIN, INPUT);

  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  allLedsOff();

  dht.begin();

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("NAVYA SATYA IOT");
  lcd.setCursor(0, 1);
  lcd.print("PROJECT");
  delay(1000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Heat Gas Sensor");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi");

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(256);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(2);

  connectWiFi();
}

// 15. LOOP
void loop() {
  unsigned long now = millis();

  // Coba sambungkan kembali Wi-Fi setiap 10 detik.
  if (WiFi.status() != WL_CONNECTED && now - lastWifiRetryMs >= WIFI_RETRY_MS) {
    lastWifiRetryMs = now;
    Serial.println("Mencoba menghubungkan ulang WiFi...");
    WiFi.reconnect();
  }

  // Coba sambungkan MQTT setiap 5 detik.
  if (!mqttClient.connected() &&
      WiFi.status() == WL_CONNECTED &&
      now - lastMqttAttemptMs >= MQTT_RETRY_MS) {
    lastMqttAttemptMs = now;
    connectMQTT();
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
  }

  // Baca sensor setiap 2 detik.
  if (now - lastReadMs >= SENSOR_INTERVAL_MS || lastReadMs == 0) {
    lastReadMs = now;
    readSensors();
  }

  // Kirim payload setiap 2 detik.
  if (mqttClient.connected() &&
      now - lastPublishMs >= MQTT_PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    publishSensorData();
  }

  // Buzzer berkala saat WARNING.
  if (state == WARNING_STATE) {
    if (now - lastBuzzerMs >= 300) {
      lastBuzzerMs = now;
      buzzerPhase = !buzzerPhase;
      digitalWrite(BUZZER_PIN, buzzerPhase ? HIGH : LOW);
    }
  }
  // Buzzer dan LED merah berkedip saat SENSOR ERROR.
  else if (state == SENSOR_ERROR_STATE) {
    if (now - lastBuzzerMs >= 700) {
      lastBuzzerMs = now;
      buzzerPhase = !buzzerPhase;
      digitalWrite(BUZZER_PIN, buzzerPhase ? HIGH : LOW);
      digitalWrite(LED_RED_PIN, buzzerPhase ? HIGH : LOW);
    }
  } else if (state != DANGER_STATE) {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // Perbarui LCD setiap 500 ms.
  if (now - lastLcdMs >= LCD_INTERVAL_MS || lastLcdMs == 0) {
    lastLcdMs = now;
    updateLcd();
  }
}
