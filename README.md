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
- **Default WiFi:**
  - The device starts up using the WiFi SSID and password defined in `main_wifi/config.h`.
- **Changing WiFi:**
  - On the dashboard, use the WiFi Config form to enter a new SSID and password, then click "Update WiFi".
  - The device will attempt to connect to the new WiFi immediately. The new credentials are stored in RAM only.
- **Reverting to Default:**
  - If the device is powered off or reset, it will revert to the default WiFi credentials from `config.h` on the next boot.
  - This ensures you can always recover the device by rebooting it within range of the default WiFi network.

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
