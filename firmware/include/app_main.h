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

#define MEASUREMENT_INTERVAL_SEC 600 // 10 minutes

class AppMain {
public:
    AppMain() : _led(STATUS_LED_PIN), _portal(_configMgr, _led), _lastDailyCheckEpoch(0), _lastChannelAnalysisEpoch(0) {}

    void setup() {
        Serial.begin(115200);
        delay(500);

        Serial.println(F("\n========================================"));
        Serial.printf(" kreier/wifi Firmware v%s (%s)\n", FIRMWARE_VERSION, BOARD_NAME);
        Serial.printf(" Target LED Pin: GPIO %d\n", STATUS_LED_PIN);
        Serial.println(F("========================================"));

        _led.begin();
        pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

        _configMgr.begin();
        SpoolManager::begin();

        bool hasConfig = _configMgr.loadConfig(_config);

        // Check if BOOT button is held for 3 seconds on boot
        bool forcePortal = checkBootButtonHold(3000);

        if (!hasConfig || forcePortal) {
            Serial.println(F("[Main] Launching setup captive portal..."));
            _portal.startPortal(_config, 60);
            _configMgr.loadConfig(_config);
        }

        // Attempt zero-trust token registration if needed
        ensureDeviceRegistered();
    }

    void loop() {
        uint32_t cycleStartMs = millis();
        Serial.println(F("\n>>> Starting 10-Minute Measurement Cycle <<<"));

        // 1. Maintain NTP time synchronization (re-sync every 24h)
        if (_ntp.shouldSync() && _config.network_count > 0) {
            connectFirstAvailableWifi();
            if (WiFi.status() == WL_CONNECTED) {
                _ntp.syncTime();
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

        // 4. Sequentially probe configured monitored networks
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

        // 5. Package cycle telemetry and append to LittleFS spool ring buffer
        CycleTelemetry cycle;
        cycle.station_id = _config.station_id;
        cycle.timestamp = currentEpoch;
        cycle.scan_duration_ms = scanDurationMs;
        cycle.measurements = measurements;
        cycle.survey = scanResults;
        cycle.has_diagnostics = diagCaptured;
        cycle.diagnostics = dailyDiag;
        cycle.has_channel_utilization = shouldRunChannelAnalysis;
        cycle.channel_utilization = chanResults;

        SpoolManager::appendCycle(cycle);

        // 6. Connect to network and upload pending telemetry spool
        uploadSpooledData();

        // 6. Calculate sleep duration and enter ESP32 Light Sleep
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
                ensureDeviceRegistered();
            }
        }
    }

private:
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

        if (_configMgr.hasDeviceToken()) {
            Serial.println(F("[Main] Device token is already provisioned in NVS."));
            return;
        }

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
            // Save token to NVS
            _configMgr.saveDeviceToken(receivedToken);
            // Permanently erase MASTER_PHRASE from LittleFS and RAM
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

    void uploadSpooledData() {
        if (!SpoolManager::hasPendingData()) return;

        if (!_configMgr.hasDeviceToken()) {
            Serial.println(F("[Main] Cannot upload telemetry: Missing device token."));
            ensureDeviceRegistered();
            if (!_configMgr.hasDeviceToken()) return;
        }

        if (!connectFirstAvailableWifi()) {
            Serial.println(F("[Main] Cannot upload: Unable to connect to any monitored Wi-Fi network. Data retained in spool."));
            return;
        }

        String token = _configMgr.getDeviceToken();
        String payload = SpoolManager::readNextBatchJson();

        if (payload.length() > 2) {
            bool success = ApiClient::uploadTelemetry(_config.worker_url, token, payload);
            if (success) {
                SpoolManager::clearSpool();
            } else {
                Serial.println(F("[Main] Telemetry upload failed; spool preserved for next cycle."));
            }
        }

        WiFi.disconnect(true);
    }

    void enterLightSleep(uint32_t sleepDurationMs) {
        Serial.printf("[Main] Entering Light Sleep for %u seconds...\n", (unsigned)(sleepDurationMs / 1000));
        Serial.flush();

        // Cleanly shut down radio subsystems
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _led.setOff();

        // Configure timer wake-up (in microseconds)
        uint64_t sleepUs = ((uint64_t)sleepDurationMs) * 1000ULL;
        esp_sleep_enable_timer_wakeup(sleepUs);

        // Configure external wake-up on GPIO 0 (BOOT button LOW)
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

        // Enter light sleep (retains SRAM state)
        esp_light_sleep_start();

        Serial.println(F("[Main] Woke up from Light Sleep."));
    }

    AppConfig _config;
    AppConfigManager _configMgr;
    LedController _led;
    CaptivePortalManager _portal;
    NtpSyncManager _ntp;
    uint64_t _lastDailyCheckEpoch;
    uint64_t _lastChannelAnalysisEpoch;
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
