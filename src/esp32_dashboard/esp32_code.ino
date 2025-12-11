/*
  ESP32 OBD-II Data Logger - Telemetria Automotiva
  Envia dados da ECU para servidor Flask via HTTP POST
  
  Dependências necessárias (instalar via Library Manager):
  - ArduinoJson (versão 6 ou superior)
  - WiFiManager (opcional, para configuração Wi-Fi fácil)
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// ===== CONFIGURAÇÕES =====
// Modifique conforme sua rede
const char* ssid = "Marcola";
const char* password = "abc12345";

// Configuração do servidor
const char* serverUrl = "http://192.168.XXX.X:5000/data";
const char* deviceId = "Carro 1"; // ID único para este veículo

// ===== SIMULAÇÃO DE DADOS OBD-II (substitua por leituras reais) =====
// Valores simulados - no projeto real, substitua por:
// - Leitura CAN bus via MCP2515
// - OBD-II via ELM327
// - Comunicação direta com ECU

struct ECUData {
  int rpm;           // RPM do motor (0-8000)
  float speed;       // Velocidade (km/h)
  float engineTemp;  // Temperatura do motor (°C)
  float throttlePos; // Posição do acelerador (%)
  float voltage;     // Tensão da bateria (V)
  int gear;          // Marcha atual (0-6, 0=N)
  float fuelLevel;   // Nível de combustível (%)
};

ECUData currentData;

// ===== VARIÁVEIS GLOBAIS =====
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 1000; // Envia a cada 1 segundo
int failedAttempts = 0;
const int maxFailedAttempts = 5;

// ===== PROTÓTIPOS DE FUNÇÃO =====
void connectToWiFi();
void simulateECUData(); // Substituir por readRealECUData()
String createJsonPayload(const ECUData& data);
bool sendDataToServer(const String& payload);
void deepSleepOnFailure();

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 OBD-II Data Logger ===");
  Serial.println("Inicializando...");
  
  // Inicializar comunicação com ECU (exemplo para projeto real)
  // Serial2.begin(115200, SERIAL_8N1, 16, 17); // Para comunicação com MCP2515
  
  // Conectar ao Wi-Fi
  connectToWiFi();
}

// ===== LOOP PRINCIPAL =====
void loop() {
  
  // Atualizar dados da ECU
  simulateECUData(); // SUBSTITUIR POR: readRealECUData();
  
  // Enviar dados no intervalo configurado
  unsigned long currentTime = millis();
  if (currentTime - lastSendTime >= sendInterval) {
    
    // Criar payload JSON
    String payload = createJsonPayload(currentData);
    Serial.print("Payload: ");
    Serial.println(payload);
    
    // Enviar para servidor
    if (sendDataToServer(payload)) {
      Serial.println("Dados enviados com sucesso!");
      failedAttempts = 0; // Resetar contador de falhas
    } else {
      failedAttempts++;
      Serial.print("Falha no envio. Tentativa ");
      Serial.println(failedAttempts);
      
      if (failedAttempts >= maxFailedAttempts) {
        Serial.println("Muitas falhas. Reiniciando...");
        deepSleepOnFailure();
      }
    }
    
    lastSendTime = currentTime;
  }
  
  // Pequena pausa para não sobrecarregar
  delay(10);
}

// ===== IMPLEMENTAÇÕES =====

void connectToWiFi() {
  Serial.print("Conectando a ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("\nFalha na conexão Wi-Fi!");
    // Para produção, considere usar WiFiManager para AP de configuração
  }
}

void simulateECUData() {
  // Simulação de dados - SUBSTITUIR POR LEITURA REAL DA ECU
  
  // RPM: 800-6500 com variação suave
  static float rpmOffset = 0;
  rpmOffset += 0.01;
  currentData.rpm = 1500 + sin(rpmOffset) * 500 + random(-50, 50);
  currentData.rpm = constrain(currentData.rpm, 800, 6500);
  
  // Velocidade: 0-180 km/h
  static float speedOffset = 0;
  speedOffset += 0.005;
  currentData.speed = 60 + sin(speedOffset * 0.5) * 20 + random(-2, 2);
  currentData.speed = constrain(currentData.speed, 0, 180);
  
  // Temperatura do motor: 70-110°C
  currentData.engineTemp = 85 + sin(millis() / 60000.0) * 5;
  
  // Posição do acelerador: 0-100%
  currentData.throttlePos = 30 + sin(millis() / 30000.0) * 25;
  currentData.throttlePos = constrain(currentData.throttlePos, 0, 100);
  
  // Tensão da bateria: 12.0-14.5V
  currentData.voltage = 13.2 + random(-10, 10) * 0.01;
  
  // Marcha simulada baseada na velocidade
  if (currentData.speed < 20) currentData.gear = 1;
  else if (currentData.speed < 40) currentData.gear = 2;
  else if (currentData.speed < 60) currentData.gear = 3;
  else if (currentData.speed < 80) currentData.gear = 4;
  else if (currentData.speed < 100) currentData.gear = 5;
  else currentData.gear = 6;
  
  // Nível de combustível: 0-100%
  static float fuel = 80.0;
  fuel -= 0.001; // Consumo simulado
  currentData.fuelLevel = constrain(fuel, 0, 100);
}

String createJsonPayload(const ECUData& data) {
  StaticJsonDocument<512> doc;
  
  // Campos obrigatórios para o servidor
  doc["device_id"] = deviceId;
  doc["rpm"] = data.rpm;
  doc["speed"] = round(data.speed * 10) / 10.0; // 1 casa decimal
  doc["temp_motor"] = round(data.engineTemp * 10) / 10.0;
  doc["throttle_pos"] = round(data.throttlePos * 10) / 10.0;
  
  // Campos adicionais (opcionais)
  doc["voltage"] = round(data.voltage * 100) / 100.0; // 2 casas decimais
  doc["gear"] = data.gear;
  doc["fuel_level"] = round(data.fuelLevel * 10) / 10.0;
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  return payload;
}

bool sendDataToServer(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado. Reconectando...");
    connectToWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      return false;
    }
  }
  
  HTTPClient http;
  WiFiClient client;
  
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000); // Timeout de 5 segundos
  
  int httpCode = http.POST(payload);
  String response = http.getString();
  
  bool success = (httpCode == 200);
  
  if (!success) {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    Serial.print("Response: ");
    Serial.println(response);
  }
  
  http.end();
  return success;
}

void deepSleepOnFailure() {
  Serial.println("Entrando em deep sleep por 60 segundos...");
  esp_deep_sleep(60 * 1000000); // 60 segundos
}
