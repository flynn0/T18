#pragma once

#include <Arduino.h>

// Board-specific hardware pin definitions
// Target boards:
// - T18: STATUS_LED_PIN = 5
// - DevKit V1: STATUS_LED_PIN = 2
// - T8: STATUS_LED_PIN = 21

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 5 // Default fallback to T18
#endif

#ifndef BOARD_NAME
#if STATUS_LED_PIN == 5
#define BOARD_NAME "T18"
#elif STATUS_LED_PIN == 2
#define BOARD_NAME "DevKitV1"
#elif STATUS_LED_PIN == 21
#define BOARD_NAME "T8"
#else
#define BOARD_NAME "ESP32-Generic"
#endif
#endif

// ESP32 standard BOOT button is connected to GPIO 0 (Active LOW with pullup)
#define BOOT_BUTTON_PIN 0
