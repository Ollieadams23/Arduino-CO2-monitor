


// Main Arduino sketch for Uno R4 WiFi, CCS811 CO2 sensor, and XC4419/XC3782 relay
#include <Arduino.h>// Core Arduino library
#include <Wire.h>// I2C library for sensor communication   
#include <ESP32SerialCtl.h>
#include <Adafruit_CCS811.h>


#define RELAY_PIN 8 // D8 for relay control
CCS811 sensor;

ESP32SerialBT btSerial; // Bluetooth Serial object

void setup() {
    Serial.begin(115200);
    btSerial.begin("CO2Monitor"); // Bluetooth device name
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    while(sensor.begin() != 0){
        Serial.println("failed to init chip, please check if the chip connection is fine");
        delay(1000);
    }
    sensor.setMeasCycle(sensor.eCycle_250ms);
    Serial.println("Setup complete");
    btSerial.println("Bluetooth ready. Type 'FAN ON', 'FAN OFF', or 'AUTO'.");
}

// Hysteresis state for relay
bool relayOn = false;

// Bluetooth/Manual override state
enum FanMode { AUTO, ON, OFF };
FanMode fanMode = AUTO;

void loop() {
    // Handle Bluetooth commands
    if (btSerial.available()) {
        String cmd = btSerial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();
        if (cmd == "FAN ON") {
            fanMode = ON;
            btSerial.println("Fan forced ON");
        } else if (cmd == "FAN OFF") {
            fanMode = OFF;
            btSerial.println("Fan forced OFF");
        } else if (cmd == "AUTO") {
            fanMode = AUTO;
            btSerial.println("Fan set to AUTO");
        } else {
            btSerial.println("Unknown command. Use 'FAN ON', 'FAN OFF', or 'AUTO'.");
        }
    }

    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 1000) {
        lastSend = millis();
        if(sensor.checkDataReady() == true){
            int co2ppm = sensor.getCO2PPM();
            int tvoc = sensor.getTVOCPPB();
            Serial.print("CO2: ");
            Serial.print(co2ppm);
            Serial.print("ppm, TVOC: ");
            Serial.print(tvoc);
            Serial.println("ppb");

            // Send CSV data for easy graphing: time,CO2,TVOC,fan
            String data = String(millis()/1000) + "," + String(co2ppm) + "," + String(tvoc) + "," + (relayOn ? "1" : "0");
            btSerial.println(data);

            // Fan control
            if (fanMode == ON) {
                digitalWrite(RELAY_PIN, HIGH);
                relayOn = true;
            } else if (fanMode == OFF) {
                digitalWrite(RELAY_PIN, LOW);
                relayOn = false;
            } else { // AUTO
                if (!relayOn && co2ppm >= 1000) {
                    digitalWrite(RELAY_PIN, HIGH);
                    relayOn = true;
                } else if (relayOn && co2ppm <= 800) {
                    digitalWrite(RELAY_PIN, LOW);
                    relayOn = false;
                }
            }
        } else {
            Serial.println("Data is not ready!");
        }
    }
}
