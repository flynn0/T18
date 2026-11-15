#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app_state.h"

class OtaManager {
public:
    static bool checkForUpdate(const String &workerUrl, const String &stationId,
                              const String &currentVersion, const HardwareProfile &hw) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[OTA] Cannot check for updates: WiFi not connected"));
            return false;
        }

        String checkEndpoint = workerUrl;
        if (checkEndpoint.endsWith("/")) checkEndpoint.remove(checkEndpoint.length() - 1);
        checkEndpoint += "/api/ota?station_id=" + stationId +
                         "&version=" + currentVersion +
                         "&board=" + String(hw.board_name);

        Serial.printf("[OTA] Checking for firmware updates at: %s\n", checkEndpoint.c_str());

        WiFiClientSecure client;
        client.setInsecure(); // Cloudflare TLS validation

        HTTPClient http;
        http.setTimeout(12000);

        if (!http.begin(client, checkEndpoint)) {
            Serial.println(F("[OTA] Failed to initialize HTTPS connection for update check"));
            return false;
        }

        int httpCode = http.GET();
        Serial.printf("[OTA] Server check response: HTTP %d\n", httpCode);

        if (httpCode == 200) {
            String payload = http.getString();
            http.end();

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload);
            if (err) {
                Serial.printf("[OTA] Failed to parse OTA response: %s\n", err.c_str());
                return false;
            }

            bool updateAvailable = doc["update_available"] | false;
            const char *targetVersion = doc["target_version"] | "";
            const char *binUrl = doc["url"] | "";

            if (updateAvailable && strlen(binUrl) > 0) {
                Serial.println(F("\n=========================================="));
                Serial.printf(" [OTA] UPDATE FOUND! Target: %s\n", targetVersion);
                Serial.printf(" [OTA] Binary URL: %s\n", binUrl);
                Serial.println(F("==========================================\n"));

                return performOtaUpdate(binUrl);
            } else {
                Serial.printf("[OTA] Up to date (Running v%s)\n", currentVersion.c_str());
            }
        } else if (httpCode == 304) {
            Serial.println(F("[OTA] Firmware is up to date (HTTP 304 Not Modified)."));
            http.end();
        } else {
            Serial.printf("[OTA] Check failed with HTTP status %d\n", httpCode);
            http.end();
        }

        return false;
    }

    static bool performOtaUpdate(const String &binaryUrl) {
        WiFiClientSecure otaClient;
        otaClient.setInsecure();

        // Automatically reboot on successful flash
        httpUpdate.rebootOnUpdate(true);

        Serial.println(F("[OTA] Streaming new firmware directly to inactive OTA flash partition..."));
        t_httpUpdate_return ret = httpUpdate.update(otaClient, binaryUrl);

        switch (ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("[OTA] ERROR: Update failed (%d): %s\n",
                              httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                return false;
            case HTTP_UPDATE_NO_UPDATES:
                Serial.println(F("[OTA] No updates available on server."));
                return false;
            case HTTP_UPDATE_OK:
                Serial.println(F("[OTA] SUCCESS! Firmware verified. Device is rebooting into new version..."));
                return true;
        }
        return false;
    }
};
