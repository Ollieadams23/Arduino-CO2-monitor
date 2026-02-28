#include <Adafruit_CCS811.h>
#include <ArduinoJson.h>



// Example: Uno R4 WiFi + CCS811 sensor + WiFiS3 HTTP POST
#include <Arduino.h>
#include <Wire.h>
#include <WiFiS3.h>



#define RELAY_PIN 8
Adafruit_CCS811 sensor;

// WiFi credentials
const char* ssid = "Repeater-F058";
const char* password = "Jesus4866";

// Server details
const char* server = "192.168.1.99"; // Example: your PC's IP
const int port = 5000;
const char* path = "/sensor";

WiFiClient client;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

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
}

void loop() {
  static unsigned long lastSend = 0;
  static unsigned long lastFanCheck = 0;
  if (millis() - lastSend >= 5000) { // Send every 5 seconds
    lastSend = millis();
    if(sensor.available()){
      if(sensor.readData()==false){
        int co2ppm = sensor.geteCO2();
        int tvoc = sensor.getTVOC();
        Serial.print("CO2: "); Serial.print(co2ppm);
        Serial.print(" ppm, TVOC: "); Serial.print(tvoc); Serial.println(" ppb");
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
          digitalWrite(RELAY_PIN, HIGH);
          Serial.println("Fan set to ON by server");
        } else if (offIdx != -1) {
          digitalWrite(RELAY_PIN, LOW);
          Serial.println("Fan set to OFF by server");
        }
      }
    } else {
      Serial.println("Could not connect to server for fan state");
    }
  }
}
