#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include "board_pins.h"
#include "app_state.h"
#include "led_controller.h"
#include "app_config.h"
#include "captive_portal.h"
#include "ntp_sync.h"
#include "wifi_scanner.h"
#include "latency_prober.h"
#include "spool_manager.h"
#include "api_client.h"
#include "channel_analyzer.h"
#include "ota_manager.h"

#define MEASUREMENT_INTERVAL_SEC 600 // 10 minutes

class AppMain {
public:
    AppMain() : _led(5), _portal(_configMgr, _led), _lastDailyCheckEpoch(0),
                _lastChannelAnalysisEpoch(0), _lastOtaCheckEpoch(0) {}

    void setup() {
        Serial.begin(115200);
        delay(500);

        _configMgr.begin();
        SpoolManager::begin();

        bool hasConfig = _configMgr.loadConfig(_config);

        // Configure LED pin from saved hardware profile
        _led.setPin(_config.hw.status_led_pin);
        _led.begin();
        pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

        Serial.println(F("\n========================================"));
        Serial.printf(" kreier/T18 Unified Firmware v%s (%s)\n", FIRMWARE_VERSION, _config.hw.board_name);
        Serial.printf(" Target LED: GPIO %d | Battery Pin: GPIO %d\n",
                      _config.hw.status_led_pin, _config.hw.battery_pin);
        Serial.printf(" Power Bank Keep-Alive: %s\n", _config.powerbank_keepalive ? "ENABLED (500ms/20s)" : "DISABLED");
        Serial.printf(" Fallback Network: %s ('%s')\n",
                      _config.fallback_enabled ? "ENABLED" : "DISABLED", _config.fallback_network.ssid);
        Serial.println(F("========================================"));

        // Always launch the 60-second captive portal on power-on / reset
        // If a client connects to T18-Mon-XXXX, timeout pauses so the user can reconfigure station name/SSIDs.
        // If no client connects within 60s (or user taps BOOT button), it automatically proceeds to monitoring.
        Serial.println(F("[Main] Launching setup captive portal (60s window, or press BOOT to skip)..."));
        _portal.startPortal(_config, 60);
        _configMgr.loadConfig(_config);
        _led.setPin(_config.hw.status_led_pin);

        // Attempt zero-trust token registration if needed
        ensureDeviceRegistered();
    }

    void loop() {
        uint32_t cycleStartMs = millis();
        Serial.println(F("\n>>> Starting 10-Minute Measurement Cycle <<<"));

        // 1. Maintain NTP time synchronization (re-sync every 24h) and early OTA check
        if (_ntp.shouldSync()) {
            syncNtpAndCheckOta();
        }

        // 2. Perform 2.4 GHz spectrum Wi-Fi scan survey
        _led.setSolid(true);
        uint32_t scanDurationMs = 0;
        std::vector<SurveyItem> scanResults = WifiScanner::performScan(scanDurationMs);

        // 3. Post-sleep unuploaded data check:
        // If recent data has not been uploaded, look for the fallback network in scan results and upload old data before the measurement cycle
        if (SpoolManager::hasPendingData() && _config.fallback_enabled && strlen(_config.fallback_network.ssid) > 0) {
            bool fallbackSeen = false;
            for (const auto &item : scanResults) {
                if (item.ssid.equalsIgnoreCase(_config.fallback_network.ssid)) {
                    fallbackSeen = true;
                    break;
                }
            }

            if (fallbackSeen) {
                Serial.printf("[Main] Unuploaded data detected (%u records) and fallback network '%s' found in scan survey!\n",
                              (unsigned)SpoolManager::getRecordCount(), _config.fallback_network.ssid);
                Serial.printf("[Main] Connecting to '%s' to upload backlog before measurement cycle...\n",
                              _config.fallback_network.ssid);
                if (connectWifi(_config.fallback_network)) {
                    String token = _configMgr.getDeviceToken();
                    if (token.length() > 0) {
                        uploadBacklogIfPending(token);
                    }
                    WiFi.disconnect(true);
                }
            } else {
                Serial.printf("[Main] Note: %u unuploaded records pending in spool, but fallback network '%s' not visible in scan.\n",
                              (unsigned)SpoolManager::getRecordCount(), _config.fallback_network.ssid);
            }
        }

        // 3. Promiscuous mode 13-channel congestion analysis (every 30 minutes / 1800s)
        uint64_t currentEpoch = _ntp.getCurrentEpoch();
        bool shouldRunChannelAnalysis = false;
        if (_lastChannelAnalysisEpoch == 0 || (currentEpoch >= _lastChannelAnalysisEpoch + 1800)) {
            shouldRunChannelAnalysis = true;
        }

        std::vector<ChannelUtilizationItem> chanResults;
        if (shouldRunChannelAnalysis) {
            chanResults = ChannelAnalyzer::measureChannels(2000);
            _lastChannelAnalysisEpoch = currentEpoch;
        }

        // 4. Read battery voltage
        uint16_t batteryMv = BatteryReader::readBatteryMilliVolts(_config.hw);
        if (batteryMv > 0) {
            Serial.printf("[Main] Battery Voltage: %u mV (Divider: %.2f)\n", batteryMv, _config.hw.battery_divider);
        }

        // 5. Sequentially probe configured monitored networks
        std::vector<NetworkMeasurement> measurements;
        bool shouldRunDaily = false;
        if (_lastDailyCheckEpoch == 0 || (currentEpoch > _lastDailyCheckEpoch + 86400)) {
            shouldRunDaily = true;
        }

        std::vector<NetworkDiagnostics> dailyDiags;

        for (uint8_t i = 0; i < _config.network_count; i++) {
            NetworkMeasurement m = LatencyProber::probeNetwork(_config.networks[i], 15000);
            measurements.push_back(m);

            if (shouldRunDaily && m.status == "connected") {
                dailyDiags.push_back(LatencyProber::runDailyDiagnostics(_config.networks[i]));
            }
            delay(200);
        }
        if (shouldRunDaily && !dailyDiags.empty()) {
            _lastDailyCheckEpoch = currentEpoch;
        }
        _led.setOff();

        // 6. Package cycle telemetry
        CycleTelemetry cycle;
        cycle.station_id = _config.station_id;
        cycle.timestamp = currentEpoch;
        cycle.scan_duration_ms = scanDurationMs;
        cycle.battery_mv = batteryMv;
        cycle.board_type = _config.hw.board_name;
        cycle.measurements = measurements;
        cycle.survey = scanResults;
        cycle.has_diagnostics = !dailyDiags.empty();
        cycle.diagnostics = dailyDiags;
        cycle.has_channel_utilization = shouldRunChannelAnalysis;
        cycle.channel_utilization = chanResults;

        // 7. Check OTA & Upload Telemetry (In-Memory-First)
        processCycleUploadAndOta(cycle, currentEpoch);

        // 8. Calculate sleep duration and enter ESP32 Light Sleep
        uint32_t elapsedMs = millis() - cycleStartMs;
        uint32_t sleepMs = 10000; // minimum 10s fallback
        if (elapsedMs < (MEASUREMENT_INTERVAL_SEC * 1000)) {
            sleepMs = (MEASUREMENT_INTERVAL_SEC * 1000) - elapsedMs;
        }

        enterLightSleep(sleepMs);

        // On wake: check if woke up by BOOT button
        if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
            Serial.println(F("[Main] Woken up by BOOT button!"));
            if (checkBootButtonHold(2500)) {
                Serial.println(F("[Main] BOOT button held! Launching captive portal..."));
                _portal.startPortal(_config, 60);
                _configMgr.loadConfig(_config);
                _led.setPin(_config.hw.status_led_pin);
                ensureDeviceRegistered();
            }
        }
    }

private:
    void processCycleUploadAndOta(const CycleTelemetry &cycle, uint64_t currentEpoch) {
        if (!_configMgr.hasDeviceToken()) {
            Serial.println(F("[Main] Missing device token. Attempting registration..."));
            ensureDeviceRegistered();
            if (!_configMgr.hasDeviceToken()) {
                Serial.println(F("[Main] No token; spooling to raw flash offline buffer."));
                SpoolManager::appendCycle(cycle);
                return;
            }
        }

        String token = _configMgr.getDeviceToken();
        String currentPayload = serializeCycleJson(cycle);

        bool uploadSucceeded = false;

        // 1. Sequentially try each monitored network (Network 1, Network 2, Network 3...)
        for (uint8_t i = 0; i < _config.network_count; i++) {
            Serial.printf("[Main] Attempting cycle upload via monitored network %d/%d ('%s')...\n",
                          i + 1, _config.network_count, _config.networks[i].ssid);
            if (connectWifi(_config.networks[i])) {
                checkOtaIfDue(currentEpoch);
                uploadBacklogIfPending(token);

                if (ApiClient::uploadTelemetry(_config.worker_url, token, currentPayload)) {
                    Serial.printf("[Main] Telemetry successfully uploaded via '%s' (RAM fast path, zero flash wear).\n",
                                  _config.networks[i].ssid);
                    uploadSucceeded = true;
                    WiFi.disconnect(true);
                    break;
                } else {
                    Serial.printf("[Main] Network '%s' connected to AP but failed internet upload (no route/WAN issue). Trying next network...\n",
                                  _config.networks[i].ssid);
                    WiFi.disconnect(true);
                }
            } else {
                Serial.printf("[Main] Could not associate with '%s'. Trying next network...\n",
                              _config.networks[i].ssid);
            }
        }

        // 2. In case the monitored networks have problems with the internet connection, again the backup network is used
        if (!uploadSucceeded && _config.fallback_enabled && strlen(_config.fallback_network.ssid) > 0) {
            Serial.printf("[Main] All monitored networks failed internet upload. Falling back to backup network '%s'...\n",
                          _config.fallback_network.ssid);
            if (connectWifi(_config.fallback_network)) {
                checkOtaIfDue(currentEpoch);
                uploadBacklogIfPending(token);

                if (ApiClient::uploadTelemetry(_config.worker_url, token, currentPayload)) {
                    Serial.printf("[Main] Telemetry successfully uploaded via fallback backup network '%s'!\n",
                                  _config.fallback_network.ssid);
                    uploadSucceeded = true;
                } else {
                    Serial.printf("[Main] Fallback network '%s' connected but upload failed (no internet access).\n",
                                  _config.fallback_network.ssid);
                }
                WiFi.disconnect(true);
            } else {
                Serial.printf("[Main] Failed to associate with fallback network '%s'.\n",
                              _config.fallback_network.ssid);
            }
        }

        // 3. Spool to raw flash circular buffer if all upload options failed
        if (!uploadSucceeded) {
            Serial.println(F("[Main] All networks offline or without internet access! Spooling cycle to raw flash binary buffer."));
            SpoolManager::appendCycle(cycle);
        }
    }

    String serializeCycleJson(const CycleTelemetry &cycle) {
        JsonDocument doc;
        doc["station_id"] = cycle.station_id;
        doc["timestamp"] = cycle.timestamp;
        doc["scan_duration_ms"] = cycle.scan_duration_ms;
        if (cycle.battery_mv > 0) doc["battery_mv"] = cycle.battery_mv;
        doc["board_type"] = cycle.board_type;
        doc["firmware_version"] = FIRMWARE_VERSION;
        if (strlen(_config.station_desc) > 0) doc["description"] = _config.station_desc;

        JsonArray arrNet = doc["networks"].to<JsonArray>();
        for (const auto &net : cycle.measurements) {
            JsonObject o = arrNet.add<JsonObject>();
            o["ssid"] = net.ssid;
            o["bssid"] = net.bssid;
            o["channel"] = net.channel;
            o["assoc_ms"] = net.assoc_ms;
            o["dhcp_ms"] = net.dhcp_ms;
            o["gateway_ms"] = net.gateway_ms;
            o["dns_ipv4_ms"] = net.dns_ipv4_ms;
            o["dns_ipv6_ms"] = net.dns_ipv6_ms;
            o["captive_detected"] = net.captive_detected;

            JsonObject lat = o["http_latencies"].to<JsonObject>();
            lat["google"] = net.google_ms;
            lat["cloudflare"] = net.cloudflare_ms;
            lat["microsoft"] = net.microsoft_ms;
            lat["facebook"] = net.facebook_ms;
            lat["netflix"] = net.netflix_ms;

            o["status"] = net.status;
        }

        JsonArray arrSurvey = doc["scan_survey"].to<JsonArray>();
        for (const auto &item : cycle.survey) {
            JsonObject s = arrSurvey.add<JsonObject>();
            s["ssid"] = item.ssid;
            s["bssid"] = item.bssid;
            s["rssi"] = item.rssi;
            s["quality"] = item.quality;
            s["encryption"] = item.encryption;
            s["phy_mode"] = item.phy_mode;
            s["channel"] = item.channel;
            s["band"] = item.band;
        }

        if (cycle.has_diagnostics && !cycle.diagnostics.empty()) {
            JsonArray arrDiag = doc["diagnostics"].to<JsonArray>();
            for (const auto &dItem : cycle.diagnostics) {
                JsonObject d = arrDiag.add<JsonObject>();
                d["ssid"] = dItem.ssid;
                d["gateway_ip"] = dItem.gateway_ip;
                d["gateway_vendor"] = dItem.gateway_vendor;
                d["web_exposed"] = dItem.web_exposed;
                d["login_url"] = dItem.login_url;
                d["https_redirect"] = dItem.https_redirect;
                d["mtu_1500_ok"] = dItem.mtu_1500_ok;
                d["mtu_1492_ok"] = dItem.mtu_1492_ok;
                d["mtu_1420_ok"] = dItem.mtu_1420_ok;
                d["dns_ipv4_ms"] = dItem.dns_ipv4_ms;
                d["dns_ipv6_ms"] = dItem.dns_ipv6_ms;
                d["dns_ipv6_timeout"] = dItem.dns_ipv6_timeout;
                d["dns_rebind_blocked"] = dItem.dns_rebind_blocked;
                d["captive_portal_detected"] = dItem.captive_portal_detected;
                d["open_ports"] = dItem.open_ports;
                d["details_json"] = dItem.details_json;
            }
        }

        if (cycle.has_channel_utilization) {
            JsonArray arrChan = doc["channel_utilization"].to<JsonArray>();
            for (const auto &item : cycle.channel_utilization) {
                JsonObject co = arrChan.add<JsonObject>();
                co["channel"] = item.channel;
                co["mgmt_frames"] = item.mgmt_frames;
                co["data_frames"] = item.data_frames;
                co["ctrl_frames"] = item.ctrl_frames;
                JsonArray rh = co["rssi_hist"].to<JsonArray>();
                rh.add(item.rssi_hist[0]);
                rh.add(item.rssi_hist[1]);
                rh.add(item.rssi_hist[2]);
                rh.add(item.rssi_hist[3]);
                co["unique_transmitters"] = item.unique_transmitters;
                co["airtime_pct"] = round(item.airtime_pct * 10.0f) / 10.0f;
            }
        }

        String jsonPayload;
        serializeJson(doc, jsonPayload);
        return jsonPayload;
    }

    bool checkBootButtonHold(uint32_t holdTimeMs) {
        if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
            return false;
        }

        Serial.printf("[Main] Checking BOOT button hold (%u ms)...\n", (unsigned)holdTimeMs);
        uint32_t start = millis();
        while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            if (millis() - start >= holdTimeMs) {
                Serial.println(F("[Main] BOOT button hold confirmed!"));
                return true;
            }
            delay(50);
        }
        return false;
    }

    void ensureDeviceRegistered() {
        if (!_config.is_configured) return;
        if (_configMgr.hasDeviceToken()) return;

        if (strlen(_config.master_phrase) == 0) {
            Serial.println(F("[Main] No device token and no MASTER_PHRASE present. Re-run portal to configure."));
            return;
        }

        Serial.println(F("[Main] Starting Zero-Trust token registration..."));

        bool registered = false;
        String receivedToken;

        // Try monitored networks
        for (uint8_t i = 0; i < _config.network_count; i++) {
            if (connectWifi(_config.networks[i])) {
                _ntp.syncTime();
                if (ApiClient::registerStation(_config.worker_url, _config.station_id,
                                               _config.station_desc, _config.master_phrase,
                                               receivedToken)) {
                    registered = true;
                    WiFi.disconnect(true);
                    break;
                }
                WiFi.disconnect(true);
            }
        }

        // Try fallback network if monitored networks failed
        if (!registered && _config.fallback_enabled && strlen(_config.fallback_network.ssid) > 0) {
            Serial.printf("[Main] Monitored networks failed registration. Trying fallback network '%s'...\n",
                          _config.fallback_network.ssid);
            if (connectWifi(_config.fallback_network)) {
                _ntp.syncTime();
                if (ApiClient::registerStation(_config.worker_url, _config.station_id,
                                               _config.station_desc, _config.master_phrase,
                                               receivedToken)) {
                    registered = true;
                }
                WiFi.disconnect(true);
            }
        }

        if (registered) {
            _configMgr.saveDeviceToken(receivedToken);
            _configMgr.eraseMasterPhrase(_config);
            Serial.println(F("[Main] Zero-Trust provisioning complete. Device token secured."));
        } else {
            Serial.println(F("[Main] Registration failed. Retaining MASTER_PHRASE for next cycle retry."));
        }
    }

    bool connectWifi(const WifiNetworkConfig &net, uint32_t timeoutMs = 15000) {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);

        Serial.printf("[Main] Connecting to Wi-Fi '%s'...\n", net.ssid);
        if (strlen(net.password) > 0) {
            WiFi.begin(net.ssid, net.password);
        } else {
            WiFi.begin(net.ssid);
        }

        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutMs)) {
            delay(200);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[Main] Connected to '%s' (IP: %s)\n", net.ssid, WiFi.localIP().toString().c_str());
            return true;
        } else {
            Serial.printf("[Main] Failed to connect to '%s'\n", net.ssid);
            WiFi.disconnect(true);
            delay(100);
            return false;
        }
    }

    bool syncNtpAndCheckOta() {
        for (uint8_t i = 0; i < _config.network_count; i++) {
            if (connectWifi(_config.networks[i])) {
                if (_ntp.syncTime()) {
                    checkOtaIfDue(_ntp.getCurrentEpoch());
                    WiFi.disconnect(true);
                    return true;
                }
                WiFi.disconnect(true);
            }
        }
        if (_config.fallback_enabled && strlen(_config.fallback_network.ssid) > 0) {
            if (connectWifi(_config.fallback_network)) {
                if (_ntp.syncTime()) {
                    checkOtaIfDue(_ntp.getCurrentEpoch());
                    WiFi.disconnect(true);
                    return true;
                }
                WiFi.disconnect(true);
            }
        }
        return false;
    }

    void checkOtaIfDue(uint64_t currentEpoch) {
        if (_lastOtaCheckEpoch == 0 || (currentEpoch >= _lastOtaCheckEpoch + 3600)) {
            _lastOtaCheckEpoch = currentEpoch;
            OtaManager::checkForUpdate(_config.worker_url, _config.station_id,
                                       FIRMWARE_VERSION, _config.hw);
        }
    }

    void uploadBacklogIfPending(const String &token) {
        if (!SpoolManager::hasPendingData()) return;

        Serial.printf("[Main] Found %u pending offline records in flash spool. Uploading backlog...\n",
                      (unsigned)SpoolManager::getRecordCount());
        String spooledBatch = SpoolManager::readNextBatchJson(_config.station_id);
        if (spooledBatch.length() > 2) {
            if (ApiClient::uploadTelemetry(_config.worker_url, token, spooledBatch)) {
                SpoolManager::clearSpool();
                Serial.println(F("[Main] Offline backlog successfully transmitted and cleared."));
            } else {
                Serial.println(F("[Main] Backlog upload failed on current connection."));
            }
        }
    }

    void enterLightSleep(uint32_t sleepDurationMs) {
        Serial.printf("[Main] Entering Light Sleep for %u seconds (Power Bank Keep-Alive: %s)...\n",
                      (unsigned)(sleepDurationMs / 1000),
                      _config.powerbank_keepalive ? "ENABLED (500ms pulse every 20s)" : "DISABLED");
        Serial.flush();

        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _led.setOff();

        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

        if (!_config.powerbank_keepalive) {
            uint64_t sleepUs = ((uint64_t)sleepDurationMs) * 1000ULL;
            esp_sleep_enable_timer_wakeup(sleepUs);
            esp_light_sleep_start();
        } else {
            uint32_t remainingMs = sleepDurationMs;
            const uint32_t INTERVAL_SLEEP_MS = 20000; // 20 seconds
            const uint32_t WAKE_PULSE_MS = 500;       // 500 ms

            while (remainingMs > 0) {
                uint32_t chunkSleepMs = (remainingMs > INTERVAL_SLEEP_MS) ? INTERVAL_SLEEP_MS : remainingMs;
                uint64_t chunkSleepUs = ((uint64_t)chunkSleepMs) * 1000ULL;
                esp_sleep_enable_timer_wakeup(chunkSleepUs);
                esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

                esp_light_sleep_start();

                // If woken up by BOOT button, exit sleep immediately
                if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
                    break;
                }

                if (remainingMs > chunkSleepMs) {
                    remainingMs -= chunkSleepMs;
                } else {
                    break;
                }

                // Keep-alive pulse: wake for 500 ms with active current draw to keep power bank awake
                _led.setSolid(true);
                delay(WAKE_PULSE_MS);
                _led.setOff();

                if (remainingMs > WAKE_PULSE_MS) {
                    remainingMs -= WAKE_PULSE_MS;
                } else {
                    break;
                }

                // If BOOT button is pressed during pulse, break immediately
                if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
                    break;
                }
            }
        }

        Serial.println(F("[Main] Woke up from Light Sleep."));
    }

    AppConfigManager _configMgr;
    AppConfig _config;
    LedController _led;
    CaptivePortalManager _portal;
    NtpSyncManager _ntp;
    uint64_t _lastDailyCheckEpoch;
    uint64_t _lastChannelAnalysisEpoch;
    uint64_t _lastOtaCheckEpoch;
};

// Global singleton helper for setup & loop
inline AppMain& getAppInstance() {
    static AppMain instance;
    return instance;
}

inline void app_setup() {
    getAppInstance().setup();
}

inline void app_loop() {
    getAppInstance().loop();
}

