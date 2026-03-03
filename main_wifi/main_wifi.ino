#include <Arduino_LED_Matrix.h>
#include "font5x5.h"
// Helper to draw only fan state (centered vertically)
void drawFanState(ArduinoLEDMatrix &matrix, const char *state) {
  uint8_t bitmap[8][12] = {0}; // 8 rows x 12 columns
  int colIdx = 0;
  const char *text = state;
  // Center vertically: draw in rows 2-6 (for 5x5 font)
  int yOffset = 2;
  while (*text && colIdx <= 8) {
    char c = *text++;
    int idx = font5x5_index(c);
    const uint8_t *glyph = font5x5[idx];
    for (int col = 0; col < 5 && colIdx < 12; col++, colIdx++) {
      for (int row = 0; row < 5; row++) {
        if ((glyph[col] >> row) & 0x01) {
          bitmap[row + yOffset][colIdx] = 1;
        }
      }
    }
    colIdx++; // space between chars
  }
  matrix.renderBitmap(bitmap, 8, 12);
}


#include <ArduinoBLE.h>
#include <Adafruit_CCS811.h>
#include <ArduinoJson.h>
#include "config.h"






// Example: Uno R4 WiFi + CCS811 sensor + WiFiS3 HTTP POST
#include <Arduino.h>
#include <Wire.h>
#include <WiFiS3.h>



#define RELAY_PIN 8
Adafruit_CCS811 sensor;


// WiFi credentials and server details from config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* server = SERVER_IP;
const int port = SERVER_PORT;
const char* path = "/sensor";

WiFiClient client;

// BLE Service and Characteristics
BLEService co2Service("180A"); // Custom service UUID
BLEIntCharacteristic co2Char("2A6E", BLERead | BLENotify); // CO2 ppm
BLEIntCharacteristic tvocChar("2A6F", BLERead | BLENotify); // TVOC ppb

ArduinoLEDMatrix matrix;

// Add a variable to track fan control mode
enum FanMode { FAN_AUTO, FAN_MANUAL_ON };
FanMode fanMode = FAN_AUTO;
bool fanState = false; // true = ON, false = OFF

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // BLE setup
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }
  BLE.setLocalName("CO2Sensor");
  BLE.setAdvertisedService(co2Service);
  co2Service.addCharacteristic(co2Char);
  co2Service.addCharacteristic(tvocChar);
  BLE.addService(co2Service);
  co2Char.writeValue(0);
  tvocChar.writeValue(0);
  BLE.advertise();
  Serial.println("BLE device active, waiting for connections...");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // CCS811 sensor init
  if(!sensor.begin()){
    Serial.println("Failed to start CCS811 sensor! Please check your wiring.");
    while(1);
  }
  Serial.println("Sensor ready");
  matrix.begin();
}

void loop() {
  static unsigned long lastSend = 0;
  static unsigned long lastFanCheck = 0;
  static int lastCO2 = 0;
  int co2ppm = 0;
  int tvoc = 0;
  bool fanOn = false;
  if (millis() - lastSend >= 5000) { // Send every 5 seconds
    lastSend = millis();
    if(sensor.available()){
      if(sensor.readData()==false){
        co2ppm = sensor.geteCO2();
        tvoc = sensor.getTVOC();
        Serial.print("CO2: "); Serial.print(co2ppm);
        Serial.print(" ppm, TVOC: "); Serial.print(tvoc); Serial.println(" ppb");
        // Update BLE characteristics
        co2Char.writeValue(co2ppm);
        tvocChar.writeValue(tvoc);
        // Prepare HTTP POST with JSON
        if (client.connect(server, port)) {
          StaticJsonDocument<200> doc;
          doc["co2"] = co2ppm;
          doc["tvoc"] = tvoc;
          String json;
          serializeJson(doc, json);
          client.println(String("POST ") + path + " HTTP/1.1");
          client.println(String("Host: ") + server);
          client.println("Content-Type: application/json");
          client.print("Content-Length: "); client.println(json.length());
          client.println();
          client.print(json);
          Serial.println("JSON data sent to server");
          delay(100);
          while (client.available()) {
            String line = client.readStringUntil('\n');
            Serial.println(line);
          }
          client.stop();
        } else {
          Serial.println("Connection to server failed");
        }
        // Fan control logic
        if (fanMode == FAN_MANUAL_ON) {
          digitalWrite(RELAY_PIN, HIGH);
          fanState = true;
        } else { // FAN_AUTO
          if (fanState) {
            if (co2ppm < 800) {
              digitalWrite(RELAY_PIN, LOW);
              fanState = false;
            }
          } else {
            if (co2ppm > 1000) {
              digitalWrite(RELAY_PIN, HIGH);
              fanState = true;
            }
          }
        }
        updateMatrixDisplay(co2ppm, fanState);
        lastCO2 = co2ppm;
      } else {
        Serial.println("CCS811 readData failed");
      }
    } else {
      Serial.println("CCS811 not available");
    }
  }
  // Check fan state from server every 5 seconds
  if (millis() - lastFanCheck >= 5000) {
    lastFanCheck = millis();
    if (client.connect(server, port)) {
      client.println("GET /fan/state HTTP/1.1");
      client.println(String("Host: ") + server);
      client.println("Connection: close");
      client.println();
      String response = "";
      while (client.connected() || client.available()) {
        if (client.available()) {
          String line = client.readStringUntil('\n');
          response += line;
        }
      }
      client.stop();
      // Parse fan state from response
      int idx = response.indexOf("{\"fan\":");
      if (idx != -1) {
        int onIdx = response.indexOf("on", idx);
        int offIdx = response.indexOf("off", idx);
        if (onIdx != -1 && (offIdx == -1 || onIdx < offIdx)) {
          fanMode = FAN_MANUAL_ON;
        } else if (offIdx != -1) {
          fanMode = FAN_AUTO;
        }
      }
    } else {
      Serial.println("Could not connect to server for fan state");
    }
  }
  BLE.poll();
}

void updateMatrixDisplay(int co2ppm, bool fanOn) {
  drawFanState(matrix, fanOn ? "ON" : "OFF");
}
