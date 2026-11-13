#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include "app_state.h"

class WifiScanner {
public:
    static std::vector<SurveyItem> performScan() {
        std::vector<SurveyItem> results;
        Serial.println(F("[Scanner] Scanning 2.4 GHz Wi-Fi spectrum..."));

        // Set station mode for scanning
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);

        // Parameters: async=false, show_hidden=true, passive=false, max_ms_per_chan=300
        int16_t n = WiFi.scanNetworks(false, true);

        if (n < 0) {
            Serial.printf("[Scanner] Wi-Fi scan failed with code %d\n", n);
            return results;
        }

        Serial.printf("[Scanner] Scan complete. Found %d networks.\n", n);
        results.reserve(n);

        for (int i = 0; i < n; i++) {
            SurveyItem item;
            item.ssid = WiFi.SSID(i);
            if (item.ssid.length() == 0) {
                item.ssid = "<Hidden>";
            }
            item.bssid = WiFi.BSSIDstr(i);
            item.rssi = WiFi.RSSI(i);
            item.quality = calculateQuality(item.rssi);
            item.encryption = getEncryptionString(WiFi.encryptionType(i));
            item.channel = (uint8_t)WiFi.channel(i);
            item.band = "2.4 GHz";
            item.phy_mode = determinePhyMode(WiFi.encryptionType(i), item.channel);

            results.push_back(item);
        }

        // Free scan memory
        WiFi.scanDelete();

        return results;
    }

    static uint8_t calculateQuality(int16_t rssi) {
        if (rssi <= -100) return 0;
        if (rssi >= -50) return 100;
        return (uint8_t)(2 * (rssi + 100));
    }

    static String getEncryptionString(wifi_auth_mode_t auth) {
        switch (auth) {
            case WIFI_AUTH_OPEN:            return "Open";
            case WIFI_AUTH_WEP:             return "WEP";
            case WIFI_AUTH_WPA_PSK:         return "WPA";
            case WIFI_AUTH_WPA2_PSK:        return "WPA2";
            case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
            case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
            case WIFI_AUTH_WPA3_PSK:        return "WPA3";
            case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
            default:                        return "Unknown";
        }
    }

    static String determinePhyMode(wifi_auth_mode_t auth, uint8_t channel) {
        // Standard ESP32 is 2.4 GHz supporting 802.11b (WIFI_1), 802.11g (WIFI_3), 802.11n (WIFI_4)
        if (auth == WIFI_AUTH_WEP) {
            return "WIFI_1"; // 802.11b legacy
        }
        if (auth == WIFI_AUTH_OPEN && channel > 13) {
            return "WIFI_1";
        }
        // WPA2 and WPA3 networks on 2.4GHz almost universally support 802.11n HT20
        return "WIFI_4"; // 802.11n (Wi-Fi 4)
    }
};
