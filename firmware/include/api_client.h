#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app_state.h"

class ApiClient {
public:
    static bool registerStation(const String &workerUrl, const String &stationId,
                                const String &description, const String &bootstrapSecret,
                                String &outDeviceToken) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[API] Cannot register: WiFi not connected"));
            return false;
        }

        String endpoint = workerUrl;
        if (endpoint.endsWith("/")) endpoint.remove(endpoint.length() - 1);
        endpoint += "/register";

        Serial.printf("[API] Registering station '%s' at %s...\n", stationId.c_str(), endpoint.c_str());

        WiFiClientSecure client;
        client.setInsecure(); // Cloudflare TLS validation without storing heavy root CA bundle

        HTTPClient http;
        http.setTimeout(10000);

        if (!http.begin(client, endpoint)) {
            Serial.println(F("[API] Failed to begin HTTPS connection for registration"));
            return false;
        }

        http.addHeader("Content-Type", "application/json");

        JsonDocument reqDoc;
        reqDoc["station_id"] = stationId;
        reqDoc["description"] = description;
        reqDoc["bootstrap_secret"] = bootstrapSecret;

        String reqBody;
        serializeJson(reqDoc, reqBody);

        int httpCode = http.POST(reqBody);
        Serial.printf("[API] Registration response code: %d\n", httpCode);

        if (httpCode == 200 || httpCode == 201) {
            String respBody = http.getString();
            JsonDocument respDoc;
            DeserializationError err = deserializeJson(respDoc, respBody);
            http.end();

            if (err) {
                Serial.printf("[API] Failed to parse registration JSON: %s\n", err.c_str());
                return false;
            }

            const char *token = respDoc["device_token"] | "";
            if (strlen(token) > 0) {
                outDeviceToken = String(token);
                Serial.printf("[API] Registration SUCCESS! Received device_token: %s\n", outDeviceToken.c_str());
                return true;
            } else {
                Serial.println(F("[API] Response missing 'device_token' field"));
                return false;
            }
        } else {
            String errBody = http.getString();
            Serial.printf("[API] Registration failed (HTTP %d): %s\n", httpCode, errBody.c_str());
            http.end();
            return false;
        }
    }

    static bool uploadTelemetry(const String &workerUrl, const String &deviceToken, const String &jsonPayload) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("[API] Cannot upload telemetry: WiFi not connected"));
            return false;
        }

        String endpoint = workerUrl;
        if (endpoint.endsWith("/")) endpoint.remove(endpoint.length() - 1);
        endpoint += "/data";

        Serial.printf("[API] Uploading telemetry to %s (Payload: %u bytes)...\n",
                      endpoint.c_str(), (unsigned)jsonPayload.length());

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setTimeout(15000);

        if (!http.begin(client, endpoint)) {
            Serial.println(F("[API] Failed to begin HTTPS connection for telemetry upload"));
            return false;
        }

        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + deviceToken);

        int httpCode = http.POST(jsonPayload);
        Serial.printf("[API] Telemetry upload response code: %d\n", httpCode);

        bool success = (httpCode >= 200 && httpCode < 300);
        if (!success) {
            String resp = http.getString();
            Serial.printf("[API] Upload error (HTTP %d): %s\n", httpCode, resp.c_str());
        }

        http.end();
        return success;
    }
};
