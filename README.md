# Arduino CO2 Sensor Project

This project monitors CO₂ levels using an Arduino-based sensor and provides a web dashboard for real-time data visualization and fan control. It supports both wired and WiFi-enabled sensor configurations.

## Project Structure

- `dashboard.html` — Web dashboard for displaying sensor data and controlling fan thresholds.
- `server.js` — Node.js server for handling sensor data, fan control, and serving the dashboard.
- `package.json` — Node.js project configuration and dependencies.
- `main_wifi/` — WiFi-enabled Arduino sketch and supporting files.
  - `main_wifi.ino` — Main Arduino sketch for WiFi sensor.
  - `font5x5.h`, `font5x7.h` — Font headers for display modules.
  - `lib/` — Additional libraries for the WiFi sensor.

## Features
- Real-time CO₂ and TVOC monitoring
- Web dashboard for live data and fan control
- WiFi-enabled sensor data transmission
- BLE support for local data access

## Getting Started

### Requirements
- Arduino Uno R4 WiFi (or compatible board)
- CCS811 air quality sensor (e.g., XC3782)
- Node.js (for running the server)
- WiFi network

### Setup
1. **Arduino Firmware**
   - Open `main_wifi/main_wifi.ino` in the Arduino IDE.
   - Edit `main_wifi/config.h` to set your WiFi credentials and server IP address:
     ```cpp
     #define WIFI_SSID     "your_wifi_ssid"
     #define WIFI_PASSWORD "your_wifi_password"
     #define SERVER_IP     "192.168.1.99"
     #define SERVER_PORT   5000
     ```
   - Upload to your Arduino board.
2. **Server**
   - Install dependencies: `npm install`
   - Start the server: `node server.js`
3. **Dashboard**
   - Open `dashboard.html` in your browser to view sensor data and control fan thresholds.

## Usage
- The Arduino sensor reads CO₂ levels and sends data to the server.
- The server processes incoming data and updates the dashboard in real time.
- Adjust fan ON/OFF thresholds from the dashboard to control the fan automatically.

## License
MIT License

## Author
Ollie
