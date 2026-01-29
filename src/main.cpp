#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID "iCellulaire"
#define WIFI_PASS "mBi540816"

#define MQTT_HOST "captain.dev0.pandor.cloud"
#define MQTT_PORT 1884
#define MQTT_TOPIC "tug/teamB/Kaktus"

#define BUTTON_TEST 14

WiFiClient espClient;
PubSubClient mqtt(espClient);

const char* DEVICE_ID = "esp32-div";
uint32_t seq = 42;

bool lastButtonState = HIGH;

// Stats pour analyser le rate limit
struct RateLimitStats {
  uint32_t totalSent = 0;
  uint32_t totalSuccess = 0;
  uint32_t totalFailed = 0;
  unsigned long startTime = 0;
  unsigned long endTime = 0;
  uint32_t messagesPerSecond = 0;
};

RateLimitStats stats;

void connectWiFi() {
  if(WiFi.status() == WL_CONNECTED) return;
  
  Serial.println("\n[WIFI] Connexion...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int timeout = 30;
  while(WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(500);
    Serial.print(".");
    timeout--;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] ✓ OK - IP: " + WiFi.localIP().toString());
  }
}

void connectMQTT() {
  if(mqtt.connected()) return;
  
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(15);
  
  Serial.println("[MQTT] Connexion...");
  String clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  
  for(int i = 0; i < 3; i++) {
    if(mqtt.connect(clientId.c_str())) {
      Serial.println("[MQTT] ✓ Connecté");
      return;
    }
    delay(1000);
  }
}

bool sendMessage() {
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["seq"] = seq++;
  doc["timestamp"] = millis();
  
  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  
  return mqtt.publish(MQTT_TOPIC, payload, n);
}

void testRateLimit(uint32_t totalMessages, uint32_t delayMs) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     TEST DE RATE LIMIT                 ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Messages à envoyer : ");
  Serial.println(totalMessages);
  Serial.print("Délai entre messages : ");
  Serial.print(delayMs);
  Serial.println("ms");
  Serial.println("────────────────────────────────────────");
  
  // Reset stats
  stats.totalSent = 0;
  stats.totalSuccess = 0;
  stats.totalFailed = 0;
  stats.startTime = millis();
  
  uint32_t successStreak = 0;
  uint32_t failStreak = 0;
  uint32_t maxSuccessStreak = 0;
  uint32_t firstFailAt = 0;
  
  for(uint32_t i = 0; i < totalMessages; i++) {
    stats.totalSent++;
    
    bool success = sendMessage();
    
    if(success) {
      stats.totalSuccess++;
      successStreak++;
      failStreak = 0;
      Serial.print("✓");
      
      if(successStreak > maxSuccessStreak) {
        maxSuccessStreak = successStreak;
      }
    } else {
      stats.totalFailed++;
      failStreak++;
      successStreak = 0;
      Serial.print("✗");
      
      if(firstFailAt == 0) {
        firstFailAt = i + 1;
      }
    }
    
    // Affichage tous les 10 messages
    if((i + 1) % 10 == 0) {
      Serial.print(" [");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(totalMessages);
      Serial.print("] ");
      
      // Taux de succès actuel
      float successRate = (float)stats.totalSuccess / stats.totalSent * 100.0;
      Serial.print(successRate, 1);
      Serial.println("%");
    }
    
    // mqtt.loop tous les 5 messages
    if(i % 5 == 0) {
      mqtt.loop();
    }
    
    if(delayMs > 0) {
      delay(delayMs);
    }
  }
  
  stats.endTime = millis();
  mqtt.loop(); // Final loop
  
  // Calcul des statistiques
  unsigned long duration = stats.endTime - stats.startTime;
  float durationSec = duration / 1000.0;
  stats.messagesPerSecond = (uint32_t)(stats.totalSuccess / durationSec);
  
  // Affichage des résultats
  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║          RÉSULTATS                     ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  Serial.print("Messages envoyés    : ");
  Serial.println(stats.totalSent);
  
  Serial.print("✓ Succès            : ");
  Serial.print(stats.totalSuccess);
  Serial.print(" (");
  Serial.print((float)stats.totalSuccess / stats.totalSent * 100.0, 1);
  Serial.println("%)");
  
  Serial.print("✗ Échecs            : ");
  Serial.print(stats.totalFailed);
  Serial.print(" (");
  Serial.print((float)stats.totalFailed / stats.totalSent * 100.0, 1);
  Serial.println("%)");
  
  Serial.println("────────────────────────────────────────");
  
  Serial.print("Durée totale        : ");
  Serial.print(durationSec, 2);
  Serial.println(" secondes");
  
  Serial.print("Débit réel          : ");
  Serial.print(stats.messagesPerSecond);
  Serial.println(" msg/sec");
  
  Serial.println("────────────────────────────────────────");
  
  if(firstFailAt > 0) {
    Serial.print("Premier échec au    : message #");
    Serial.println(firstFailAt);
  } else {
    Serial.println("Premier échec au    : AUCUN !");
  }
  
  Serial.print("Plus longue série ✓ : ");
  Serial.print(maxSuccessStreak);
  Serial.println(" messages");
  
  Serial.println("════════════════════════════════════════\n");
}

void testMultipleScenarios() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   TESTS MULTIPLES SCENARIOS            ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Test 1 : Burst rapide
  Serial.println("📊 TEST 1 : Burst rapide (100 msg, 0ms délai)");
  testRateLimit(10, 0);
  delay(2000);
  
  // Test 2 : Envoi modéré
  Serial.println("📊 TEST 2 : Envoi modéré (50 msg, 50ms délai)");
  testRateLimit(50, 50);
  delay(2000);
  
  // Test 3 : Envoi lent
  Serial.println("📊 TEST 3 : Envoi lent (30 msg, 100ms délai)");
  testRateLimit(30, 100);
  delay(2000);
  
  // Test 4 : Long burst
  Serial.println("📊 TEST 4 : Long burst (200 msg, 0ms délai)");
  testRateLimit(50, 0);
  
  Serial.println("\n✅ TOUS LES TESTS TERMINÉS !\n");
}

void handleButton() {
  int reading = digitalRead(BUTTON_TEST);
  
  if(reading == LOW && lastButtonState == HIGH) {
    testMultipleScenarios();
    delay(300);
  }
  
  lastButtonState = reading;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   RATE LIMIT TESTER v1.0               ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  pinMode(BUTTON_TEST, INPUT_PULLUP);
  
  connectWiFi();
  connectMQTT();
  
  Serial.println("\n✅ PRÊT - Appuyez sur le bouton pour tester\n");
  Serial.println("Ou envoyez 't' via Serial Monitor\n");
}

void loop() {
  static unsigned long lastWifiCheck = 0;
  if(millis() - lastWifiCheck > 5000) {
    if(WiFi.status() != WL_CONNECTED) connectWiFi();
    lastWifiCheck = millis();
  }
  
  if(!mqtt.connected()) connectMQTT();
  
  mqtt.loop();
  handleButton();
  
  // Test via Serial Monitor
  if(Serial.available()) {
    char cmd = Serial.read();
    if(cmd == 't' || cmd == 'T') {
      testMultipleScenarios();
    }
    if(cmd == '1') {
      Serial.println("\nTest personnalisé :");
      Serial.println("Entrez le nombre de messages :");
      while(!Serial.available()) delay(5000);
      uint32_t numMsg = Serial.parseInt();
      Serial.println("Entrez le délai (ms) :");
      while(!Serial.available()) delay(5000);
      uint32_t delayMs = Serial.parseInt();
      testRateLimit(numMsg, delayMs);
    }
  }
  
  delay(10);
}