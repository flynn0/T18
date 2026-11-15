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
        Serial.printf(" kreier/wifi Unified Firmware v%s (%s)\n", FIRMWARE_VERSION, _config.hw.board_name);
        Serial.printf(" Target LED: GPIO %d | Battery Pin: GPIO %d\n",
                      _config.hw.status_led_pin, _config.hw.battery_pin);
        Serial.println(F("========================================"));

        // Check if BOOT button is held for 3 seconds on boot
        bool forcePortal = checkBootButtonHold(3000);

        if (!hasConfig || forcePortal) {
            Serial.println(F("[Main] Launching setup captive portal..."));
            _portal.startPortal(_config, 60);
            _configMgr.loadConfig(_config);
            _led.setPin(_config.hw.status_led_pin);
        }

        // Attempt zero-trust token registration if needed
        ensureDeviceRegistered();
    }

    void loop() {
        uint32_t cycleStartMs = millis();
        Serial.println(F("\n>>> Starting 10-Minute Measurement Cycle <<<"));

        // 1. Maintain NTP time synchronization (re-sync every 24h) and early OTA check
        if (_ntp.shouldSync() && _config.network_count > 0) {
            connectFirstAvailableWifi();
            if (WiFi.status() == WL_CONNECTED) {
                _ntp.syncTime();

                // Self-healing: Check for OTA updates immediately on boot before long measurements
                uint64_t currentEpoch = _ntp.getCurrentEpoch();
                if (_lastOtaCheckEpoch == 0 || (currentEpoch >= _lastOtaCheckEpoch + 3600)) {
                    _lastOtaCheckEpoch = currentEpoch;
                    bool updating = OtaManager::checkForUpdate(_config.worker_url, _config.station_id,
                                                               FIRMWARE_VERSION, _config.hw);
                    if (updating) return;
                }

                WiFi.disconnect(true);
            }
        }

        // 2. Perform 2.4 GHz spectrum Wi-Fi scan survey
        _led.setSolid(true);
        uint32_t scanDurationMs = 0;
        std::vector<SurveyItem> scanResults = WifiScanner::performScan(scanDurationMs);

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

        NetworkDiagnostics dailyDiag;
        bool diagCaptured = false;

        for (uint8_t i = 0; i < _config.network_count; i++) {
            NetworkMeasurement m = LatencyProber::probeNetwork(_config.networks[i], 15000);
            measurements.push_back(m);

            if (shouldRunDaily && !diagCaptured && m.status == "connected") {
                dailyDiag = LatencyProber::runDailyDiagnostics(_config.networks[i]);
                diagCaptured = true;
                _lastDailyCheckEpoch = currentEpoch;
            }
            delay(200);
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
        cycle.has_diagnostics = diagCaptured;
        cycle.diagnostics = dailyDiag;
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
                // Buffer to raw flash
                Serial.println(F("[Main] No token; spooling to raw flash offline buffer."));
                SpoolManager::appendCycle(cycle);
                return;
            }
        }

        bool connected = connectFirstAvailableWifi();
        if (!connected) {
            Serial.println(F("[Main] Offline: No Wi-Fi available. Spooling to raw flash binary buffer."));
            SpoolManager::appendCycle(cycle);
            return;
        }

        // Check for OTA firmware updates (check every 1 hour or on initial boot)
        if (_lastOtaCheckEpoch == 0 || (currentEpoch >= _lastOtaCheckEpoch + 3600)) {
            _lastOtaCheckEpoch = currentEpoch;
            bool updating = OtaManager::checkForUpdate(_config.worker_url, _config.station_id,
                                                       FIRMWARE_VERSION, _config.hw);
            if (updating) {
                // If update is proceeding, ESP will reboot. Return here.
                return;
            }
        }

        String token = _configMgr.getDeviceToken();

        // If there is pending data from past offline cycles, upload it first
        if (SpoolManager::hasPendingData()) {
            Serial.printf("[Main] Found %u pending offline records in flash spool. Uploading backlog...\n",
                          (unsigned)SpoolManager::getRecordCount());
            String spooledBatch = SpoolManager::readNextBatchJson(_config.station_id);
            if (spooledBatch.length() > 2) {
                if (ApiClient::uploadTelemetry(_config.worker_url, token, spooledBatch)) {
                    SpoolManager::clearSpool();
                    Serial.println(F("[Main] Offline backlog successfully transmitted and cleared."));
                }
            }
        }

        // Transmit current cycle directly from RAM (ZERO FLASH WEAR)
        String currentPayload = serializeCycleJson(cycle);
        bool uploadOk = ApiClient::uploadTelemetry(_config.worker_url, token, currentPayload);

        if (uploadOk) {
            Serial.println(F("[Main] Telemetry uploaded directly from RAM. Zero flash wear."));
        } else {
            Serial.println(F("[Main] Upload failed! Writing cycle to raw flash binary buffer."));
            SpoolManager::appendCycle(cycle);
        }

        WiFi.disconnect(true);
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

        if (cycle.has_diagnostics) {
            JsonObject d = doc["diagnostics"].to<JsonObject>();
            d["ssid"] = cycle.diagnostics.ssid;
            d["gateway_ip"] = cycle.diagnostics.gateway_ip;
            d["gateway_vendor"] = cycle.diagnostics.gateway_vendor;
            d["web_exposed"] = cycle.diagnostics.web_exposed;
            d["login_url"] = cycle.diagnostics.login_url;
            d["https_redirect"] = cycle.diagnostics.https_redirect;
            d["mtu_1500_ok"] = cycle.diagnostics.mtu_1500_ok;
            d["mtu_1492_ok"] = cycle.diagnostics.mtu_1492_ok;
            d["mtu_1420_ok"] = cycle.diagnostics.mtu_1420_ok;
            d["dns_ipv4_ms"] = cycle.diagnostics.dns_ipv4_ms;
            d["dns_ipv6_ms"] = cycle.diagnostics.dns_ipv6_ms;
            d["dns_ipv6_timeout"] = cycle.diagnostics.dns_ipv6_timeout;
            d["dns_rebind_blocked"] = cycle.diagnostics.dns_rebind_blocked;
            d["captive_portal_detected"] = cycle.diagnostics.captive_portal_detected;
            d["open_ports"] = cycle.diagnostics.open_ports;
            d["details_json"] = cycle.diagnostics.details_json;
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

        if (!connectFirstAvailableWifi()) {
            Serial.println(F("[Main] Cannot register: No Wi-Fi connection available."));
            return;
        }

        _ntp.syncTime();

        String receivedToken;
        bool ok = ApiClient::registerStation(_config.worker_url, _config.station_id,
                                            _config.station_desc, _config.master_phrase,
                                            receivedToken);

        if (ok) {
            _configMgr.saveDeviceToken(receivedToken);
            _configMgr.eraseMasterPhrase(_config);
            Serial.println(F("[Main] Zero-Trust provisioning complete. Device token secured."));
        } else {
            Serial.println(F("[Main] Registration failed. Retaining MASTER_PHRASE for next cycle retry."));
        }

        WiFi.disconnect(true);
    }

    bool connectFirstAvailableWifi(uint32_t timeoutMs = 15000) {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);

        for (uint8_t i = 0; i < _config.network_count; i++) {
            const char *ssid = _config.networks[i].ssid;
            const char *pass = _config.networks[i].password;
            Serial.printf("[Main] Connecting to Wi-Fi '%s'...\n", ssid);

            if (strlen(pass) > 0) {
                WiFi.begin(ssid, pass);
            } else {
                WiFi.begin(ssid);
            }

            uint32_t start = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - start < timeoutMs)) {
                delay(200);
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[Main] Connected to '%s' (IP: %s)\n", ssid, WiFi.localIP().toString().c_str());
                return true;
            } else {
                Serial.printf("[Main] Failed to connect to '%s'\n", ssid);
                WiFi.disconnect(true);
                delay(100);
            }
        }
        return false;
    }

    void enterLightSleep(uint32_t sleepDurationMs) {
        Serial.printf("[Main] Entering Light Sleep for %u seconds...\n", (unsigned)(sleepDurationMs / 1000));
        Serial.flush();

        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _led.setOff();

        uint64_t sleepUs = ((uint64_t)sleepDurationMs) * 1000ULL;
        esp_sleep_enable_timer_wakeup(sleepUs);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

        esp_light_sleep_start();

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

