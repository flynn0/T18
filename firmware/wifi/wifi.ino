// kreier/wifi - ESP32 Wi-Fi Monitoring Node
// Arduino IDE Sketch Entry Point
// Version: 0.4.0
//
// Dual-compatible build with PlatformIO.
// Select your target board below if building in Arduino IDE:
// #define STATUS_LED_PIN 5   // T18 board (Default)
// #define STATUS_LED_PIN 2   // ESP32 DevKit V1
// #define STATUS_LED_PIN 21  // T8 board

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 5 // Default for T18
#endif

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.4.0"
#endif

#include "../include/app_main.h"

void setup() {
    app_setup();
}

void loop() {
    app_loop();
}
