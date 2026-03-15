#include <Arduino_LED_Matrix.h>
#include "font5x5.h"
// Use WiFiServer for basic web server functionality

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
char currentSsid[33] = "arduinowifi";
char currentPassword[65] = "password";
const char* defaultSsid = "arduinowifi";
const char* defaultPassword = "password";
const char* server = SERVER_IP;
const int port = SERVER_PORT;
const char* path = "/sensor";

WiFiClient client;

// Create a WiFiServer on port 80 for web page hosting
WiFiServer webServer(80);

// BLE Service and Characteristics
BLEService co2Service("180A"); // Custom service UUID
BLEIntCharacteristic co2Char("2A6E", BLERead | BLENotify); // CO2 ppm
BLEIntCharacteristic tvocChar("2A6F", BLERead | BLENotify); // TVOC ppb


ArduinoLEDMatrix matrix;

// Function prototype for updateMatrixDisplay
void updateMatrixDisplay(int co2ppm, bool fanOn);

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
  WiFi.begin(currentSsid, currentPassword);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start the web server
  webServer.begin();

  // CCS811 sensor init
  if(!sensor.begin()){
    Serial.println("Failed to start CCS811 sensor! Please check your wiring.");
    while(1);
  }
  Serial.println("Sensor ready");
  matrix.begin();
}

void loop() {
    // ...existing code...
  static int lastCO2 = 0;
  int co2ppm = 0;
  int tvoc = 0;
  static unsigned long lastSend = 0;
  static unsigned long lastFanCheck = 0;
  bool fanOn = false;

  // --- Unified web server handler ---
  WiFiClient webClient = webServer.available();
  if (webClient) {
    while (!webClient.available()) { delay(1); }
    String req = webClient.readStringUntil('\r');
    webClient.readStringUntil('\n');
    // Check all endpoints in order
    bool isWifiConfig = req.indexOf("POST /wifi") == 0;
    bool isFanOn = req.indexOf("POST /fan/on") == 0;
    bool isFanOff = req.indexOf("POST /fan/off") == 0;
    bool isRoot = req.indexOf("GET / ") == 0 || req.indexOf("GET /HTTP") == 0;
    bool isData = req.indexOf("GET /data") == 0;
    bool isFanState = req.indexOf("GET /fan/state") == 0;
    bool isWifiInfo = req.indexOf("GET /wifi/info") == 0;
    if (isWifiConfig) {
      // Read POST body for ssid and password
      String body = "";
      while (webClient.available()) {
        char c = webClient.read();
        body += c;
      }
      // Expect body as ssid=...&password=...
      int ssidIdx = body.indexOf("ssid=");
      int passIdx = body.indexOf("password=");
      if (ssidIdx >= 0 && passIdx >= 0) {
        int ssidEnd = body.indexOf('&', ssidIdx);
        String ssidVal = body.substring(ssidIdx + 5, ssidEnd > 0 ? ssidEnd : passIdx-1);
        String passVal = body.substring(passIdx + 9);
        ssidVal.trim();
        passVal.trim();
        ssidVal.toCharArray(currentSsid, sizeof(currentSsid));
        passVal.toCharArray(currentPassword, sizeof(currentPassword));
        // Attempt to reconnect
        WiFi.disconnect();
        WiFi.begin(currentSsid, currentPassword);
        webClient.println("HTTP/1.1 200 OK");
        webClient.println("Content-Type: application/json");
        webClient.println("Connection: close");
        webClient.println();
        webClient.print("{\"result\":\"ok\"}");
      } else {
        webClient.println("HTTP/1.1 400 Bad Request");
        webClient.println("Content-Type: application/json");
        webClient.println("Connection: close");
        webClient.println();
        webClient.print("{\"result\":\"error\"}");
      }
      delay(1);
      webClient.stop();
    } else if (isWifiInfo) {
      // Serve JSON with current connected SSID
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: application/json");
      webClient.println("Connection: close");
      webClient.println();
      String ssid = WiFi.SSID();
      webClient.print("{\"ssid\":\"");
      webClient.print(ssid);
      webClient.print("\"}");
      delay(1);
      webClient.stop();
    } else if (isFanOn) {
      fanMode = FAN_MANUAL_ON;
      digitalWrite(RELAY_PIN, HIGH);
      fanState = true;
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: application/json");
      webClient.println("Connection: close");
      webClient.println();
      webClient.print("{\"fan\":\"on\"}");
      delay(1);
      webClient.stop();
    } else if (isFanOff) {
      fanMode = FAN_AUTO;
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: application/json");
      webClient.println("Connection: close");
      webClient.println();
      webClient.print("{\"fan\":\"auto\"}");
      delay(1);
      webClient.stop();
    } else if (isRoot) {
      // Serve dashboard.html (embedded as a string for now)
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: text/html");
      webClient.println("Connection: close");
      webClient.println();
      webClient.println("<!DOCTYPE html><html><head><title>CO2 Sensor Dashboard</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><style>body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(120deg,#e0eafc,#cfdef3);color:#222;margin:0;padding:0;min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;}.dashboard{background:#fff;border-radius:16px;box-shadow:0 4px 24px rgba(0,0,0,0.08);padding:2rem 3rem;margin-top:2rem;text-align:center;min-width:320px;}h1{color:#2a5298;margin-bottom:1.5rem;}.reading{font-size:2.5rem;margin:1rem 0;font-weight:bold;letter-spacing:2px;}.label{font-size:1.1rem;color:#555;}.fan-btn{background:linear-gradient(90deg,#2a5298,#1e3c72);color:#fff;border:none;border-radius:8px;padding:0.7rem 2rem;margin:0.5rem 1rem;font-size:1.1rem;cursor:pointer;transition:background 0.2s,transform 0.2s;box-shadow:0 2px 8px rgba(42,82,152,0.08);}.fan-btn:hover{background:linear-gradient(90deg,#1e3c72,#2a5298);transform:translateY(-2px) scale(1.04);}.fan-state{margin-top:1.5rem;font-size:1.2rem;color:#2a5298;font-weight:500;}.wifi-form{margin-top:2rem;padding:1rem 2rem;background:#f7faff;border-radius:10px;box-shadow:0 2px 8px rgba(42,82,152,0.06);display:inline-block;}input[type=text],input[type=password]{padding:0.5rem 1rem;border-radius:6px;border:1px solid #bbb;margin:0.5rem 0;width:180px;}button.wifi-btn{background:linear-gradient(90deg,#43cea2,#185a9d);color:#fff;border:none;border-radius:8px;padding:0.6rem 1.5rem;margin:0.5rem 0;font-size:1rem;cursor:pointer;transition:background 0.2s,transform 0.2s;box-shadow:0 2px 8px rgba(42,82,152,0.08);}button.wifi-btn:hover{background:linear-gradient(90deg,#185a9d,#43cea2);transform:translateY(-2px) scale(1.04);}.wifi-ssid{margin-top:1rem;font-size:1.1rem;color:#185a9d;font-weight:500;}.footer{margin-top:2rem;color:#888;font-size:0.95rem;}</style></head><body><div class=\"dashboard\"><h1>CO₂ Sensor Dashboard</h1><div style=\"margin:2rem 0;\"><canvas id=\"co2gauge\" width=\"220\" height=\"120\"></canvas></div><div class=\"reading\"><span id=\"co2\">-</span> <span class=\"label\">ppm CO₂</span></div><div class=\"reading\"><span id=\"tvoc\">-</span> <span class=\"label\">ppb TVOC</span></div><button class=\"fan-btn\" onclick=\"fan('on')\">Fan ON</button><button class=\"fan-btn\" onclick=\"fan('off')\">Fan OFF/AUTO</button><div class=\"fan-state\">Fan State: <span id=\"fan\">-</span></div><form class=\"wifi-form\" onsubmit=\"return wifiUpdate(event)\"><h3>WiFi Config</h3><input type=\"text\" id=\"ssid\" placeholder=\"WiFi SSID\" required><br><input type=\"password\" id=\"wifipass\" placeholder=\"WiFi Password\"><br><button class=\"wifi-btn\" type=\"submit\">Update WiFi</button><span id=\"wifi-status\" style=\"margin-left:1rem;font-size:1rem;\"></span></form><div class=\"wifi-ssid\">Connected WiFi: <span id=\"current-ssid\">-</span></div></div><div class=\"footer\">Arduino CO₂ Sensor &copy; 2026</div><script>function drawGauge(value){value=Math.max(0,Math.min(2000,value));const canvas=document.getElementById('co2gauge');const ctx=canvas.getContext('2d');ctx.fillStyle='#ffff66';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.clearRect(0,0,canvas.width,canvas.height);ctx.beginPath();ctx.arc(110,110,90,Math.PI,2*Math.PI);ctx.lineWidth=20;ctx.strokeStyle=value>1000?'#ffcccc':'#eee';ctx.stroke();ctx.beginPath();ctx.arc(110,110,90,Math.PI,Math.PI+(value/2000)*Math.PI,false);ctx.lineWidth=20;ctx.strokeStyle=value>1000?'#e53935':'#2a5298';ctx.stroke();const angle=Math.PI+(value/2000)*Math.PI+(90*Math.PI/180);ctx.save();ctx.translate(110,110);ctx.rotate(angle);ctx.beginPath();ctx.moveTo(0,0);ctx.lineTo(0,-70);ctx.lineWidth=10;ctx.strokeStyle='#ff0000';ctx.stroke();ctx.restore();ctx.beginPath();ctx.arc(110,110,12,0,2*Math.PI);ctx.fillStyle='#fff';ctx.fill();ctx.strokeStyle='#888';ctx.lineWidth=2;ctx.stroke();ctx.font='20px Segoe UI';ctx.fillStyle=value>1000?'#e53935':'#2a5298';ctx.textAlign='center';ctx.fillText(value+' ppm',110,70);ctx.font='14px Segoe UI';ctx.fillStyle='#888';ctx.fillText('0',30,120);ctx.fillText('2000',190,120);}function updateData(){fetch('/data').then(r=>r.json()).then(d=>{document.getElementById('co2').textContent=d.co2;document.getElementById('tvoc').textContent=d.tvoc;drawGauge(Number(d.co2)||0);});fetch('/fan/state').then(r=>r.json()).then(d=>{document.getElementById('fan').textContent=d.fan;});fetch('/wifi/info').then(r=>r.json()).then(d=>{document.getElementById('current-ssid').textContent=d.ssid||'-';});}function fan(state){fetch('/fan/'+state,{method:'POST'}).then(updateData);}function wifiUpdate(e){e.preventDefault();var ssid=document.getElementById('ssid').value;var pass=document.getElementById('wifipass').value;var status=document.getElementById('wifi-status');status.textContent='Updating...';fetch('/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass)}).then(r=>r.json()).then(d=>{if(d.result==='ok'){status.textContent='WiFi updated!';}else{status.textContent='Update failed';}}).catch(()=>{status.textContent='Error';});}setInterval(updateData,5000);updateData();drawGauge(0);</script></body></html>");
      delay(1);
      webClient.stop();
    } else if (isData) {
      // Serve JSON with latest CO2 and TVOC values
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: application/json");
      webClient.println("Connection: close");
      webClient.println();
      webClient.print("{\"co2\":");
      webClient.print(lastCO2);
      webClient.print(",\"tvoc\":");
      webClient.print(tvoc);
      webClient.println("}");
      delay(1);
      webClient.stop();
    } else if (isFanState) {
      // Serve JSON with current fan state
      webClient.println("HTTP/1.1 200 OK");
      webClient.println("Content-Type: application/json");
      webClient.println("Connection: close");
      webClient.println();
      if (fanMode == FAN_MANUAL_ON) {
        webClient.print("{\"fan\":\"on\"}");
      } else {
        webClient.print("{\"fan\":\"auto\"}");
      }
      delay(1);
      webClient.stop();
    } else {
      // 404 Not Found
      webClient.println("HTTP/1.1 404 Not Found");
      webClient.println("Content-Type: text/plain");
      webClient.println("Connection: close");
      webClient.println();
      webClient.println("Not found");
      delay(1);
      webClient.stop();
    }

  }
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
        // Removed: HTTP POST to backend server
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
  // Removed: Check fan state from server every 5 seconds
  BLE.poll();
}
void updateMatrixDisplay(int co2ppm, bool fanOn) {
  drawFanState(matrix, fanOn ? "ON" : "OFF");
}