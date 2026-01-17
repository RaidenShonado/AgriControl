#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>

#define WIFI_SSID "DEER COFFEE"
#define WIFI_PASSWORD "12345678"

/* Firebase config */
#define FIREBASE_HOST "argicontrol-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "noucLqeXUEKomO7bapwj2AK3d8gUKDPJSg6HgyXk"

/* Đường dẫn */
const char *pathSensors = "/agricontrol/climate/khuA";
const char *pathControl = "/agricontrol/irrigation/khuA";

/* UART pins and baud (ESP32) */
#define RX_PIN 16   // nối TX của STM32 (PA9)
#define TX_PIN 17    // nối RX của STM32 (PA10)
#define UART_BAUD 115200

/* Firebase objects */
FirebaseData fbdo;
FirebaseData streamData;
FirebaseAuth auth;
FirebaseConfig config;

/* buffer UART */
String uartLine = "";
bool haveLine = false;

/* Flag to force sync on first stream trigger */
static bool firstSync = true;

/* Sensor data structure (the keys we will upload) */
struct SensorData {
  float temperature;     // temperature (T)
  float airHumidity;     // from H
  uint16_t soilHumidity; // from Soil (ADC raw)
  float lightIntensity;  // from BH1750 or L
  // optional: actuator states stored locally
  int waterState; // pump
  int lightState; // led
  int ventPos;    // servo degree
};

SensorData lastSensor = {0.0f, 0.0f, 0, 0.0f, 0, 0, 0};

/* Parse line from STM32 (example):
   T:23.4,H:55.1,L:120.3,Soil:512,Pump:1,Servo:90,LED:1
   we map:
     T -> temperature
     H -> airHumidity
     L -> lightIntensity
     Soil -> soilHumidity
     Pump -> waterState
     Servo -> ventPos
     LED -> lightState
*/
bool parseSTM32Line(const String &line, SensorData &out) {
  String s = line;
  s.trim();
  while (s.endsWith("\n") || s.endsWith("\r")) s.remove(s.length()-1);
  if (s.length() == 0) return false;

  // start from previous values to avoid overwriting missing fields
  out = lastSensor;

  int start = 0;
  while (start < (int)s.length()) {
    int comma = s.indexOf(',', start);
    String token;
    if (comma == -1) {
      token = s.substring(start);
      start = s.length();
    } else {
      token = s.substring(start, comma);
      start = comma + 1;
    }
    token.trim();
    if (token.length() == 0) continue;

    int colon = token.indexOf(':');
    if (colon == -1) continue;
    String key = token.substring(0, colon);
    String val = token.substring(colon + 1);
    key.trim(); val.trim();
    key.toUpperCase();

    if (key == "T" || key == "TEMP" || key == "TEMPERATURE") {
      out.temperature = val.toFloat();
    } else if (key == "H" || key == "HUM" || key == "HUMIDITY") {
      out.airHumidity = val.toFloat();
    } else if (key == "L" || key == "LUX" || key == "LIGHT" || key == "LIGHTINTENSITY") {
      out.lightIntensity = val.toFloat();
    } else if (key == "SOIL" || key == "SOILHUMIDITY") {
      out.soilHumidity = (uint16_t)val.toInt();
    } else if (key == "PUMP" || key == "P") {
      out.waterState = val.toInt() ? 1 : 0;
    } else if (key == "SERVO" || key == "S") {
      out.ventPos = val.toInt();
    } else if (key == "LED" || key == "LD") {
      out.lightState = val.toInt() ? 1 : 0;
    }
  }

  return true;
}

/* Upload sensor values to Firebase */
void uploadSensorToFirebase(const SensorData &d) {
  // Upload ONLY sensor readings to climate path
  // Note: airHumidity key must NOT have leading space
  if (Firebase.setFloat(fbdo, String(pathSensors) + "/temperature", d.temperature) &&
      Firebase.setFloat(fbdo, String(pathSensors) + "/airHumidity", d.airHumidity) &&
      Firebase.setFloat(fbdo, String(pathSensors) + "/lightIntensity", d.lightIntensity) &&
      Firebase.setFloat(fbdo, String(pathSensors) + "/soilHumidity", d.soilHumidity)) {
    Serial.println("[FB] Sensors uploaded.");
  } else {
    Serial.printf("[FB] Upload failed: %s\n", fbdo.errorReason().c_str());
  }
  
  // Note: Actuator states (water, light, vent) are managed by web interface
  // They are stored in /irrigation path, not /climate
}

/* Send command to STM32 - HEX protocol:
   0x01 = Pump ON, 0x02 = Pump OFF
   0x03 = LED ON, 0x04 = LED OFF
   0x80-0xE4 = Servo 0-100
*/
void sendCommandToSTM32(int water, int light, int vent) {
  // Send each command separately with small delay
  // to ensure STM32 processes them one by one
  
  // 1. Water (pump)
  uint8_t pump_cmd = (water ? 0x01 : 0x02);
  Serial.printf("[->STM32] Sending PUMP: 0x%02X\n", pump_cmd);
  Serial2.write(pump_cmd);
  Serial2.flush();
  delay(20);  // 20ms delay for STM32 to process
  
  // 2. Light (LED)
  uint8_t led_cmd = (light ? 0x03 : 0x04);
  Serial.printf("[->STM32] Sending LED: 0x%02X\n", led_cmd);
  Serial2.write(led_cmd);
  Serial2.flush();
  delay(20);  // 20ms delay for STM32 to process
  
  // 3. Servo: map 0-100 to 0x80-0xE4
  int servo_val = 0x80 + (vent * (0xE4 - 0x80) / 100);
  uint8_t servo_cmd = (uint8_t)servo_val;
  Serial.printf("[->STM32] Sending SERVO: 0x%02X\n", servo_cmd);
  Serial2.write(servo_cmd);
  Serial2.flush();
  
  Serial.println("[->STM32] All commands sent");
}

/* Stream callback from Firebase */
void streamCallback(StreamData data) {
  Serial.printf("[STREAM] ===== TRIGGERED =====\n");
  Serial.printf("[STREAM] path: %s\n", data.dataPath().c_str());
  Serial.printf("[STREAM] type: %s\n", data.dataType().c_str());
  
  // Print raw data for debugging
  if (data.dataType() == "json") {
    Serial.printf("[STREAM] JSON data: %s\n", data.jsonString().c_str());
  } else if (data.dataType() == "int") {
    Serial.printf("[STREAM] Int value: %d\n", data.intData());
  } else if (data.dataType() == "bool") {
    Serial.printf("[STREAM] Bool value: %s\n", data.boolData() ? "true" : "false");
  }

  // Initialize with last known values
  int water = lastSensor.waterState;
  int light = lastSensor.lightState;
  int vent = lastSensor.ventPos;
  
  bool changed = false;
  
  // Parse JSON object from stream data
  FirebaseJson json;
  FirebaseJsonData result;
  
  // Prefer using stream data directly; avoid network fetch on single-field updates
  bool single_field_handled = false;
  if (data.dataType() == "json") {
    json.setJsonData(data.jsonString());
    Serial.printf("[STREAM] Parsing stream JSON data...\n");
    Serial.printf("[STREAM] Parsing JSON for water/light/vent...\n");
  } else {
    // Single-field update: use path to update only that field
    String key = data.dataPath(); // e.g., "/water"
    if (key.startsWith("/")) key.remove(0, 1);
    bool handled = false;

    if (key == "water") {
      int new_water = 0;
      if (data.dataType() == "bool") new_water = data.boolData() ? 1 : 0;
      else if (data.dataType() == "int") new_water = (data.intData() != 0) ? 1 : 0;
      else if (data.dataType() == "string") new_water = (data.stringData().indexOf("true") >= 0) ? 1 : 0;
      if (new_water != water) { water = new_water; changed = true; Serial.printf("[STREAM] water changed to %d\n", water); }
      else { Serial.printf("[STREAM] water unchanged: %d\n", water); }
      handled = true;
    } else if (key == "light") {
      int new_light = 0;
      if (data.dataType() == "bool") new_light = data.boolData() ? 1 : 0;
      else if (data.dataType() == "int") new_light = (data.intData() != 0) ? 1 : 0;
      else if (data.dataType() == "string") new_light = (data.stringData().indexOf("true") >= 0) ? 1 : 0;
      if (new_light != light) { light = new_light; changed = true; Serial.printf("[STREAM] light changed to %d\n", light); }
      else { Serial.printf("[STREAM] light unchanged: %d\n", light); }
      handled = true;
    } else if (key == "vent") {
      int new_vent = 0;
      if (data.dataType() == "int") new_vent = data.intData();
      else if (data.dataType() == "string") new_vent = data.stringData().toInt();
      if (new_vent != vent) { vent = new_vent; changed = true; Serial.printf("[STREAM] vent changed to %d\n", vent); }
      else { Serial.printf("[STREAM] vent unchanged: %d\n", vent); }
      handled = true;
    }

    if (handled) {
      // Single-field handled, skip JSON parsing
      single_field_handled = true;
    } else {
      // Unknown key; fallback to fetching full JSON
      Serial.printf("[STREAM] Unknown key '%s'. Fetching full data from: %s\n", key.c_str(), pathControl);
      if (!Firebase.getJSON(fbdo, pathControl)) {
        Serial.printf("[STREAM] getJSON failed: %s\n", fbdo.errorReason().c_str());
        return;
      }
      json = fbdo.jsonObject();
      Serial.println("[STREAM] getJSON success");
      Serial.printf("[STREAM] Parsing JSON for water/light/vent...\n");
    }
  }

  // Only parse JSON if not a single-field update or if we fetched full JSON
  if (!single_field_handled && json.get(result, "water")) {
    Serial.printf("[STREAM] Found 'water' field, type: %s\n", result.type.c_str());
    // Handle both boolean and integer values
    int new_water = 0;
    if (result.type == "int") {
      new_water = (result.intValue != 0) ? 1 : 0;
    } else if (result.type == "bool") {
      new_water = result.boolValue ? 1 : 0;
    } else {
      // Try to parse as string "true"/"false"
      new_water = (result.stringValue.indexOf("true") >= 0) ? 1 : 0;
    }
    if (new_water != water) {
      water = new_water;
      changed = true;
      Serial.printf("[STREAM] water changed to %d\n", water);
    } else {
      Serial.printf("[STREAM] water unchanged: %d\n", water);
    }
  } else if (!single_field_handled) {
    Serial.println("[STREAM] 'water' field NOT found");
  }
  
  if (!single_field_handled && json.get(result, "light")) {
    Serial.printf("[STREAM] Found 'light' field, type: %s\n", result.type.c_str());
    // Handle both boolean and integer values
    int new_light = 0;
    if (result.type == "int") {
      new_light = (result.intValue != 0) ? 1 : 0;
    } else if (result.type == "bool") {
      new_light = result.boolValue ? 1 : 0;
    } else {
      // Try to parse as string "true"/"false"
      new_light = (result.stringValue.indexOf("true") >= 0) ? 1 : 0;
    }
    if (new_light != light) {
      light = new_light;
      changed = true;
      Serial.printf("[STREAM] light changed to %d\n", light);
    } else {
      Serial.printf("[STREAM] light unchanged: %d\n", light);
    }
  } else if (!single_field_handled) {
    Serial.println("[STREAM] 'light' field NOT found");
  }
  
  if (!single_field_handled && json.get(result, "vent")) {
    Serial.printf("[STREAM] Found 'vent' field, type: %s\n", result.type.c_str());
    int new_vent = result.intValue;
    if (new_vent != vent) {
      vent = new_vent;
      changed = true;
      Serial.printf("[STREAM] vent changed to %d\n", vent);
    } else {
      Serial.printf("[STREAM] vent unchanged: %d\n", vent);
    }
  } else if (!single_field_handled) {
    Serial.println("[STREAM] 'vent' field NOT found");
  }

  // Send to STM32 if changed OR if this is the first sync
  if (changed || firstSync) {
    if (firstSync) {
      Serial.printf("[STREAM] ✓ FIRST SYNC! Sending initial state to STM32...\n");
      firstSync = false;
    } else {
      Serial.printf("[STREAM] ✓ CHANGES DETECTED! Sending to STM32...\n");
    }
    Serial.printf("[STREAM] Sending to STM32: PUMP:%d,LED:%d,SERVO:%d\n", water, light, vent);
    sendCommandToSTM32(water, light, vent);
    lastSensor.waterState = water;
    lastSensor.lightState = light;
    lastSensor.ventPos = vent;
  } else {
    Serial.println("[STREAM] No changes detected.");
  }
  Serial.println("[STREAM] ===== DONE =====");
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("[STREAM] timeout");
  }
}

/* Setup */
void setup() {
  Serial.begin(115200);
  delay(50);

  // UART2 between ESP32 and STM32
  // Wait a bit before initializing Serial2 to avoid boot messages
  delay(500);
  Serial2.begin(UART_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // Clear any boot garbage in the buffer
  while (Serial2.available()) {
    Serial2.read();
  }
  
  Serial.printf("Serial2 started RX=%d TX=%d @%d\n", RX_PIN, TX_PIN, UART_BAUD);
  
  // Debug: check UART pins
  Serial.println("[UART] Waiting for STM32 boot...");
  delay(1000);
  
  // Test UART by checking if any data available
  if (Serial2.available()) {
    Serial.printf("[UART] %d bytes in RX buffer\n", Serial2.available());
  } else {
    Serial.println("[UART] No data from STM32 yet (normal if STM32 just booted)");
  }

  // connect WiFi
  Serial.printf("Connecting to WiFi %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++retry > 40) { // ~20s timeout
      Serial.println("\nWiFi connect failed, restart");
      delay(2000);
      ESP.restart();
    }
  }
  Serial.println("\nWiFi connected, IP:");
  Serial.println(WiFi.localIP());

  // Firebase config
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // begin stream on pathControl
  if (!Firebase.beginStream(streamData, pathControl)) {
    Serial.printf("[FB] beginStream failed: %s\n", streamData.errorReason().c_str());
  } else {
    Firebase.setStreamCallback(streamData, streamCallback, streamTimeoutCallback);
    Serial.println("[FB] Stream started on control path");
  }
}

/* Main loop */
void loop() {
  // read Serial2 until newline
  static unsigned long last_rx_debug = 0;
  static int rx_bytes_this_sec = 0;
  
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    rx_bytes_this_sec++;
    
    // Debug every second: show RX activity
    if (millis() - last_rx_debug > 1000) {
      if (rx_bytes_this_sec > 0) {
        Serial.printf("[UART RX] Received %d bytes in last second\n", rx_bytes_this_sec);
      }
      rx_bytes_this_sec = 0;
      last_rx_debug = millis();
    }
    
    uartLine += c;
    if (c == '\n') {
      haveLine = true;
      break;
    }
    if (uartLine.length() > 512) {
      Serial.println("[UART] ⚠️  Buffer overflow, clearing");
      uartLine = "";
    }
  }

  if (haveLine) {
    String line = uartLine;
    uartLine = "";
    haveLine = false;
    Serial.printf("[<-STM32] %s", line.c_str());

    SensorData s = lastSensor; // start from last known
    if (parseSTM32Line(line, s)) {
      // update lastSensor
      lastSensor = s;
      // upload sensors only (names requested)
      uploadSensorToFirebase(s);
    } else {
      Serial.println("[UART] parse failed");
    }
  }

  // Periodically read control values from Firebase (fallback if stream fails)
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll > 5000) { // poll every 5 seconds
    lastPoll = millis();
    
    if (Firebase.getJSON(fbdo, pathControl)) {
      FirebaseJson &json = fbdo.jsonObject();
      FirebaseJsonData result;
      
      int water = lastSensor.waterState;
      int light = lastSensor.lightState;
      int vent = lastSensor.ventPos;
      bool changed = false;
      
      if (json.get(result, "water")) {
        int new_water = (result.type == "bool") ? (result.boolValue ? 1 : 0) : (result.intValue ? 1 : 0);
        if (new_water != water) {
          water = new_water;
          changed = true;
          Serial.printf("[POLL] water changed to %d\n", water);
        }
      }
      
      if (json.get(result, "light")) {
        int new_light = (result.type == "bool") ? (result.boolValue ? 1 : 0) : (result.intValue ? 1 : 0);
        if (new_light != light) {
          light = new_light;
          changed = true;
          Serial.printf("[POLL] light changed to %d\n", light);
        }
      }
      
      if (json.get(result, "vent")) {
        int new_vent = result.intValue;
        if (new_vent != vent) {
          vent = new_vent;
          changed = true;
          Serial.printf("[POLL] vent changed to %d\n", vent);
        }
      }
      
      if (changed) {
        Serial.printf("[POLL] Sending to STM32: PUMP:%d,LED:%d,SERVO:%d\n", water, light, vent);
        sendCommandToSTM32(water, light, vent);
        lastSensor.waterState = water;
        lastSensor.lightState = light;
        lastSensor.ventPos = vent;
      }
    }
  }

  delay(100);
}
