# Arduino CO₂ Sensor Project

This project monitors CO₂ levels using an Arduino-based sensor and provides a web dashboard for real-time data visualization, fan control, and WiFi configuration.

## Project Structure

- `dashboard.html` — Sample/reference dashboard HTML. **Note:** The actual dashboard is embedded as a string in `main_wifi/main_wifi.ino` and not served from this file.
- `main_wifi/` — WiFi-enabled Arduino sketch and supporting files.
  - `main_wifi.ino` — Main Arduino sketch for WiFi sensor and dashboard.
  - `font5x5.h`, `font5x7.h` — Font headers for display modules.
  - `config.h` — Default WiFi credentials.

## Features
- Real-time CO₂ and TVOC monitoring
- Web dashboard for live data, fan control, and WiFi configuration
- WiFi-enabled sensor data transmission
- BLE support for local data access

## Getting Started

### Requirements
- Arduino Uno R4 WiFi (or compatible board)
- CCS811 air quality sensor (e.g., XC3782)
- WiFi network

### Setup
1. **Arduino Firmware**
   - Open `main_wifi/main_wifi.ino` in the Arduino IDE.
   - Edit `main_wifi/config.h` to set your default WiFi credentials:
     ```cpp
     #define WIFI_SSID     "your_wifi_ssid"
     #define WIFI_PASSWORD "your_wifi_password"
     ```
   - Upload to your Arduino board.
2. **Dashboard**
   - Connect to the device's IP address in your browser (shown in Serial Monitor after upload) to view the dashboard and control the fan.
   - **Note:** The dashboard is served directly from the Arduino's firmware, not from `dashboard.html`.

## WiFi Configuration
- **Always-on access point:**
  - The device always boots its own hotspot using `AP_SSID` and `AP_PASSWORD` from `main_wifi/config.h`.
  - By default that hotspot is `arduinowifi` on `192.168.4.1`.
- **Station WiFi:**
  - The device does not auto-join any station WiFi during boot.
  - When the dashboard saves WiFi credentials, the device attempts that station connection immediately while keeping the hotspot available.
  - Saved station credentials are persisted in EEPROM for display and reuse, but boot still starts in AP mode first.
- **Changing WiFi from the dashboard:**
  - Connect to the device hotspot, open the dashboard, and submit a new SSID/password in the WiFi form.
  - The hotspot stays up while the device attempts to join the new network.
  - If the target network is unavailable, the device disconnects the failed station attempt and returns to AP mode so the dashboard remains reachable on the hotspot.

## Usage
- The Arduino sensor reads CO₂ levels and serves a web dashboard for live data and control.
- Adjust fan ON/OFF from the dashboard.
- Update WiFi credentials from the dashboard as needed.

## Notes
- No Node.js, Express, or body-parser dependencies are required. All server and dashboard functionality is handled by the Arduino firmware.
- The `package.json` file is now empty and can be deleted if not needed for other tooling.

## License
MIT License

## Author
Ollie

## Bluetooth Low Energy (BLE) Support

This device also broadcasts sensor data over Bluetooth Low Energy (BLE).

- **Device Name:** `CO2Sensor`
- **Service UUID:** `180A` (custom)
- **Characteristics:**
  - `2A6E` (CO2, ppm, readable/notify)
  - `2A6F` (TVOC, ppb, readable/notify)

### How to Connect
1. Use a BLE scanner app (such as LightBlue or nRF Connect) on your phone or tablet.
2. Scan for devices and connect to `CO2Sensor`.
3. After connecting, look for the custom service (UUID `180A`).
4. You can read the CO2 and TVOC values from the characteristics listed above.

> Note: BLE is not a serial Bluetooth connection. Use a BLE app, not a serial Bluetooth app.
