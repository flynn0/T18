#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "app_state.h"

#define CONFIG_FILE_PATH "/config.json"
#define NVS_NAMESPACE "wifi_mon"
#define NVS_KEY_TOKEN "device_token"

class AppConfigManager {
public:
    AppConfigManager() : _fsMounted(false) {}

    bool begin() {
        if (!_fsMounted) {
            _fsMounted = LittleFS.begin(true);
            if (!_fsMounted) {
                Serial.println(F("[Config] ERROR: Failed to mount LittleFS"));
                return false;
            }
        }
        return true;
    }

    bool loadConfig(AppConfig &config) {
        if (!begin()) return false;

        if (!LittleFS.exists(CONFIG_FILE_PATH)) {
            Serial.println(F("[Config] No config file found on LittleFS"));
            config.is_configured = false;
            return false;
        }

        File file = LittleFS.open(CONFIG_FILE_PATH, "r");
        if (!file) {
            Serial.println(F("[Config] Failed to open config file for reading"));
            return false;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.printf("[Config] JSON deserialize error: %s\n", error.c_str());
            return false;
        }

        strlcpy(config.station_id, doc["station_id"] | "", sizeof(config.station_id));
        strlcpy(config.station_desc, doc["station_desc"] | "", sizeof(config.station_desc));
        strlcpy(config.worker_url, doc["worker_url"] | DEFAULT_WORKER_URL, sizeof(config.worker_url));
        strlcpy(config.master_phrase, doc["master_phrase"] | "", sizeof(config.master_phrase));

        JsonArray networks = doc["networks"].as<JsonArray>();
        config.network_count = 0;
        for (JsonObject net : networks) {
            if (config.network_count >= MAX_MONITORED_NETWORKS) break;
            const char *ssid = net["ssid"] | "";
            const char *pass = net["password"] | "";
            if (strlen(ssid) > 0) {
                strlcpy(config.networks[config.network_count].ssid, ssid, sizeof(config.networks[0].ssid));
                strlcpy(config.networks[config.network_count].password, pass, sizeof(config.networks[0].password));
                config.network_count++;
            }
        }

        config.is_configured = (strlen(config.station_id) > 0 && config.network_count > 0);
        Serial.printf("[Config] Loaded station '%s' with %d networks configured\n",
                      config.station_id, config.network_count);
        return true;
    }

    bool saveConfig(const AppConfig &config) {
        if (!begin()) return false;

        JsonDocument doc;
        doc["station_id"] = config.station_id;
        doc["station_desc"] = config.station_desc;
        doc["worker_url"] = config.worker_url;
        
        // Only write master_phrase if not empty
        if (strlen(config.master_phrase) > 0) {
            doc["master_phrase"] = config.master_phrase;
        } else {
            doc["master_phrase"] = "";
        }

        JsonArray networks = doc["networks"].to<JsonArray>();
        for (uint8_t i = 0; i < config.network_count; i++) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = config.networks[i].ssid;
            net["password"] = config.networks[i].password;
        }

        File file = LittleFS.open(CONFIG_FILE_PATH, "w");
        if (!file) {
            Serial.println(F("[Config] Failed to open config file for writing"));
            return false;
        }

        if (serializeJson(doc, file) == 0) {
            Serial.println(F("[Config] Failed to write JSON to file"));
            file.close();
            return false;
        }

        file.close();
        Serial.println(F("[Config] Successfully saved configuration to LittleFS"));
        return true;
    }

    // Zero-Trust erasure of MASTER_PHRASE
    bool eraseMasterPhrase(AppConfig &config) {
        // 1. Wipe in-memory master_phrase
        memset(config.master_phrase, 0, sizeof(config.master_phrase));

        // 2. Re-save config to LittleFS without master_phrase
        bool saved = saveConfig(config);
        if (saved) {
            Serial.println(F("[Config] Zero-Trust: MASTER_PHRASE permanently erased from LittleFS and RAM"));
        }
        return saved;
    }

    // NVS Device Token Management
    bool saveDeviceToken(const String &token) {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, false)) {
            Serial.println(F("[NVS] Failed to open NVS namespace"));
            return false;
        }
        size_t written = prefs.putString(NVS_KEY_TOKEN, token);
        prefs.end();
        return written > 0;
    }

    String getDeviceToken() {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, true)) {
            return String();
        }
        String token = prefs.getString(NVS_KEY_TOKEN, "");
        prefs.end();
        return token;
    }

    bool hasDeviceToken() {
        String token = getDeviceToken();
        return token.length() > 0;
    }

    void clearDeviceToken() {
        Preferences prefs;
        if (prefs.begin(NVS_NAMESPACE, false)) {
            prefs.remove(NVS_KEY_TOKEN);
            prefs.end();
            Serial.println(F("[NVS] Cleared stored device token"));
        }
    }

private:
    bool _fsMounted;
};
