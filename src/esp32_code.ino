#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include "ELMduino.h"

// ===== WIFI =====
const char* ssid = "Marcola";
const char* password = "abc12345";

// ===== SERVIDOR =====
const char* serverUrl = "http://192.168.137.1:5000/data";
const char* deviceId = "Carro 1";

// ===== BLUETOOTH ELM327 =====
BluetoothSerial SerialBT;
ELM327 myELM327;

const char remotePin[] = "1234";
uint8_t deviceMAC[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xBA};

// ===== OBD STATES =====
enum obd_pid_states {
  ENG_RPM,
  SPEED,
  THROTTLE,
  BATTERY_VOLTAGE,
  FUEL_LEVEL,
  ENG_COOLANT_TEMP
};

obd_pid_states obd_state = ENG_RPM;
bool obdCycleCompleted = false;

// ===== ECU DATA (APENAS DADOS OBD) =====
struct ECUData {
  int rpm;
  float speed;
  float throttlePos;
  float voltage;
  float fuelLevel;
  float engineTemp;
};

ECUData currentData;

// ===== CONTROLE DE ENVIO =====
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 1000;

// ===== FUNÇÃO GENÉRICA OBD =====
inline void handleResult(float value, obd_pid_states next) {
  if (myELM327.nb_rx_state == ELM_SUCCESS) {

    switch (obd_state) {

      case ENG_RPM:
        currentData.rpm = (int)value;
        Serial.print("[OBD] RPM: ");
        Serial.println(currentData.rpm);
        break;

      case SPEED:
        currentData.speed = value;
        Serial.print("[OBD] Speed (km/h): ");
        Serial.println(currentData.speed);
        break;

      case THROTTLE:
        currentData.throttlePos = value;
        Serial.print("[OBD] Throttle (%): ");
        Serial.println(currentData.throttlePos);
        break;

      case BATTERY_VOLTAGE:
        currentData.voltage = value;
        Serial.print("[OBD] Battery (V): ");
        Serial.println(currentData.voltage);
        break;

      case FUEL_LEVEL:
        currentData.fuelLevel = value;
        Serial.print("[OBD] Fuel Level (%): ");
        Serial.println(currentData.fuelLevel);
        break;

      case ENG_COOLANT_TEMP:
        currentData.engineTemp = value;
        Serial.print("[OBD] Coolant Temp (°C): ");
        Serial.println(currentData.engineTemp);

        obdCycleCompleted = true;
        Serial.println("✅ [OBD] Ciclo completo de leitura!");
        Serial.println("--------------------------------");
        break;
    }

    obd_state = next;
  }
  else if (myELM327.nb_rx_state != ELM_GETTING_MSG) {
    Serial.print("❌ [OBD ERROR] ");
    myELM327.printError();
    delay(50);
    // obd_state = next;
  }
}


// ===== WIFI =====
void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {Serial.println("✅ Connected to WiFi");}
  else {Serial.println("❌ Connection to WiFi not established");}
}

// ===== JSON =====
String createJsonPayload(const ECUData& data) {
  StaticJsonDocument<512> doc;

  doc["device_id"] = deviceId;
  doc["rpm"] = data.rpm;
  doc["speed"] = data.speed;
  doc["throttle_pos"] = data.throttlePos;
  doc["voltage"] = data.voltage;
  doc["fuel_level"] = data.fuelLevel;
  doc["temp_motor"] = data.engineTemp;
  doc["timestamp"] = millis();

  String payload;
  serializeJson(doc, payload);
  return payload;
}

// ===== HTTP =====
bool sendDataToServer(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  HTTPClient http;
  WiFiClient client;

  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(payload);
  http.end();

  return httpCode == 200;
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  connectToWiFi();

  SerialBT.begin("ESP32_Master", true);
  esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, (uint8_t*)remotePin);
}

// ===== LOOP =====
void loop() {

  if (!SerialBT.connected()) {
    Serial.println("Trying to connect to ELM327 via BT...");
    if (SerialBT.connect(deviceMAC)) {
      Serial.println("✅ ELM327 connected - Phase 1");
      if (!myELM327.begin(SerialBT, false, 2000)) {
        Serial.println("❌ Couldn't connect to OBD scanner - Phase 2");
        while (1);
      }
    }
    delay(3000);
    return;
  }

  // ===== MÁQUINA DE ESTADOS OBD =====
  switch (obd_state) {
    case ENG_RPM:
      handleResult(myELM327.rpm(), SPEED);
      break;

    case SPEED:
      handleResult(myELM327.kph(), THROTTLE);
      break;

    case THROTTLE:
      handleResult(myELM327.throttle(), BATTERY_VOLTAGE);
      break;

    case BATTERY_VOLTAGE:
      handleResult(myELM327.batteryVoltage(), FUEL_LEVEL);
      break;

    case FUEL_LEVEL:
      handleResult(myELM327.fuelLevel(), ENG_COOLANT_TEMP);
      break;

    case ENG_COOLANT_TEMP:
      handleResult(myELM327.engineCoolantTemp(), ENG_RPM);
      break;
  }

  // ===== ENVIO APÓS CICLO COMPLETO =====
  if (obdCycleCompleted && millis() - lastSendTime >= sendInterval) {
    String payload = createJsonPayload(currentData);
    sendDataToServer(payload);

    lastSendTime = millis();
    obdCycleCompleted = false; // aguarda próximo ciclo completo
  }
}
