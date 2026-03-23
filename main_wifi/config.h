#ifndef CONFIG_H
#define CONFIG_H

// Access point credentials served by the device on every boot.
#define AP_SSID       "arduinowifi"
#define AP_PASSWORD   "password"

// Station credentials are set from the dashboard and persisted in EEPROM.
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

// Server details
#define SERVER_IP   "192.168.1.99"   // Change to your server IP
#define SERVER_PORT 5000

#endif // CONFIG_H
