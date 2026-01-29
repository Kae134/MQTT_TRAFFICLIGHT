#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID "iCellulaire"
#define WIFI_PASS "mBi540816"

#define MQTT_HOST "172.20.10.11"
#define MQTT_PORT 1883

#define MQTT_TOPIC "trafic/status"

WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* DEVICE_ID = "trafic-02";
uint32_t seq = 42;
const uint32_t baseTs = 1767828437;
const uint32_t publishIntervalMs = 5000;
unsigned long lastPublishMs = 0;


const int LED_VERTE  = 25;
const int LED_YELLOW = 33;
const int LED_ROUGE  = 27;
const int BOUTON     = 14;
const int BUZZER     = 26;

bool green = false;
bool yellow = false;
bool red = false;

bool ambulanceState = false;
bool sirenActive = false;
unsigned long lastToneChange = 0;
bool toneHigh = false;
const int TONE_HIGH = 800;
const int TONE_LOW = 400;
const int TONE_DURATION = 300; 

void connectWiFi() {
  Serial.println("\n[WIFI] Démarrage...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("[WIFI] Scan des réseaux disponibles...");
  int n = WiFi.scanNetworks();
  for(int i = 0; i < n; i++) {
    Serial.print("  ");
    Serial.print(i+1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (");
    Serial.print(WiFi.RSSI(i));
    Serial.print(" dBm) ");
    Serial.println(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED");
  }
  
  Serial.print("[WIFI] Tentative de connexion à ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int timeout = 30; // 30 secondes
  while(WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    Serial.print(WiFi.status()); // Affiche le code d'état
    timeout--;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] ✓ Connecté !");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[WIFI] ✗ ÉCHEC de connexion");
    Serial.print("Statut WiFi: ");
    Serial.println(WiFi.status());
  }
}

void mqttCallBack(char* topic, byte* payload, unsigned int lenght) {
  Serial.print("[MQTT] Message recu sur le topic : ");
  Serial.println(topic);

  char message[lenght + 1];
  memcpy(message, payload, lenght);
  message[lenght] = '\0';

  Serial.print("[MQTT] Payload :");
  Serial.println(message);

  StaticJsonDocument<256> doc;
  deserializeJson(doc, message);
  if (DEVICE_ID == doc["deviceId"]) {
    green = doc["green"];
    yellow = doc["yellow"];
    red = doc["red"];
  }
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(30);

  mqtt.setCallback(mqttCallBack);
  
  Serial.println("\n[MQTT] Configuration:");
  Serial.print("  Host: ");
  Serial.println(MQTT_HOST);
  Serial.print("  Port: ");
  Serial.println(MQTT_PORT);
  
  int retries = 0;
  while(!mqtt.connected() && retries < 5) {
    String clientId = String("esp32-") + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("[MQTT] Tentative ");
    Serial.print(retries + 1);
    Serial.print(" - ClientID: ");
    Serial.println(clientId);
    
    bool ok = mqtt.connect(clientId.c_str());
    
    if(ok) {
      Serial.println("[MQTT] ✓ Connecté !");

      mqtt.subscribe("trafic/cmd");
      Serial.println("[MQTT] Abonné au topic !");
      return;
    } else {
      int state = mqtt.state();
      Serial.print("[MQTT] ✗ Échec, code: ");
      Serial.print(state);
      Serial.print(" = ");
      
      switch(state) {
        case -4: Serial.println("MQTT_CONNECTION_TIMEOUT"); break;
        case -3: Serial.println("MQTT_CONNECTION_LOST"); break;
        case -2: Serial.println("MQTT_CONNECT_FAILED"); break;
        case -1: Serial.println("MQTT_DISCONNECTED"); break;
        case  1: Serial.println("MQTT_CONNECT_BAD_PROTOCOL"); break;
        case  2: Serial.println("MQTT_CONNECT_BAD_CLIENT_ID"); break;
        case  3: Serial.println("MQTT_CONNECT_UNAVAILABLE"); break;
        case  4: Serial.println("MQTT_CONNECT_BAD_CREDENTIALS"); break;
        case  5: Serial.println("MQTT_CONNECT_UNAUTHORIZED"); break;
        default: Serial.println("UNKNOWN"); break;
      }
      
      retries++;
      if(retries < 5) {
        Serial.println("  Nouvelle tentative dans 3s...");
        delay(3000);
      }
    }
  }
  
  Serial.println("[MQTT] ✗ ABANDON après 5 tentatives");
}

void updateSiren() {
  if (!sirenActive) {
    noTone(BUZZER);
    return;
  }
  
  // Sirène avec balayage de fréquence
  static int currentFreq = TONE_LOW;
  static int direction = 10;
  
  unsigned long now = millis();
  if (now - lastToneChange >= 20) {  // Mise à jour toutes les 20ms
    lastToneChange = now;
    
    currentFreq += direction;
    if (currentFreq >= TONE_HIGH) direction = -10;
    if (currentFreq <= TONE_LOW) direction = 10;
    
    tone(BUZZER, currentFreq);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectWiFi();
  connectMQTT();

  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_VERTE, OUTPUT);

  pinMode(BUZZER, OUTPUT);

  pinMode(BOUTON, INPUT_PULLUP);
}


void publishAmbulance(bool ambulanceStatus) {

  sirenActive = ambulanceStatus;
  if (!sirenActive) {
    noTone(BUZZER);
  }

  StaticJsonDocument<256> doc;
  JsonObject t = doc.to<JsonObject>();
  t["deviceId"] = DEVICE_ID;
  t["ambulance"] = ambulanceStatus;
  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  bool ok = mqtt.publish(MQTT_TOPIC, payload, n);
  Serial.print("[MQTT] Publish to ");
  Serial.print(MQTT_TOPIC);
  Serial.print(" ... ");
  Serial.println(ok ? payload : "FAILED");
}

void loop() {
  if(WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if(!mqtt.connected()) {
    connectMQTT();
  }

  mqtt.loop();

  updateSiren();

  digitalWrite(LED_ROUGE, red);
  digitalWrite(LED_YELLOW, yellow);
  digitalWrite(LED_VERTE, green);

  unsigned long now = millis();
  if(now - lastPublishMs >= publishIntervalMs && digitalRead(BOUTON) == LOW) {
    lastPublishMs = now;
    ambulanceState = !ambulanceState;
    publishAmbulance(ambulanceState);
  }

}


