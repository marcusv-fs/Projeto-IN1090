/*
  ESP32 OBD-II Data Logger com ELM327 - Telemetria Automotiva
  Envia dados reais da ECU para servidor Flask via HTTP POST
  
  Dependências necessárias (instalar via Library Manager):
  - ArduinoJson (versão 6 ou superior)
  - ELMduino (https://github.com/PowerBroker2/ELMduino)
  - BluetoothSerial (já incluída no ESP32)
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <ELMduino.h>

// ===== CONFIGURAÇÕES DE REDE =====
const char* ssid = "Marcola";
const char* password = "abc12345";

// Configuração do servidor
const char* serverUrl = "http://192.168.137.1:5000/data";
const char* deviceId = "Carro 1"; // ID único para este veículo

// ===== CONFIGURAÇÕES BLUETOOTH ELM327 =====
BluetoothSerial SerialBT;
ELM327 myELM327;

// MAC Address do seu scanner ELM327 (opcional - pode conectar por nome também)
// uint8_t elm327Mac[] = {0x00, 0x1D, 0xA5, 0x00, 0x00, 0x00}; // Substitua pelo seu

// Nome do dispositivo Bluetooth do ELM327 (geralmente começa com "OBDII" ou "ELM327")
const char* elm327DeviceName = "OBDII"; // Altere para o nome do seu scanner

// PIN do ELM327 (se necessário, geralmente 1234 ou 0000)
const char* elm327Pin = "1234";

// ===== ESTRUTURA DE DADOS DA ECU =====
struct ECUData {
  int rpm;               // RPM do motor
  float speed;           // Velocidade (km/h)
  float engineTemp;      // Temperatura do motor (°C)
  float coolantTemp;     // Temperatura do líquido de arrefecimento (°C)
  float intakeTemp;      // Temperatura do ar de admissão (°C)
  float throttlePos;     // Posição do acelerador (%)
  float engineLoad;      // Carga do motor (%)
  float mafRate;         // Taxa do sensor MAF (g/s)
  float fuelLevel;       // Nível de combustível (%)
  float voltage;         // Tensão da bateria (V)
  float fuelPressure;    // Pressão do combustível (kPa)
  float timingAdvance;   // Avanço da ignição (°)
  float fuelRate;        // Taxa de consumo de combustível (L/h)
  int gear;              // Marcha atual (calculada)
  bool dataValid;        // Flag indicando se os dados são válidos
};

ECUData currentData;

// ===== VARIÁVEIS GLOBAIS =====
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000; // Envia a cada 2 segundos
unsigned long lastOBDReadTime = 0;
const unsigned long obdReadInterval = 500; // Lê OBD a cada 500ms
int failedAttempts = 0;
const int maxFailedAttempts = 5;
bool elmConnected = false;
bool wifiConnected = false;

// ===== PID SUPPORTED FLAGS =====
bool pidSupported[256]; // Para verificar quais PIDs são suportados
bool pidInitialized = false;

// ===== PROTÓTIPOS DE FUNÇÃO =====
void connectToWiFi();
void connectToELM327();
void readOBDData();
void requestSupportedPIDs();
bool isPIDSupported(uint8_t pid);
String createJsonPayload(const ECUData& data);
bool sendDataToServer(const String& payload);
void deepSleepOnFailure();
float calculateGear(float speed, float rpm);
void printECUData();

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 OBD-II Data Logger com ELM327 ===");
  Serial.println("Inicializando...");
  
  // Inicializar estrutura de dados
  memset(&currentData, 0, sizeof(currentData));
  currentData.dataValid = false;
  
  // Conectar ao Wi-Fi
  connectToWiFi();
  
  // Conectar ao ELM327
  connectToELM327();
  
  Serial.println("Sistema inicializado e pronto!");
}

// ===== LOOP PRINCIPAL =====
void loop() {
  // Manter conexão Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    Serial.println("Wi-Fi desconectado. Reconectando...");
    connectToWiFi();
  }
  
  // Manter conexão ELM327
  if (!elmConnected) {
    Serial.println("ELM327 desconectado. Tentando reconectar...");
    connectToELM327();
  }
  
  // Ler dados OBD no intervalo configurado
  unsigned long currentTime = millis();
  if (currentTime - lastOBDReadTime >= obdReadInterval) {
    if (elmConnected) {
      readOBDData();
      if (currentData.dataValid) {
        printECUData(); // Exibir dados no Serial Monitor
      }
    }
    lastOBDReadTime = currentTime;
  }
  
  // Enviar dados no intervalo configurado
  if (currentTime - lastSendTime >= sendInterval) {
    if (currentData.dataValid && wifiConnected) {
      // Criar payload JSON
      String payload = createJsonPayload(currentData);
      Serial.print("Enviando dados... ");
      
      // Enviar para servidor
      if (sendDataToServer(payload)) {
        Serial.println("OK!");
        failedAttempts = 0;
      } else {
        failedAttempts++;
        Serial.print("Falha. Tentativa ");
        Serial.println(failedAttempts);
        
        if (failedAttempts >= maxFailedAttempts) {
          Serial.println("Muitas falhas. Reiniciando...");
          deepSleepOnFailure();
        }
      }
    } else {
      if (!currentData.dataValid) {
        Serial.println("Dados OBD inválidos - não enviando");
      }
      if (!wifiConnected) {
        Serial.println("Wi-Fi não conectado - não enviando");
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
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { // Aumentado para 30 tentativas
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    wifiConnected = false;
    Serial.println("\nFalha na conexão Wi-Fi!");
  }
}

void connectToELM327() {
  Serial.println("Iniciando Bluetooth...");
  
  if (!SerialBT.begin("ESP32-OBD-Logger", true)) {
    Serial.println("Erro ao iniciar Bluetooth!");
    elmConnected = false;
    return;
  }
  
  Serial.print("Conectando ao ELM327: ");
  Serial.println(elm327DeviceName);
  
  // Tenta conectar pelo nome do dispositivo
  bool connected = SerialBT.connect(elm327DeviceName);
  
  // Alternativa: conectar por MAC address (descomente se necessário)
  // bool connected = SerialBT.connect(elm327Mac);
  
  if (!connected) {
    Serial.println("Falha na conexão Bluetooth!");
    elmConnected = false;
    
    // Listar dispositivos Bluetooth próximos para debug
    Serial.println("Procurando dispositivos Bluetooth...");
    // Nota: A busca completa requer mais código
    return;
  }
  
  Serial.println("Conectado ao ELM327! Inicializando...");
  
  // Aguardar estabilização da conexão
  delay(1000);
  
  // Inicializar ELMduino
  myELM327.begin(SerialBT, true, 2000); // true = debug mode
  
  // Testar comunicação
  if (!myELM327.getResponse()) {
    Serial.println("Falha na comunicação com ELM327!");
    elmConnected = false;
    return;
  }
  
  elmConnected = true;
  
  // Solicitar PIDs suportados
  requestSupportedPIDs();
  
  Serial.println("ELM327 inicializado com sucesso!");
  
  // Mostrar informações do scanner
  Serial.print("Protocolo: ");
  Serial.println(myELM327.getProtocol());
}

void requestSupportedPIDs() {
  Serial.println("Verificando PIDs suportados...");
  
  // Inicializar array de PIDs suportados
  memset(pidSupported, 0, sizeof(pidSupported));
  
  // Solicitar PIDs suportados do modo 01
  // PID 0x00 - PIDs suportados 01-20
  if (myELM327.supportedPIDs_01_20()) {
    uint32_t supported = myELM327.supportedPIDs_1;
    for (int i = 1; i <= 32; i++) {
      pidSupported[i] = (supported >> (32 - i)) & 0x01;
    }
  }
  
  // PID 0x20 - PIDs suportados 21-40
  if (myELM327.supportedPIDs_21_40()) {
    uint32_t supported = myELM327.supportedPIDs_2;
    for (int i = 33; i <= 64; i++) {
      pidSupported[i] = (supported >> (64 - i)) & 0x01;
    }
  }
  
  // PID 0x40 - PIDs suportados 41-60
  if (myELM327.supportedPIDs_41_60()) {
    uint32_t supported = myELM327.supportedPIDs_3;
    for (int i = 65; i <= 96; i++) {
      pidSupported[i] = (supported >> (96 - i)) & 0x01;
    }
  }
  
  pidInitialized = true;
  
  // Mostrar PIDs suportados
  Serial.println("PIDs suportados:");
  for (int i = 1; i <= 96; i++) {
    if (pidSupported[i]) {
      Serial.printf("PID 0x%02X suportado\n", i);
    }
  }
}

bool isPIDSupported(uint8_t pid) {
  if (!pidInitialized) return false;
  if (pid == 0) return true; // PID 0x00 sempre suportado para verificação
  return pidSupported[pid];
}

void readOBDData() {
  bool dataUpdated = false;
  
  // Tentar ler RPM (PID 0x0C)
  if (isPIDSupported(0x0C)) {
    float tempRPM = myELM327.rpm();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.rpm = (int)tempRPM;
      dataUpdated = true;
    }
  }
  
  // Tentar ler velocidade (PID 0x0D)
  if (isPIDSupported(0x0D)) {
    float tempSpeed = myELM327.kph();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.speed = tempSpeed;
      dataUpdated = true;
    }
  }
  
  // Tentar ler temperatura do motor (PID 0x05)
  if (isPIDSupported(0x05)) {
    float temp = myELM327.engineCoolantTemp();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.engineTemp = temp;
      currentData.coolantTemp = temp;
      dataUpdated = true;
    }
  }
  
  // Tentar ler posição do acelerador (PID 0x11)
  if (isPIDSupported(0x11)) {
    float throttle = myELM327.throttle();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.throttlePos = throttle;
      dataUpdated = true;
    }
  }
  
  // Tentar ler tensão da bateria (PID 0x42)
  // Nota: Este PID não é padrão OBD-II, mas muitos ELM327 suportam
  {
    float voltage = myELM327.obdVoltage();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.voltage = voltage;
      dataUpdated = true;
    }
  }
  
  // Tentar ler carga do motor (PID 0x04)
  if (isPIDSupported(0x04)) {
    float load = myELM327.engineLoad();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.engineLoad = load;
      dataUpdated = true;
    }
  }
  
  // Tentar ler temperatura do ar de admissão (PID 0x0F)
  if (isPIDSupported(0x0F)) {
    float temp = myELM327.intakeAirTemp();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.intakeTemp = temp;
      dataUpdated = true;
    }
  }
  
  // Tentar ler taxa MAF (PID 0x10)
  if (isPIDSupported(0x10)) {
    float maf = myELM327.mafRate();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.mafRate = maf;
      dataUpdated = true;
    }
  }
  
  // Tentar ler nível de combustível (PID 0x2F)
  if (isPIDSupported(0x2F)) {
    float fuel = myELM327.fuelLevel();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.fuelLevel = fuel;
      dataUpdated = true;
    }
  }
  
  // Tentar ler pressão do combustível (PID 0x0A)
  if (isPIDSupported(0x0A)) {
    float pressure = myELM327.fuelPressure();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.fuelPressure = pressure;
      dataUpdated = true;
    }
  }
  
  // Tentar ler avanço da ignição (PID 0x0E)
  if (isPIDSupported(0x0E)) {
    float timing = myELM327.timingAdvance();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      currentData.timingAdvance = timing;
      dataUpdated = true;
    }
  }
  
  // Calcular marcha se tiver RPM e velocidade
  if (currentData.rpm > 0 && currentData.speed > 0) {
    currentData.gear = (int)calculateGear(currentData.speed, currentData.rpm);
    dataUpdated = true;
  }
  
  // Calcular taxa de consumo de combustível aproximada
  if (currentData.mafRate > 0) {
    // Fórmula simplificada: consumo (L/h) ≈ MAF (g/s) / 11.2
    currentData.fuelRate = currentData.mafRate / 11.2;
    dataUpdated = true;
  }
  
  currentData.dataValid = dataUpdated;
}

float calculateGear(float speed, float rpm) {
  // Cálculo simplificado da marcha
  // Baseado na relação entre velocidade e RPM
  // Valores típicos para carros de passeio:
  // 1ª: ~15 km/h a 3000 RPM
  // 2ª: ~30 km/h a 3000 RPM
  // 3ª: ~45 km/h a 3000 RPM
  // 4ª: ~60 km/h a 3000 RPM
  // 5ª: ~75 km/h a 3000 RPM
  
  if (rpm < 500 || speed < 5) return 0; // Neutro ou parado
  
  float ratio = speed / (rpm / 1000.0); // km/h por 1000 RPM
  
  if (ratio < 8) return 1;       // 1ª marcha
  else if (ratio < 14) return 2; // 2ª marcha
  else if (ratio < 20) return 3; // 3ª marcha
  else if (ratio < 26) return 4; // 4ª marcha
  else if (ratio < 32) return 5; // 5ª marcha
  else return 6;                 // 6ª marcha ou acima
}

void printECUData() {
  Serial.println("\n=== DADOS OBD-II ===");
  Serial.printf("RPM: %d\n", currentData.rpm);
  Serial.printf("Velocidade: %.1f km/h\n", currentData.speed);
  Serial.printf("Temp. Motor: %.1f °C\n", currentData.engineTemp);
  Serial.printf("Pos. Acelerador: %.1f %%\n", currentData.throttlePos);
  Serial.printf("Tensão Bateria: %.2f V\n", currentData.voltage);
  Serial.printf("Carga Motor: %.1f %%\n", currentData.engineLoad);
  Serial.printf("Nível Combustível: %.1f %%\n", currentData.fuelLevel);
  Serial.printf("Marcha: %d\n", currentData.gear);
  Serial.printf("Taxa MAF: %.2f g/s\n", currentData.mafRate);
  Serial.printf("Consumo: %.2f L/h\n", currentData.fuelRate);
  Serial.println("==================\n");
}

String createJsonPayload(const ECUData& data) {
  StaticJsonDocument<1024> doc; // Aumentado para 1024 bytes para mais dados
  
  // Campos obrigatórios para o servidor
  doc["device_id"] = deviceId;
  doc["rpm"] = data.rpm;
  doc["speed"] = round(data.speed * 10) / 10.0; // 1 casa decimal
  doc["temp_motor"] = round(data.engineTemp * 10) / 10.0;
  doc["throttle_pos"] = round(data.throttlePos * 10) / 10.0;
  
  // Campos adicionais
  doc["voltage"] = round(data.voltage * 100) / 100.0; // 2 casas decimais
  doc["gear"] = data.gear;
  doc["fuel_level"] = round(data.fuelLevel * 10) / 10.0;
  
  // Novos campos
  doc["engine_load"] = round(data.engineLoad * 10) / 10.0;
  doc["coolant_temp"] = round(data.coolantTemp * 10) / 10.0;
  doc["intake_temp"] = round(data.intakeTemp * 10) / 10.0;
  doc["maf_rate"] = round(data.mafRate * 100) / 100.0;
  doc["fuel_pressure"] = round(data.fuelPressure * 10) / 10.0;
  doc["timing_advance"] = round(data.timingAdvance * 10) / 10.0;
  doc["fuel_rate"] = round(data.fuelRate * 100) / 100.0;
  
  doc["timestamp"] = millis();
  
  String payload;
  serializeJson(doc, payload);
  return payload;
}

bool sendDataToServer(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi desconectado. Tentando reconectar...");
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