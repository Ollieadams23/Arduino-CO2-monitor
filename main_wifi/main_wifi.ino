#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Arduino_LED_Matrix.h>
#include <SensirionI2cScd4x.h>
#include <EEPROM.h>
#include <WiFiS3.h>
#include <Wire.h>

#include "config.h"
#include "font5x5.h"

#define RELAY_PIN 8
#define WIFI_CONFIG_MAGIC 0x434F3257UL
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define HISTORY_SAMPLE_INTERVAL_MS 300000UL
#define HISTORY_SAMPLE_COUNT 288
#define SENSOR_RETRY_INTERVAL_MS 5000UL

struct StoredWifiConfig {
  uint32_t magic;
  char ssid[33];
  char password[65];
};

SensirionI2cScd4x sensor;
ArduinoLEDMatrix matrix;
WiFiServer webServer(80);

BLEService co2Service("180A");
BLEIntCharacteristic co2Char("2A6E", BLERead | BLENotify);
BLEIntCharacteristic tvocChar("2A6F", BLERead | BLENotify);

enum FanMode { FAN_AUTO, FAN_MANUAL_ON };

FanMode fanMode = FAN_AUTO;
bool fanState = false;
int fanOnThreshold = 1000;
int lastCO2 = 0;
int lastTVOC = 0;
unsigned long lastSensorRead = 0;
unsigned long lastHistorySampleAt = 0;
unsigned long lastSensorRetryAt = 0;
int16_t lastSensorError = 0;
bool sensorOnline = false;

char stationSsid[33] = "";
char stationPassword[65] = "";
IPAddress apIp(192, 168, 4, 1);
IPAddress stationIp(0, 0, 0, 0);
uint16_t co2History[HISTORY_SAMPLE_COUNT] = {0};
uint16_t historyHead = 0;
uint16_t historyCount = 0;

int fanOffThreshold() {
  return fanOnThreshold - 200;
}

void tryStartSensor() {
  sensor.begin(Wire, SCD41_I2C_ADDR_62);
  lastSensorError = sensor.startPeriodicMeasurement();
  if (lastSensorError != 0) {
    sensorOnline = false;
    Serial.print("Failed to start SCD40 periodic measurement. Error: ");
    Serial.println(lastSensorError);
    Serial.println("Sensor will stay offline, but the dashboard will remain available.");
    return;
  }

  sensorOnline = true;
  lastSensorRead = 0;
  Serial.println("SCD40 sensor ready.");
}

void drawFanState(ArduinoLEDMatrix &displayMatrix, const char *state) {
  uint8_t bitmap[8][12] = {0};
  int columnIndex = 0;
  const char *text = state;
  const int yOffset = 2;

  while (*text && columnIndex <= 8) {
    int glyphIndex = font5x5_index(*text++);
    const uint8_t *glyph = font5x5[glyphIndex];
    for (int column = 0; column < 5 && columnIndex < 12; column++, columnIndex++) {
      for (int row = 0; row < 5; row++) {
        if ((glyph[column] >> row) & 0x01) {
          bitmap[row + yOffset][columnIndex] = 1;
        }
      }
    }
    columnIndex++;
  }

  displayMatrix.renderBitmap(bitmap, 8, 12);
}

void updateMatrixDisplay(int co2ppm, bool isFanOn) {
  drawFanState(matrix, isFanOn ? "ON" : "OFF");
}

bool hasConfiguredStationWifi() {
  return stationSsid[0] != '\0';
}

String wifiStatusLabel(int status) {
  switch (status) {
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_NO_SSID_AVAIL:
      return "ssid_unavailable";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_IDLE_STATUS:
      return "idle";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_AP_LISTENING:
      return "ap_listening";
    case WL_AP_CONNECTED:
      return "ap_client_connected";
    default:
      return "unknown";
  }
}

String wifiFailureReason(int status) {
  switch (status) {
    case WL_CONNECT_FAILED:
      return "Connection failed. Check password or AP compatibility.";
    case WL_NO_SSID_AVAIL:
      return "SSID not found.";
    case WL_CONNECTION_LOST:
      return "Connection lost while joining network.";
    case WL_DISCONNECTED:
      return "Disconnected before DHCP completed.";
    case WL_IDLE_STATUS:
      return "Radio stayed idle and did not complete the join attempt.";
    case WL_CONNECTED:
      return "Connected.";
    default:
      return "Unexpected WiFi status: " + String(status) + " (" + wifiStatusLabel(status) + ")";
  }
}

void logWifiFailureReason(int status) {
  Serial.print("WiFi error detail: ");
  Serial.println(wifiFailureReason(status));
}

void addHistorySample(int co2ppm) {
  int boundedValue = co2ppm;
  if (boundedValue < 0) {
    boundedValue = 0;
  }
  if (boundedValue > 65535) {
    boundedValue = 65535;
  }

  co2History[historyHead] = static_cast<uint16_t>(boundedValue);
  historyHead = (historyHead + 1) % HISTORY_SAMPLE_COUNT;
  if (historyCount < HISTORY_SAMPLE_COUNT) {
    historyCount++;
  }
}

String buildHistoryJson() {
  String body;
  body.reserve(64 + historyCount * 6);
  body = "{\"intervalMinutes\":5,\"windowHours\":24,\"count\":";
  body += String(historyCount);
  body += ",\"co2\":[";

  for (uint16_t sampleIndex = 0; sampleIndex < historyCount; sampleIndex++) {
    uint16_t ringIndex = (historyHead + HISTORY_SAMPLE_COUNT - historyCount + sampleIndex) % HISTORY_SAMPLE_COUNT;
    body += String(co2History[ringIndex]);
    if (sampleIndex + 1 < historyCount) {
      body += ',';
    }
  }

  body += "]}";
  return body;
}

String jsonEscape(const String &value) {
  String escaped = value;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");
  escaped.replace("\r", "\\r");
  return escaped;
}

String urlDecode(const String &value) {
  String decoded;
  decoded.reserve(value.length());

  for (unsigned int index = 0; index < value.length(); index++) {
    char current = value[index];
    if (current == '+') {
      decoded += ' ';
    } else if (current == '%' && index + 2 < value.length()) {
      char high = value[index + 1];
      char low = value[index + 2];
      char hex[3] = {high, low, '\0'};
      decoded += static_cast<char>(strtol(hex, nullptr, 16));
      index += 2;
    } else {
      decoded += current;
    }
  }

  return decoded;
}

String getFormValue(const String &body, const char *key) {
  String token = String(key) + "=";
  int start = body.indexOf(token);
  if (start < 0) {
    return "";
  }

  start += token.length();
  int end = body.indexOf('&', start);
  String rawValue = end >= 0 ? body.substring(start, end) : body.substring(start);
  return urlDecode(rawValue);
}

void sendResponse(WiFiClient &client, const char *contentType, const String &body, int statusCode = 200, const char *statusText = "OK") {
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.print(' ');
  client.println(statusText);
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);
}

void loadStoredWifiConfig() {
  StoredWifiConfig stored;
  EEPROM.get(0, stored);

  if (stored.magic == WIFI_CONFIG_MAGIC && stored.ssid[0] != '\0') {
    strncpy(stationSsid, stored.ssid, sizeof(stationSsid) - 1);
    stationSsid[sizeof(stationSsid) - 1] = '\0';
    strncpy(stationPassword, stored.password, sizeof(stationPassword) - 1);
    stationPassword[sizeof(stationPassword) - 1] = '\0';
  }
}

void saveStoredWifiConfig() {
  StoredWifiConfig stored = {};
  stored.magic = WIFI_CONFIG_MAGIC;
  strncpy(stored.ssid, stationSsid, sizeof(stored.ssid) - 1);
  strncpy(stored.password, stationPassword, sizeof(stored.password) - 1);
  EEPROM.put(0, stored);
}

void startAccessPoint() {
  Serial.print("Starting access point ");
  Serial.println(AP_SSID);

  if (WiFi.beginAP(AP_SSID, AP_PASSWORD) != WL_AP_LISTENING) {
    Serial.println("AP start requested, waiting for interface to become ready...");
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_AP_LISTENING && WiFi.status() != WL_AP_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("AP status: ");
  Serial.println(wifiStatusLabel(WiFi.status()));
  Serial.print("AP IP: ");
  Serial.println(apIp);
}

void returnToAccessPointMode() {
  Serial.println("Returning to AP mode.");
  WiFi.disconnect();
  delay(500);
  startAccessPoint();
  webServer.begin();
}

bool connectToConfiguredWifi() {
  if (!hasConfiguredStationWifi()) {
    Serial.println("No STA WiFi configured.");
    stationIp = IPAddress(0, 0, 0, 0);
    return false;
  }

  Serial.println("Starting STA join attempt.");
  Serial.print("Connecting STA to ");
  Serial.println(stationSsid);
  Serial.print("Password length: ");
  Serial.println(strlen(stationPassword));

  WiFi.begin(stationSsid, stationPassword);

  unsigned long start = millis();
  int lastStatus = -1;
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    int status = WiFi.status();
    if (status != lastStatus) {
      Serial.print("WiFi status changed: ");
      Serial.print(status);
      Serial.print(" (");
      Serial.print(wifiStatusLabel(status));
      Serial.println(")");
      lastStatus = status;
    }

    if (status == WL_CONNECTED) {
      stationIp = WiFi.localIP();
      if (stationIp == IPAddress(0, 0, 0, 0)) {
        Serial.println("STA connected but waiting for DHCP address...");
        delay(250);
        stationIp = WiFi.localIP();
      }

      if (stationIp == IPAddress(0, 0, 0, 0)) {
        Serial.println("STA reported connected but has no IP yet.");
        break;
      }

      Serial.print("STA connected, IP: ");
      Serial.println(stationIp);
      return true;
    }

    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      logWifiFailureReason(status);
      break;
    }

    delay(250);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("STA connect failed, status: ");
  Serial.println(wifiStatusLabel(WiFi.status()));
  logWifiFailureReason(WiFi.status());
  stationIp = IPAddress(0, 0, 0, 0);
  return false;
}

String buildWifiInfoJson() {
  String body = "{";
  body += "\"ap\":{";
  body += "\"ssid\":\"" + jsonEscape(String(AP_SSID)) + "\",";
  body += "\"ip\":\"" + apIp.toString() + "\"},";
  body += "\"station\":{";
  body += "\"configuredSsid\":\"" + jsonEscape(String(stationSsid)) + "\",";
  body += "\"connected\":";
  body += WiFi.status() == WL_CONNECTED ? "true" : "false";
  body += ",\"status\":\"" + jsonEscape(wifiStatusLabel(WiFi.status())) + "\",";
  body += "\"ip\":\"" + stationIp.toString() + "\"}}";
  return body;
}

void sendDashboardHtml(WiFiClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html"));
  client.println(F("Connection: close"));
  client.println();
  client.print(F(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CO2 Sensor Dashboard</title>
  <style>
    :root {
      color-scheme: light;
      --bg-top: #ecf7ef;
      --bg-bottom: #c7e4f3;
      --panel: rgba(255,255,255,0.92);
      --panel-border: rgba(18, 61, 88, 0.12);
      --text: #163142;
      --muted: #4d6574;
      --accent: #0f7b6c;
      --accent-strong: #0c5c78;
      --danger: #b64242;
      --shadow: 0 18px 40px rgba(17, 48, 66, 0.14);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: "Trebuchet MS", "Segoe UI", sans-serif;
      color: var(--text);
      background: radial-gradient(circle at top left, rgba(255,255,255,0.7), transparent 35%), linear-gradient(145deg, var(--bg-top), var(--bg-bottom));
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .shell {
      width: min(960px, 100%);
      background: var(--panel);
      border: 1px solid var(--panel-border);
      border-radius: 24px;
      box-shadow: var(--shadow);
      overflow: hidden;
      backdrop-filter: blur(8px);
    }
    .header {
      padding: 24px 28px 12px;
      background: linear-gradient(135deg, rgba(15,123,108,0.10), rgba(12,92,120,0.18));
    }
    .header h1 {
      margin: 0;
      font-size: clamp(1.8rem, 4vw, 2.8rem);
      letter-spacing: 0.03em;
    }
    .header p {
      margin: 8px 0 0;
      color: var(--muted);
      max-width: 680px;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 18px;
      padding: 20px;
    }
    .card {
      background: rgba(255,255,255,0.88);
      border: 1px solid rgba(18, 61, 88, 0.08);
      border-radius: 20px;
      padding: 20px;
    }
    .card.wide {
      grid-column: 1 / -1;
    }
    .card h2 {
      margin: 0 0 14px;
      font-size: 1.1rem;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .reading {
      font-size: clamp(2rem, 6vw, 3rem);
      font-weight: 700;
      margin: 10px 0;
    }
    .subtle {
      color: var(--muted);
      font-size: 0.95rem;
    }
    .actions {
      display: flex;
      gap: 12px;
      flex-wrap: wrap;
      margin-top: 16px;
    }
    button {
      border: none;
      border-radius: 999px;
      padding: 12px 18px;
      font-size: 0.95rem;
      font-weight: 700;
      color: #fff;
      cursor: pointer;
      transition: transform 0.18s ease, opacity 0.18s ease;
    }
    button:hover { transform: translateY(-1px); }
    button.primary { background: linear-gradient(135deg, var(--accent), var(--accent-strong)); }
    button.secondary { background: linear-gradient(135deg, #4f6f86, #26465e); }
    button.alert { background: linear-gradient(135deg, #d26a3f, var(--danger)); }
    form {
      display: grid;
      gap: 10px;
    }
    label {
      font-size: 0.92rem;
      font-weight: 700;
    }
    input {
      width: 100%;
      border: 1px solid rgba(18, 61, 88, 0.16);
      border-radius: 12px;
      padding: 12px 14px;
      font: inherit;
      color: var(--text);
      background: rgba(255,255,255,0.95);
    }
    .status-line {
      margin-top: 10px;
      font-size: 0.92rem;
      color: var(--muted);
      min-height: 1.2em;
    }
    .pill {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 6px 12px;
      border-radius: 999px;
      background: rgba(15,123,108,0.10);
      font-size: 0.9rem;
      font-weight: 700;
      color: var(--accent-strong);
    }
    canvas {
      width: 100%;
      max-width: 260px;
      display: block;
      margin: 0 auto;
    }
    .history-canvas {
      max-width: 100%;
      height: 220px;
      border-radius: 14px;
      background: linear-gradient(180deg, rgba(15,123,108,0.05), rgba(15,123,108,0.01));
      border: 1px solid rgba(18, 61, 88, 0.08);
    }
    .footer {
      padding: 0 20px 20px;
      color: var(--muted);
      font-size: 0.85rem;
      text-align: center;
    }
  </style>
</head>
<body>
  <main class="shell">
    <section class="header">
      <h1>CO2 Control Console</h1>
      <p>The device always boots as its own hotspot. Station Wi-Fi is only attempted when you submit credentials from this page.</p>
    </section>
    <section class="grid">
      <article class="card">
        <h2>Air Quality</h2>
        <canvas id="co2Gauge" width="260" height="150"></canvas>
        <div class="reading"><span id="co2">-</span> ppm</div>
        <div class="subtle">TVOC: <strong id="tvoc">-</strong> ppb</div>
      </article>
      <article class="card wide">
        <h2>24 Hour CO2 Trend</h2>
        <canvas class="history-canvas" id="historyChart" width="860" height="220"></canvas>
        <div class="subtle" id="historyMeta">Rolling 24-hour history using 5-minute samples. Data resets when the device restarts.</div>
      </article>
      <article class="card">
        <h2>Fan Control</h2>
        <div class="pill">Mode: <span id="fanMode">-</span></div>
        <div class="actions">
          <button class="primary" type="button" onclick="setFan('on')">Fan ON</button>
          <button class="secondary" type="button" onclick="setFan('off')">Auto Mode</button>
        </div>
        <form onsubmit="return updateThreshold(event)">
          <label for="threshold">Fan ON threshold (ppm)</label>
          <input id="threshold" type="number" min="400" max="5000" step="1" required>
          <button class="primary" type="submit">Save Threshold</button>
          <div style="color: #b64242; font-size: 0.92rem; font-weight: 700;">Safe CO2 level: under 800 ppm</div>
        </form>
        <div class="status-line" id="thresholdStatus"></div>
      </article>
      <article class="card">
        <h2>Wi-Fi</h2>
        <div class="subtle">Access point: <strong id="apSsid">-</strong> at <strong id="apIp">-</strong></div>
        <div class="subtle" style="margin-top: 6px;">Station target: <strong id="staSsid">-</strong></div>
        <div class="subtle" style="margin-top: 6px;">Station state: <strong id="staStatus">-</strong> <span id="staIpWrap">(<span id="staIp">0.0.0.0</span>)</span></div>
        <form onsubmit="return updateWifi(event)" style="margin-top: 16px;">
          <label for="ssid">Wi-Fi SSID</label>
          <input id="ssid" type="text" maxlength="32" required>
          <label for="wifipass">Wi-Fi password</label>
          <input id="wifipass" type="password" maxlength="64">
          <button class="primary" type="submit">Save And Connect</button>
        </form>
        <div class="status-line" id="wifiStatus"></div>
      </article>
      <article class="card">
        <h2>Device</h2>
        <p class="subtle">Use the hotspot if the station Wi-Fi is unavailable. The hotspot remains available after boot.</p>
        <div class="actions">
          <button class="alert" type="button" onclick="restartDevice()">Restart Device</button>
        </div>
      </article>
    </section>
    <div class="footer">Arduino CO2 Sensor dashboard</div>
  </main>
  <script>
    function drawGauge(value) {
      const safeValue = Math.max(0, Math.min(2000, Number(value) || 0));
      const canvas = document.getElementById('co2Gauge');
      const ctx = canvas.getContext('2d');
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      ctx.beginPath();
      ctx.arc(130, 130, 90, Math.PI, 0, false);
      ctx.lineWidth = 20;
      ctx.strokeStyle = '#d9e4ea';
      ctx.stroke();

      ctx.beginPath();
      ctx.arc(130, 130, 90, Math.PI, Math.PI + (safeValue / 2000) * Math.PI, false);
      ctx.lineWidth = 20;
      ctx.strokeStyle = safeValue >= 1000 ? '#b64242' : '#0f7b6c';
      ctx.stroke();

      const angle = Math.PI + (safeValue / 2000) * Math.PI + (Math.PI / 2);
      ctx.save();
      ctx.translate(130, 130);
      ctx.rotate(angle);
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.lineTo(0, -72);
      ctx.lineWidth = 8;
      ctx.strokeStyle = '#163142';
      ctx.stroke();
      ctx.restore();

      ctx.beginPath();
      ctx.arc(130, 130, 10, 0, Math.PI * 2);
      ctx.fillStyle = '#163142';
      ctx.fill();

      ctx.fillStyle = '#4d6574';
      ctx.font = '14px Trebuchet MS';
      ctx.textAlign = 'center';
      ctx.fillText('0', 38, 136);
      ctx.fillText('2000', 222, 136);
    }

    function drawHistoryChart(history) {
      const canvas = document.getElementById('historyChart');
      const meta = document.getElementById('historyMeta');
      const ctx = canvas.getContext('2d');
      const values = history && Array.isArray(history.co2) ? history.co2 : [];
      const intervalMinutes = history && history.intervalMinutes ? history.intervalMinutes : 5;
      ctx.clearRect(0, 0, canvas.width, canvas.height);

      const left = 42;
      const right = 12;
      const top = 16;
      const bottom = 30;
      const chartWidth = canvas.width - left - right;
      const chartHeight = canvas.height - top - bottom;

      ctx.strokeStyle = 'rgba(22, 49, 66, 0.12)';
      ctx.lineWidth = 1;
      for (let gridIndex = 0; gridIndex <= 4; gridIndex++) {
        const y = top + (chartHeight / 4) * gridIndex;
        ctx.beginPath();
        ctx.moveTo(left, y);
        ctx.lineTo(left + chartWidth, y);
        ctx.stroke();
      }

      ctx.fillStyle = '#4d6574';
      ctx.font = '12px Trebuchet MS';
      ctx.textAlign = 'left';
      ctx.fillText('oldest', left, canvas.height - 8);
      ctx.textAlign = 'center';
      ctx.fillText('12h', left + chartWidth / 2, canvas.height - 8);
      ctx.textAlign = 'right';
      ctx.fillText('now', left + chartWidth, canvas.height - 8);

      if (!values.length) {
        ctx.fillStyle = '#4d6574';
        ctx.textAlign = 'center';
        ctx.font = '15px Trebuchet MS';
        ctx.fillText('History will appear after the first saved samples.', canvas.width / 2, canvas.height / 2);
        meta.textContent = 'Rolling 24-hour history using 5-minute samples. Data resets when the device restarts.';
        return;
      }

      const minValue = Math.min(...values);
      const maxValue = Math.max(...values);
      const paddedMin = Math.max(350, Math.floor((minValue - 80) / 100) * 100);
      const paddedMax = Math.max(1200, Math.ceil((maxValue + 80) / 100) * 100);
      const valueRange = Math.max(100, paddedMax - paddedMin);

      ctx.fillStyle = '#4d6574';
      ctx.textAlign = 'right';
      for (let gridIndex = 0; gridIndex <= 4; gridIndex++) {
        const value = paddedMax - (valueRange / 4) * gridIndex;
        const y = top + (chartHeight / 4) * gridIndex + 4;
        ctx.fillText(String(Math.round(value)), left - 8, y);
      }

      const safeThreshold = 800;
      if (safeThreshold >= paddedMin && safeThreshold <= paddedMax) {
        const safeY = top + chartHeight - ((safeThreshold - paddedMin) / valueRange) * chartHeight;
        ctx.save();
        ctx.setLineDash([6, 4]);
        ctx.strokeStyle = '#b64242';
        ctx.beginPath();
        ctx.moveTo(left, safeY);
        ctx.lineTo(left + chartWidth, safeY);
        ctx.stroke();
        ctx.restore();
        ctx.fillStyle = '#b64242';
        ctx.textAlign = 'left';
        ctx.fillText('800 ppm safe line', left + 8, safeY - 6);
      }

      ctx.strokeStyle = '#0c5c78';
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      values.forEach((entry, index) => {
        const x = values.length === 1 ? left + chartWidth : left + (index / (values.length - 1)) * chartWidth;
        const y = top + chartHeight - ((entry - paddedMin) / valueRange) * chartHeight;
        if (index === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });
      ctx.stroke();

      const latest = values[values.length - 1];
      const coveredHours = ((values.length - 1) * intervalMinutes) / 60;
      meta.textContent = 'Rolling 24-hour history using ' + intervalMinutes + '-minute samples. Showing ' + coveredHours.toFixed(1) + ' hours, latest CO2 ' + latest + ' ppm.';
    }

    function updateHistory() {
      fetch('/history')
        .then(r => r.json())
        .then(drawHistoryChart)
        .catch(() => drawHistoryChart({ co2: [] }));
    }

    function updateData() {
      fetch('/data').then(r => r.json()).then(data => {
        document.getElementById('co2').textContent = data.co2;
        document.getElementById('tvoc').textContent = data.tvoc;
        drawGauge(data.co2);
      });

      fetch('/fan/state').then(r => r.json()).then(data => {
        document.getElementById('fanMode').textContent = data.fan;
      });

      fetch('/fan/threshold').then(r => r.json()).then(data => {
        document.getElementById('threshold').value = data.threshold;
      });

      fetch('/wifi/info').then(r => r.json()).then(data => {
        document.getElementById('apSsid').textContent = data.ap.ssid || '-';
        document.getElementById('apIp').textContent = data.ap.ip || '-';
        document.getElementById('staSsid').textContent = data.station.configuredSsid || '-';
        document.getElementById('staStatus').textContent = data.station.status || '-';
        document.getElementById('staIp').textContent = data.station.ip || '0.0.0.0';
      });
    }

    function setFan(state) {
      fetch('/fan/' + state, { method: 'POST' }).then(updateData);
    }

    function updateThreshold(event) {
      event.preventDefault();
      const status = document.getElementById('thresholdStatus');
      const threshold = document.getElementById('threshold').value;
      status.textContent = 'Saving threshold...';

      fetch('/fan/threshold', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'threshold=' + encodeURIComponent(threshold)
      })
        .then(r => r.json())
        .then(data => {
          status.textContent = data.result === 'ok' ? 'Threshold updated to ' + data.threshold + ' ppm.' : 'Threshold update failed.';
        })
        .catch(() => {
          status.textContent = 'Threshold update failed.';
        });

      return false;
    }

    function updateWifi(event) {
      event.preventDefault();
      const ssid = document.getElementById('ssid').value;
      const password = document.getElementById('wifipass').value;
      const status = document.getElementById('wifiStatus');
      status.textContent = 'Saving Wi-Fi and attempting connection...';

      fetch('/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)
      })
        .then(r => r.json())
        .then(data => {
          if (data.result === 'ok') {
            status.textContent = data.connected
              ? 'Connected to ' + data.ssid + ' at ' + data.ip
              : 'Station connection failed: ' + (data.errorReason || data.status || 'unknown error') + '. The device returned to AP mode.';
            updateData();
          } else {
            status.textContent = data.reason || 'Wi-Fi update failed.';
          }
        })
        .catch(() => {
          status.textContent = 'Wi-Fi update failed.';
        });

      return false;
    }

    function restartDevice() {
      fetch('/restart', { method: 'POST' }).then(() => {
        setTimeout(() => location.reload(), 2500);
      });
    }

    setInterval(updateData, 5000);
    setInterval(updateHistory, 60000);
    updateData();
    updateHistory();
    drawGauge(0);
  </script>
</body>
</html>
)HTML"));
}

void handleWifiUpdate(WiFiClient &client, const String &body) {
  String ssid = getFormValue(body, "ssid");
  String password = getFormValue(body, "password");
  ssid.trim();
  password.trim();

  Serial.println("Received WiFi update request from dashboard.");
  Serial.print("Requested SSID: ");
  Serial.println(ssid);
  Serial.print("Requested password length: ");
  Serial.println(password.length());

  if (ssid.length() == 0) {
    Serial.println("WiFi update rejected: SSID required.");
    sendResponse(client, "application/json", "{\"result\":\"error\",\"reason\":\"ssid required\"}", 400, "Bad Request");
    return;
  }

  ssid.toCharArray(stationSsid, sizeof(stationSsid));
  password.toCharArray(stationPassword, sizeof(stationPassword));
  saveStoredWifiConfig();

  bool connected = connectToConfiguredWifi();
  int wifiStatus = WiFi.status();
  String response = "{\"result\":\"ok\",\"ssid\":\"" + jsonEscape(ssid) + "\",\"connected\":";
  response += connected ? "true" : "false";
  if (!connected) {
    Serial.println("WiFi update failed. Falling back to AP mode.");
    returnToAccessPointMode();
  } else {
    Serial.println("WiFi update succeeded.");
  }
  response += ",\"apFallback\":";
  response += connected ? "false" : "true";
  response += ",\"statusCode\":" + String(wifiStatus);
  response += ",\"status\":\"" + jsonEscape(wifiStatusLabel(wifiStatus)) + "\"";
  response += ",\"errorReason\":\"" + jsonEscape(wifiFailureReason(wifiStatus)) + "\"";
  response += ",\"ip\":\"" + stationIp.toString() + "\"}";

  sendResponse(client, "application/json", response);
}

void handleThresholdUpdate(WiFiClient &client, const String &body) {
  String thresholdValue = getFormValue(body, "threshold");
  int newThreshold = thresholdValue.toInt();

  if (newThreshold < 400 || newThreshold > 5000) {
    sendResponse(client, "application/json", "{\"result\":\"error\",\"reason\":\"threshold out of range\"}", 400, "Bad Request");
    return;
  }

  fanOnThreshold = newThreshold;
  sendResponse(client, "application/json", "{\"result\":\"ok\",\"threshold\":" + String(fanOnThreshold) + "}");
}

void handleWebClient() {
  WiFiClient client = webServer.available();
  if (!client) {
    return;
  }

  unsigned long startedAt = millis();
  while (!client.available() && millis() - startedAt < 1000) {
    delay(1);
  }

  if (!client.available()) {
    client.stop();
    return;
  }

  String requestLine = client.readStringUntil('\r');
  client.readStringUntil('\n');
  requestLine.trim();

  int firstSpace = requestLine.indexOf(' ');
  int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  String method = firstSpace > 0 ? requestLine.substring(0, firstSpace) : "";
  String path = secondSpace > firstSpace ? requestLine.substring(firstSpace + 1, secondSpace) : "/";

  int contentLength = 0;
  while (client.connected()) {
    String headerLine = client.readStringUntil('\r');
    client.readStringUntil('\n');
    headerLine.trim();

    if (headerLine.length() == 0) {
      break;
    }

    if (headerLine.startsWith("Content-Length:")) {
      contentLength = headerLine.substring(15).toInt();
    }
  }

  String body;
  while (contentLength > 0 && millis() - startedAt < 3000) {
    while (client.available() && contentLength > 0) {
      body += static_cast<char>(client.read());
      contentLength--;
    }
    if (contentLength <= 0) {
      break;
    }
    delay(1);
  }

  if (method == "GET" && path == "/") {
    sendDashboardHtml(client);
  } else if (method == "GET" && path == "/history") {
    sendResponse(client, "application/json", buildHistoryJson());
  } else if (method == "GET" && path == "/data") {
    sendResponse(client, "application/json", "{\"co2\":" + String(lastCO2) + ",\"tvoc\":" + String(lastTVOC) + "}");
  } else if (method == "GET" && path == "/fan/state") {
    sendResponse(client, "application/json", String("{\"fan\":\"") + (fanMode == FAN_MANUAL_ON ? "on" : "auto") + "\"}");
  } else if (method == "GET" && path == "/fan/threshold") {
    sendResponse(client, "application/json", "{\"threshold\":" + String(fanOnThreshold) + "}");
  } else if (method == "GET" && path == "/wifi/info") {
    sendResponse(client, "application/json", buildWifiInfoJson());
  } else if (method == "POST" && path == "/wifi") {
    handleWifiUpdate(client, body);
  } else if (method == "POST" && path == "/fan/threshold") {
    handleThresholdUpdate(client, body);
  } else if (method == "POST" && path == "/fan/on") {
    fanMode = FAN_MANUAL_ON;
    fanState = true;
    digitalWrite(RELAY_PIN, HIGH);
    sendResponse(client, "application/json", "{\"fan\":\"on\"}");
  } else if (method == "POST" && path == "/fan/off") {
    fanMode = FAN_AUTO;
    sendResponse(client, "application/json", "{\"fan\":\"auto\"}");
  } else if (method == "POST" && path == "/restart") {
    sendResponse(client, "application/json", "{\"result\":\"restarting\"}");
    delay(100);
    client.stop();
    NVIC_SystemReset();
    return;
  } else {
    sendResponse(client, "text/plain", "Not found", 404, "Not Found");
  }

  delay(1);
  client.stop();
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadStoredWifiConfig();

  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1) {
    }
  }

  BLE.setLocalName("CO2Sensor");
  BLE.setAdvertisedService(co2Service);
  co2Service.addCharacteristic(co2Char);
  co2Service.addCharacteristic(tvocChar);
  BLE.addService(co2Service);
  co2Char.writeValue(0);
  tvocChar.writeValue(0);
  BLE.advertise();

  startAccessPoint();
  webServer.begin();

  Wire.begin();
  tryStartSensor();

  matrix.begin();
}

void loop() {
  handleWebClient();

  if (!sensorOnline && millis() - lastSensorRetryAt >= SENSOR_RETRY_INTERVAL_MS) {
    lastSensorRetryAt = millis();
    tryStartSensor();
  }

  if (sensorOnline && millis() - lastSensorRead >= 5000) {
    lastSensorRead = millis();

    bool dataReady = false;
    lastSensorError = sensor.getDataReadyStatus(dataReady);
    if (lastSensorError == 0 && dataReady) {
      uint16_t co2 = 0;
      float temperature = 0.0f;
      float humidity = 0.0f;

      lastSensorError = sensor.readMeasurement(co2, temperature, humidity);
      if (lastSensorError == 0) {
        lastCO2 = co2;
        lastTVOC = 0;

        if (lastHistorySampleAt == 0 || millis() - lastHistorySampleAt >= HISTORY_SAMPLE_INTERVAL_MS) {
          addHistorySample(lastCO2);
          lastHistorySampleAt = millis();
        }

        co2Char.writeValue(lastCO2);
        tvocChar.writeValue(lastTVOC);

        if (fanMode == FAN_MANUAL_ON) {
          fanState = true;
        } else if (fanState && lastCO2 < fanOffThreshold()) {
          fanState = false;
        } else if (!fanState && lastCO2 > fanOnThreshold) {
          fanState = true;
        }

        digitalWrite(RELAY_PIN, fanState ? HIGH : LOW);
        updateMatrixDisplay(lastCO2, fanState);

        Serial.print("CO2: ");
        Serial.print(lastCO2);
        Serial.print(" ppm, Temp: ");
        Serial.print(temperature, 1);
        Serial.print(" C, Humidity: ");
        Serial.print(humidity, 1);
        Serial.print(" %, STA status: ");
        Serial.println(wifiStatusLabel(WiFi.status()));
      } else {
        Serial.print("SCD40 readMeasurement failed. Error: ");
        Serial.println(lastSensorError);
      }
    } else if (lastSensorError != 0) {
      Serial.print("SCD40 getDataReadyStatus failed. Error: ");
      Serial.println(lastSensorError);
    }
  }

  BLE.poll();
}