#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID "iCellulaire"
#define WIFI_PASS "mBi540816"

#define MQTT_HOST "captain.dev0.pandor.cloud"
#define MQTT_PORT 1884

#define BUTTON_TEST 14

const char* ESP_IDS[] = { "888", "889", "890", "891", "892" };
const int ESP_COUNT = 5;

WiFiClient espClient;
PubSubClient mqtt(espClient);

String DEVICE_ID = "flan";
String MQTT_TOPIC = "tug/teamA/MiniFlan";
uint32_t seq = 42;

bool lastButtonState = HIGH;

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
  if(mqtt.connected()) {
    mqtt.disconnect(); // Déconnexion pour forcer une nouvelle connexion avec les nouveaux paramètres
  }
  
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
  
  return mqtt.publish(MQTT_TOPIC.c_str(), payload, n);
}

void testRateLimit(uint32_t totalMessages, uint32_t delayMs) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     TEST DE RATE LIMIT                 ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Device ID : ");
  Serial.println(DEVICE_ID);
  Serial.print("Topic     : ");
  Serial.println(MQTT_TOPIC);
  Serial.print("Messages  : ");
  Serial.println(totalMessages);
  Serial.print("Délai     : ");
  Serial.print(delayMs);
  Serial.println("ms");
  Serial.println("────────────────────────────────────────");
  
  stats.totalSent = 0;
  stats.totalSuccess = 0;
  stats.totalFailed = 0;
  stats.startTime = millis();
  
  uint32_t successStreak = 0;
  uint32_t maxSuccessStreak = 0;
  uint32_t firstFailAt = 0;
  
  for(uint32_t i = 0; i < totalMessages; i++) {
    stats.totalSent++;
    
    bool success = sendMessage();
    
    if(success) {
      stats.totalSuccess++;
      successStreak++;
      Serial.print("✓");
      
      if(successStreak > maxSuccessStreak) {
        maxSuccessStreak = successStreak;
      }
    } else {
      stats.totalFailed++;
      successStreak = 0;
      Serial.print("✗");
      
      if(firstFailAt == 0) {
        firstFailAt = i + 1;
      }
    }
    
    if((i + 1) % 10 == 0) {
      Serial.print(" [");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(totalMessages);
      Serial.print("] ");
      
      float successRate = (float)stats.totalSuccess / stats.totalSent * 100.0;
      Serial.print(successRate, 1);
      Serial.println("%");
    }
    
    if(i % 5 == 0) {
      mqtt.loop();
    }
    
    if(delayMs > 0) {
      uint32_t randomDelay = random(0, delayMs + 1);
      delay(randomDelay);
    }
  }
  
  stats.endTime = millis();
  mqtt.loop();
  
  unsigned long duration = stats.endTime - stats.startTime;
  float durationSec = duration / 1000.0;
  stats.messagesPerSecond = (durationSec > 0) ? (uint32_t)(stats.totalSuccess / durationSec) : 0;
  
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
  
  Serial.println("📊 TEST 1 : Burst rapide (100 msg, 0ms délai)");
  testRateLimit(100, 0);
  delay(2000);
  
  Serial.println("📊 TEST 2 : Envoi modéré (50 msg, 50ms délai)");
  testRateLimit(50, 50);
  delay(2000);
  
  Serial.println("📊 TEST 3 : Envoi lent (30 msg, 100ms délai)");
  testRateLimit(30, 100);
  delay(2000);
  
  Serial.println("📊 TEST 4 : Long burst (200 msg, 0ms délai)");
  testRateLimit(200, 0);
  
  Serial.println("\n✅ TOUS LES TESTS TERMINÉS !\n");
}

int readNumberFromSerial(const char* prompt, int defaultValue, int minValue, int maxValue) {
  Serial.print(prompt);
  Serial.print(" [");
  Serial.print(minValue);
  Serial.print("-");
  Serial.print(maxValue);
  Serial.print("] (défaut: ");
  Serial.print(defaultValue);
  Serial.print("): ");
  
  String input = "";
  unsigned long startTime = millis();
  unsigned long timeout = 30000;
  
  while(Serial.available()) {
    Serial.read();
  }
  
  while(millis() - startTime < timeout) {
    if(Serial.available()) {
      char c = Serial.read();
      
      Serial.print(c);
      
      if(c == '\n' || c == '\r') {
        Serial.println();
        
        if(input.length() == 0) {
          Serial.print("→ Utilisation de la valeur par défaut: ");
          Serial.println(defaultValue);
          return defaultValue;
        }
        
        int value = input.toInt();
        
        if(value < minValue || value > maxValue) {
          Serial.print("⚠️  Valeur hors limites! Utilisation de ");
          Serial.println(defaultValue);
          return defaultValue;
        }
        
        Serial.print("✓ Valeur acceptée: ");
        Serial.println(value);
        return value;
      }
      
      if(c == 8 || c == 127) {
        if(input.length() > 0) {
          input.remove(input.length() - 1);
          Serial.print(" \b");
        }
      }
      else if(c >= '0' && c <= '9') {
        input += c;
      }
    }
    
    delay(10);
  }
  
  Serial.println("\n⏱️  Timeout - Utilisation de la valeur par défaut");
  return defaultValue;
}

// Fonction pour lire une chaîne de caractères
String readStringFromSerial(const char* prompt, String defaultValue, int maxLength) {
  Serial.print(prompt);
  Serial.print(" (défaut: ");
  Serial.print(defaultValue);
  Serial.print("): ");
  
  String input = "";
  unsigned long startTime = millis();
  unsigned long timeout = 30000;
  
  while(Serial.available()) {
    Serial.read();
  }
  
  while(millis() - startTime < timeout) {
    if(Serial.available()) {
      char c = Serial.read();
      
      Serial.print(c);
      
      if(c == '\n' || c == '\r') {
        Serial.println();
        
        if(input.length() == 0) {
          Serial.print("→ Utilisation de la valeur par défaut: ");
          Serial.println(defaultValue);
          return defaultValue;
        }
        
        input.trim();
        Serial.print("✓ Nouvelle valeur: ");
        Serial.println(input);
        return input;
      }
      
      if(c == 8 || c == 127) {
        if(input.length() > 0) {
          input.remove(input.length() - 1);
          Serial.print(" \b");
        }
      }
      else if(input.length() < maxLength && c >= 32 && c <= 126) {
        input += c;
      }
    }
    
    delay(10);
  }
  
  Serial.println("\n⏱️  Timeout - Utilisation de la valeur par défaut");
  return defaultValue;
}

void changeDeviceID() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     CHANGEMENT DEVICE ID               ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Device ID actuel : ");
  Serial.println(DEVICE_ID);
  Serial.println();
  
  String newID = readStringFromSerial("Nouveau Device ID", DEVICE_ID, 50);
  
  if(newID != DEVICE_ID) {
    DEVICE_ID = newID;
    Serial.println("\n✅ Device ID mis à jour!");
    Serial.print("Nouveau Device ID : ");
    Serial.println(DEVICE_ID);
    
    // Reset le compteur de séquence
    seq = 0;
    Serial.println("⚠️  Compteur de séquence réinitialisé à 0");
  } else {
    Serial.println("\n→ Device ID inchangé");
  }
}

void sendManetteCommand(const char* command) {
  static char mqttTopicBuffer[256];

  for (int i = 0; i < ESP_COUNT; i++) {

    String topic = "pokemon/vote/";

    topic += "{ \"esp32_id\": \"";
    topic += ESP_IDS[i];
    topic += "\", \"command\": \"";
    topic += command;
    topic += "\" }";

    topic.toCharArray(mqttTopicBuffer, sizeof(mqttTopicBuffer));
    MQTT_TOPIC = mqttTopicBuffer;

    Serial.print("✓ Topic prêt = ");
    Serial.println(MQTT_TOPIC);

    // 🚨 C’EST LUI QUI ENVOIE
    testRateLimit(1, 50);
  }
}




void manette() {
  bool quit = false;

  while (!quit) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║     Choisis le controle                ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println("  1. up");
    Serial.println("  2. down");
    Serial.println("  3. left");
    Serial.println("  4. right");
    Serial.println("  5. a");
    Serial.println("  6. b");
    Serial.println("  7. start");
    Serial.println("  8. select");
    Serial.println("  q. quitter");

    while (!Serial.available()) delay(10);

    char choice = Serial.read();
    while (Serial.available()) Serial.read();

    switch (choice) {
      case '1': sendManetteCommand("up"); break;
      case '2': sendManetteCommand("down"); break;
      case '3': sendManetteCommand("left"); break;
      case '4': sendManetteCommand("right"); break;
      case '5': sendManetteCommand("a"); break;
      case '6': sendManetteCommand("b"); break;
      case '7': sendManetteCommand("start"); break;
      case '8': sendManetteCommand("select"); break;

      case 'q':
      case 'Q':
        Serial.println("→ Quit");
        quit = true;
        break;

      default:
        Serial.println("❌ Choix invalide");
    }
  }
}

void changeMQTTTopic() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     CHANGEMENT MQTT TOPIC              ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Topic actuel : ");
  Serial.println(MQTT_TOPIC);
  Serial.println();
  
  String newTopic = readStringFromSerial("Nouveau MQTT Topic", MQTT_TOPIC, 100);
  
  if(newTopic != MQTT_TOPIC) {
    MQTT_TOPIC = newTopic;
    Serial.println("\n✅ MQTT Topic mis à jour!");
    Serial.print("Nouveau Topic : ");
    Serial.println(MQTT_TOPIC);
    
    // Reconnexion MQTT pour appliquer les changements
    if(mqtt.connected()) {
      Serial.println("🔄 Reconnexion MQTT...");
      connectMQTT();
    }
  } else {
    Serial.println("\n→ Topic inchangé");
  }
}

void showConfiguration() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     CONFIGURATION ACTUELLE             ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  Serial.println("\n📡 MQTT:");
  Serial.print("  Broker       : ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  Serial.print("  Topic        : ");
  Serial.println(MQTT_TOPIC);
  Serial.print("  Status       : ");
  Serial.println(mqtt.connected() ? "✓ Connecté" : "✗ Déconnecté");
  
  Serial.println("\n🔧 Device:");
  Serial.print("  Device ID    : ");
  Serial.println(DEVICE_ID);
  Serial.print("  Seq actuel   : ");
  Serial.println(seq);
  
  Serial.println("\n📶 WiFi:");
  Serial.print("  Status       : ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "✓ Connecté" : "✗ Déconnecté");
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.print("  SSID         : ");
    Serial.println(WiFi.SSID());
    Serial.print("  IP           : ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal       : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  
  Serial.println("\n════════════════════════════════════════\n");
}

void showPresets() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     PRESETS DISPONIBLES                ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("\n🎯 Device IDs:");
  Serial.println("  1. trafic-01");
  Serial.println("  2. trafic-02");
  Serial.println("  3. trafic-03");
  Serial.println("  4. trafic-adversaire");
  Serial.println("  5. trafic-test");
  
  Serial.println("\n📡 Topics:");
  Serial.println("  a. tug/teamA");
  Serial.println("  b. tug/teamB");
  Serial.println("  c. trafic/cmd");
  Serial.println("  d. test/topic");
  Serial.println("  e. pokemon/vote/{'esp32_id':'888', 'command':'up'}");
  Serial.println("  f. pokemon/vote/{'esp32_id':'888', 'command':'down'}");
  Serial.println("  g. pokemon/vote/{'esp32_id':'888', 'command':'left'}");
  Serial.println("  h. pokemon/vote/{'esp32_id':'888', 'command':'right'}");
  Serial.println("  i. pokemon/vote/{'esp32_id':'888', 'command':'a'}");
  Serial.println("  j. pokemon/vote/{'esp32_id':'888', 'command':'b'}");
  Serial.println("  k. pokemon/vote/{'esp32_id':'888', 'command':'start'}");
  Serial.println("  l. pokemon/vote/{'esp32_id':'888', 'command':'select'}");
  
  Serial.println("\nChoisissez un preset ou 'q' pour quitter: ");
  
  while(!Serial.available()) {
    delay(10);
  }
  
  char choice = Serial.read();
  while(Serial.available()) Serial.read(); 
  
  Serial.println(choice);
  
  switch(choice) {
    case '1': DEVICE_ID = "trafic-01"; Serial.println("✓ Device ID = trafic-01"); break;
    case '2': DEVICE_ID = "trafic-02"; Serial.println("✓ Device ID = trafic-02"); break;
    case '3': DEVICE_ID = "trafic-03"; Serial.println("✓ Device ID = trafic-03"); break;
    case '4': DEVICE_ID = "trafic-adversaire"; Serial.println("✓ Device ID = trafic-adversaire"); break;
    case '5': DEVICE_ID = "trafic-test"; Serial.println("✓ Device ID = trafic-test"); break;
    
    case 'a': case 'A': MQTT_TOPIC = "tug/teamA/Paindemie"; Serial.println("✓ Topic = tug/teamA/Paindemie"); connectMQTT(); break;
    case 'b': case 'B': MQTT_TOPIC = "tug/teamB/Kaktus"; Serial.println("✓ Topic = tug/teamB/Kaktus"); connectMQTT(); break;
    case 'c': case 'C': MQTT_TOPIC = "trafic/cmd"; Serial.println("✓ Topic = trafic/cmd"); connectMQTT(); break;
    case 'd': case 'D': MQTT_TOPIC = "test/topic"; Serial.println("✓ Topic = test/topic"); connectMQTT(); break;

    case 'e': case 'E': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'up'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'up'}"); connectMQTT(); break;
    case 'f': case 'F': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'down'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'down'}"); connectMQTT(); break;
    case 'g': case 'G': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'left'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'left'}"); connectMQTT(); break;
    case 'h': case 'H': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'right'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'right'}"); connectMQTT(); break;
    case 'i': case 'I': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'a'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'a'}"); connectMQTT(); break;
    case 'j': case 'J': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'b'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'b'}"); connectMQTT(); break;
    case 'k': case 'K': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'start'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'start'}"); connectMQTT(); break;
    case 'l': case 'L': MQTT_TOPIC = "pokemon/vote/{'esp32_id':'888', 'command':'select'}"; Serial.println("✓ Topic = pokemon/vote/{'esp32_id':'888', 'command':'select'}"); connectMQTT(); break;
    
    case 'q': case 'Q': Serial.println("→ Annulé"); break;
    default: Serial.println("❌ Choix invalide"); break;
  }
}

void showMenu() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         MENU PRINCIPAL                 ║");
  Serial.println("╠════════════════════════════════════════╣");
  Serial.println("║  TESTS:                                ║");
  Serial.println("║  1 - Test personnalisé                 ║");
  Serial.println("║  2 - Tests automatiques (4 scénarios)  ║");
  Serial.println("║  3 - Burst rapide (100 msg)            ║");
  Serial.println("║  4 - Test long (500 msg)               ║");
  Serial.println("║  5 - Envoi continu (s pour stop)       ║");
  Serial.println("║  6 - Manette                           ║");
  Serial.println("║                                        ║");
  Serial.println("║  CONFIGURATION:                        ║");
  Serial.println("║  d - Changer Device ID                 ║");
  Serial.println("║  t - Changer MQTT Topic                ║");
  Serial.println("║  p - Presets rapides                   ║");
  Serial.println("║  c - Voir configuration                ║");
  Serial.println("║                                        ║");
  Serial.println("║  AUTRES:                               ║");
  Serial.println("║  h - Afficher ce menu                  ║");
  Serial.println("║  r - Reconnexion MQTT                  ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("\n[");
  Serial.print(DEVICE_ID);
  Serial.print(" → ");
  Serial.print(MQTT_TOPIC);
  Serial.println("]");
  Serial.println("\nEntrez votre choix: ");
}

void handleSerialCommands() {
  if(!Serial.available()) return;
  
  char cmd = Serial.read();
  
  while(Serial.available()) {
    Serial.read();
  }
  
  Serial.println(cmd);
  
  switch(cmd) {
    case '1': {
      Serial.println("\n🔧 TEST PERSONNALISÉ\n");
      int numMsg = readNumberFromSerial("Nombre de messages", 100, 1, 10000);
      int delayMs = readNumberFromSerial("Délai entre messages (ms)", 0, 0, 5000);
      testRateLimit(numMsg, delayMs);
      showMenu();
      break;
    }
    
    case '2':
      testMultipleScenarios();
      showMenu();
      break;
    
    case '3':
      Serial.println("\n⚡ BURST RAPIDE\n");
      testRateLimit(100, 0);
      showMenu();
      break;
    
    case '4':
      Serial.println("\n📈 TEST LONG\n");
      testRateLimit(500, 0);
      showMenu();
      break;
    
    case '5': {
      Serial.println("\n🔄 ENVOI CONTINU - Envoyez 's' pour arrêter\n");
      uint32_t count = 0;
      unsigned long lastPrint = 0;
      
      while(true) {
        if(Serial.available()) {
          char stop = Serial.read();
          if(stop == 's' || stop == 'S') {
            Serial.println("\n\n⏹️  ARRÊT demandé");
            break;
          }
        }
        
        bool success = sendMessage();
        count++;
        
        if(success) {
          Serial.print("✓");
        } else {
          Serial.print("✗");
        }
        
        if(count % 10 == 0) {
          mqtt.loop();
          Serial.print(" ");
          Serial.println(count);
        }
        
        if(millis() - lastPrint > 5000) {
          Serial.print("\n📊 Total envoyé: ");
          Serial.println(count);
          lastPrint = millis();
        }
        
        delay(50);
      }
      
      Serial.print("\n✅ Total messages envoyés: ");
      Serial.println(count);
      showMenu();
      break;
    }

    case '6':
      manette();
      break;
    
    case 'd':
    case 'D':
      changeDeviceID();
      showMenu();
      break;
    
    case 't':
    case 'T':
      changeMQTTTopic();
      showMenu();
      break;
    
    case 'p':
    case 'P':
      showPresets();
      showMenu();
      break;
    
    case 'c':
    case 'C':
      showConfiguration();
      showMenu();
      break;
    
    case 'h':
    case 'H':
      showMenu();
      break;
    
    case 'r':
    case 'R':
      Serial.println("\n🔄 Reconnexion MQTT...");
      connectMQTT();
      showMenu();
      break;
    
    default:
      Serial.println("❌ Commande inconnue");
      showMenu();
      break;
  }
}

void handleButton() {
  int reading = digitalRead(BUTTON_TEST);
  
  if(reading == LOW && lastButtonState == HIGH) {
    Serial.println("\n🔘 BOUTON PRESSÉ - Burst rapide de 100 messages\n");
    testRateLimit(100, 0);
    delay(300);
  }
  
  lastButtonState = reading;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   RATE LIMIT TESTER v3.0               ║");
  Serial.println("║   Avec configuration dynamique         ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  pinMode(BUTTON_TEST, INPUT_PULLUP);
  
  connectWiFi();
  connectMQTT();
  
  Serial.println("\n✅ SYSTÈME PRÊT\n");
  
  showMenu();
}

void loop() {
  static unsigned long lastWifiCheck = 0;
  if(millis() - lastWifiCheck > 5000) {
    if(WiFi.status() != WL_CONNECTED) {
      Serial.println("\n⚠️  WiFi déconnecté - Reconnexion...");
      connectWiFi();
    }
    if(!mqtt.connected()) {
      Serial.println("\n⚠️  MQTT déconnecté - Reconnexion...");
      connectMQTT();
    }
    lastWifiCheck = millis();
  }
  
  mqtt.loop();
  handleButton();
  handleSerialCommands();
  
  delay(10);
}