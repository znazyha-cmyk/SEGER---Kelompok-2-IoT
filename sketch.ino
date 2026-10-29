/*
 * SEGER - Sistem Evaluasi Gas dan Environmental Room
 * Edge 1 (ESP32, simulasi Wokwi)
 *
 * Sensor 1: DHT22        -> suhu & kelembapan (GPIO 15)
 * Sensor 2: Potensiometer -> simulasi sensor CO2 / kualitas udara (GPIO 34)
 *
 * Data processing 1: moving average (filtering) di ESP32
 * Protokol cloud   : MQTT (broker publik HiveMQ)
 *
 * GANTI "kelompokmu" pada TOPIC_ID dengan nama unik kelompok kalian!
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"

// ================== KONFIGURASI ==================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;

// Ganti dengan nama unik kelompok
const char* TOPIC_ID     = "kelompokmu";
String TOPIC_DATA   = String("seger/") + TOPIC_ID + "/data";    // ESP32 -> HP
String TOPIC_STATUS = String("seger/") + TOPIC_ID + "/status";  // HP -> ESP32 (hasil klasifikasi)

const int PIN_DHT = 15;
const int PIN_GAS = 34;

const unsigned long PUBLISH_INTERVAL = 2000;  // ms
const int WINDOW = 10;                        // ukuran moving average
// =================================================

DHTesp dht;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Buffer moving average
float bufTemp[WINDOW], bufHum[WINDOW], bufCo2[WINDOW];
int bufIdx = 0;
int bufCount = 0;

unsigned long lastPublish = 0;

float average(float *arr, int n) {
  float sum = 0;
  for (int i = 0; i < n; i++) sum += arr[i];
  return sum / n;
}

// Potensiometer 0-4095 -> estimasi CO2 400-2000 ppm (simulasi)
float readCo2Simulated() {
  int adc = analogRead(PIN_GAS);
  return 400.0 + (adc / 4095.0) * 1600.0;
}

void connectWiFi() {
  Serial.print("Menghubungkan WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);  // channel 6 untuk Wokwi-GUEST
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" terhubung!");
}

// Menerima hasil klasifikasi dari HP (edge 2)
void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.print("[STATUS dari HP] ");
  Serial.println(msg);
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "seger-esp32-" + String(random(0xffff), HEX);
    Serial.print("Menghubungkan MQTT...");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" terhubung!");
      mqtt.subscribe(TOPIC_STATUS.c_str());
    } else {
      Serial.print(" gagal, rc=");
      Serial.print(mqtt.state());
      Serial.println(" coba lagi 3 detik");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.setup(PIN_DHT, DHTesp::DHT22);
  pinMode(PIN_GAS, INPUT);

  connectWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(onMessage);

  Serial.println("SEGER Edge 1 siap.");
  Serial.print("Topic data  : "); Serial.println(TOPIC_DATA);
  Serial.print("Topic status: "); Serial.println(TOPIC_STATUS);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPublish < PUBLISH_INTERVAL) return;
  lastPublish = now;

  // 1. Baca sensor
  TempAndHumidity th = dht.getTempAndHumidity();
  if (isnan(th.temperature) || isnan(th.humidity)) {
    Serial.println("Gagal baca DHT22");
    return;
  }
  float co2 = readCo2Simulated();

  // 2. Data processing 1: moving average
  bufTemp[bufIdx] = th.temperature;
  bufHum[bufIdx]  = th.humidity;
  bufCo2[bufIdx]  = co2;
  bufIdx = (bufIdx + 1) % WINDOW;
  if (bufCount < WINDOW) bufCount++;

  float tempAvg = average(bufTemp, bufCount);
  float humAvg  = average(bufHum, bufCount);
  float co2Avg  = average(bufCo2, bufCount);

  // 3. Kirim JSON ke MQTT
  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%.0f,\"temp_raw\":%.1f,\"hum_raw\":%.1f,\"co2_raw\":%.0f}",
           tempAvg, humAvg, co2Avg, th.temperature, th.humidity, co2);

  mqtt.publish(TOPIC_DATA.c_str(), payload);
  Serial.print("[PUBLISH] ");
  Serial.println(payload);
}
