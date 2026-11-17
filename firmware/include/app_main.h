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
#include "channel_history.h"
#include "bluetooth_scanner.h"
#include "ota_manager.h"

#define MEASUREMENT_INTERVAL_SEC 600 // 10 minutes

class AppMain {
public:
    AppMain() : _led(5), _portal(_configMgr, _led), _lastDailyCheckEpoch(0),
                _lastOtaCheckEpoch(0) {}

    void setup() {
        Serial.begin(115200);
        delay(500);

        _configMgr.begin();
        ChannelHistory::instance().begin();
        SpoolManager::begin();

        _configMgr.loadConfig(_config);

        // Configure LED pin from saved hardware profile
        _led.setPin(_config.hw.status_led_pin);
        _led.begin();
        pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

        Serial.println(F("\n========================================"));
        Serial.printf(" flynn0/T18 Unified Firmware v%s (%s)\n", FIRMWARE_VERSION, _config.hw.board_name);
        Serial.printf(" Target LED: GPIO %d | Battery Pin: GPIO %d\n",
                      _config.hw.status_led_pin, _config.hw.battery_pin);
        Serial.printf(" Power Bank Keep-Alive: %s\n", _config.powerbank_keepalive ? "ENABLED (500ms/20s)" : "DISABLED");
        Serial.printf(" Backup Internet: %s (SSID: %s)\n", _config.backup_enabled ? "ENABLED" : "DISABLED", _config.backup_ssid);
        Serial.println(F("========================================"));

        // If not configured, launch captive portal immediately
        if (!_config.is_configured) {
            Serial.println(F("[Main] Device not configured. Launching setup captive portal (120s window)..."));
            _portal.startPortal(_config, 120);
            _configMgr.loadConfig(_config);
            _led.setPin(_config.hw.status_led_pin);
        }

        // Attempt zero-trust token registration if needed
        ensureDeviceRegistered();
    }

    void loop() {
        uint32_t cycleStartMs = millis();
        Serial.println(F("\n>>> Starting 10-Minute Measurement Cycle <<<"));

        // 1. Promiscuous 13-channel congestion survey (LED on per channel, 100ms off between, BOOT button monitored)
        bool bootPressed = false;
        std::vector<ChannelUtilizationItem> chanResults = ChannelAnalyzer::measureChannels(2000, &_led, &bootPressed);

        if (bootPressed) {
            Serial.println(F("[Main] BOOT button pressed during channel scan! Launching setup portal..."));
            _portal.startPortal(_config, 60);
            _configMgr.loadConfig(_config);
            _led.setPin(_config.hw.status_led_pin);
            ensureDeviceRegistered();
            return;
        }

        // 2. Perform 2.4 GHz spectrum Wi-Fi scan survey
        _led.setSolid(true);
        uint32_t scanDurationMs = 0;
        std::vector<SurveyItem> scanResults = WifiScanner::performScan(scanDurationMs);
        _led.setOff();

        // Identify visible monitored networks and backup AP from scan results
        std::vector<uint8_t> visibleMonitoredIndices;
        for (uint8_t i = 0; i < _config.network_count; i++) {
            for (const auto &item : scanResults) {
                if (item.ssid.length() > 0 && item.ssid == _config.networks[i].ssid) {
                    visibleMonitoredIndices.push_back(i);
                    break;
                }
            }
        }

        bool backupApVisible = false;
        if (_config.backup_enabled && strlen(_config.backup_ssid) > 0) {
            for (const auto &item : scanResults) {
                if (item.ssid == _config.backup_ssid) {
                    backupApVisible = true;
                    break;
                }
            }
            if (backupApVisible) {
                Serial.printf("[Main] Backup AP '%s' detected in Wi-Fi scan.\n", _config.backup_ssid);
            }
        }

        // 3. Maintain NTP time synchronization (re-sync every 24h or if uncalibrated) and early OTA check
        if (_ntp.shouldSync()) {
            bool ntpConnected = false;

            // 3a. Try visible monitored networks first
            for (uint8_t idx : visibleMonitoredIndices) {
                Serial.printf("[Main] Visible monitored AP '%s' detected. Connecting for NTP sync...\n",
                              _config.networks[idx].ssid);
                if (connectWifiSpecific(_config.networks[idx].ssid, _config.networks[idx].password, 15000)) {
                    ntpConnected = true;
                    break;
                }
            }

            // 3b. If no monitored network connected, try backup AP if visible
            if (!ntpConnected && _config.backup_enabled && backupApVisible && strlen(_config.backup_ssid) > 0) {
                Serial.printf("[Main] Monitored networks unavailable for NTP. Connecting to backup AP '%s'...\n",
                              _config.backup_ssid);
                if (connectWifiSpecific(_config.backup_ssid, _config.backup_pass, 15000)) {
                    ntpConnected = true;
                }
            }

            // 3c. If connected, perform NTP sync and early OTA check
            if (ntpConnected) {
                _ntp.syncTime();

                // Self-healing: Check for OTA updates immediately on boot before long measurements
                uint64_t ntpEpoch = _ntp.getCurrentEpoch();
                if (_lastOtaCheckEpoch == 0 || (ntpEpoch >= _lastOtaCheckEpoch + 3600)) {
                    _lastOtaCheckEpoch = ntpEpoch;
                    bool updating = OtaManager::checkForUpdate(_config.worker_url, _config.station_id,
                                                               FIRMWARE_VERSION, _config.hw);
                    if (updating) return;
                }

                WiFi.disconnect(true);
            } else {
                Serial.println(F("[Main] No visible uplink for NTP sync (monitored networks and backup AP unavailable). Proceeding with measurements."));
            }
        }
        uint64_t currentEpoch = _ntp.getCurrentEpoch();

        // 4. Read battery voltage
        uint16_t batteryMv = BatteryReader::readBatteryMilliVolts(_config.hw);
        if (batteryMv > 0) {
            Serial.printf("[Main] Battery Voltage: %u mV (Divider: %.2f)\n", batteryMv, _config.hw.battery_divider);
        }

        // 4.5. Bluetooth 2.4 GHz Scan (BLE only; legacy Bluetooth Classic removed)
        BluetoothScanResult btScan = BluetoothScanner::performScan(3);

        // Store 30-minute average channel utilization and BLE device count in 48-slot 24h history buffer
        if (!chanResults.empty() || btScan.ble_unique_devices > 0) {
            ChannelHistory::instance().recordCycle(chanResults, btScan.ble_unique_devices, currentEpoch);
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
        cycle.has_channel_utilization = !chanResults.empty();
        cycle.channel_utilization = chanResults;

        // Bluetooth telemetry
        cycle.has_bluetooth = true;
        cycle.bluetooth.scan_duration_ms = btScan.scan_duration_ms;
        cycle.bluetooth.ble_total_packets = btScan.ble_total_packets;
        cycle.bluetooth.ble_total_macs = btScan.ble_total_macs;
        cycle.bluetooth.ble_unique_devices = btScan.ble_unique_devices;
        cycle.bluetooth.ble_random_macs = btScan.ble_random_macs;
        cycle.bluetooth.ble_public_macs = btScan.ble_public_macs;
        cycle.bluetooth.ble_beacons = btScan.ble_beacons;
        cycle.bluetooth.ble_smartphones = btScan.ble_smartphones;
        cycle.bluetooth.ble_wearables = btScan.ble_wearables;
        cycle.bluetooth.ble_audio = btScan.ble_audio;
        cycle.bluetooth.ble_other = btScan.ble_other;
        for (const auto &bd : btScan.ble_devices) {
            BleDeviceTelemetry dt;
            dt.address = bd.address;
            dt.addr_type = bd.addr_type;
            dt.name = bd.name;
            dt.vendor = bd.vendor;
            dt.category = bd.category;
            dt.rssi = bd.rssi;
            dt.fingerprint = bd.fingerprint;
            cycle.bluetooth.ble_devices.push_back(dt);
        }
        cycle.bluetooth.classic_total = btScan.classic_total;
        for (const auto &cd : btScan.classic_devices) {
            ClassicBtDeviceTelemetry ct;
            ct.address = cd.address;
            ct.name = cd.name;
            ct.cod = cd.cod;
            ct.dev_class = cd.dev_class;
            ct.rssi = cd.rssi;
            cycle.bluetooth.classic_devices.push_back(ct);
        }

        // 7. Check OTA & Upload Telemetry (In-Memory-First, with backup AP failover)
        processCycleUploadAndOta(cycle, currentEpoch, visibleMonitoredIndices, backupApVisible);

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
    void processCycleUploadAndOta(CycleTelemetry &cycle, uint64_t currentEpoch,
                                  const std::vector<uint8_t> &visibleMonitoredIndices,
                                  bool backupApVisible) {
        bool uploadSuccess = false;

        // Lambda to execute upload and OTA check once connected to an AP
        auto doUpload = [&](const char *apName) -> bool {
            if (!_configMgr.hasDeviceToken()) {
                ensureDeviceRegistered();
            }

            if (!_configMgr.hasDeviceToken()) {
                return false;
            }

            // Check for OTA firmware updates (check every 1 hour or on initial boot)
            if (_lastOtaCheckEpoch == 0 || (currentEpoch >= _lastOtaCheckEpoch + 3600)) {
                _lastOtaCheckEpoch = currentEpoch;
                bool updating = OtaManager::checkForUpdate(_config.worker_url, _config.station_id,
                                                           FIRMWARE_VERSION, _config.hw);
                if (updating) return false;
            }

            String token = _configMgr.getDeviceToken();

            // 1. Submit un-transmitted 30-minute channel utilization history first
            ChannelHistory::instance().calibrateTimestamps(currentEpoch);
            ChannelHistory::instance().submitPendingHistory(_config.worker_url, token, _config.station_id);

            // 2. If there is pending data from past offline cycles, upload it first
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

            // 3. Transmit current cycle directly from RAM (ZERO FLASH WEAR)
            String currentPayload = serializeCycleJson(cycle);
            bool ok = ApiClient::uploadTelemetry(_config.worker_url, token, currentPayload);

            if (ok) {
                ChannelHistory::instance().markSlotTransmitted(currentEpoch);
                Serial.printf("[Main] Telemetry uploaded directly from RAM via '%s'.\n", apName);
            } else {
                Serial.printf("[Main] Upload failed via '%s' (no internet access).\n", apName);
            }
            return ok;
        };

        // 1. Try visible monitored networks first
        for (uint8_t idx : visibleMonitoredIndices) {
            if (connectWifiSpecific(_config.networks[idx].ssid, _config.networks[idx].password, 15000)) {
                uploadSuccess = doUpload(_config.networks[idx].ssid);
                WiFi.disconnect(true);
                if (uploadSuccess) break;
            }
        }

        // 2. Failover: If upload failed and backup AP is enabled and was seen in the scan
        if (!uploadSuccess && _config.backup_enabled && backupApVisible && strlen(_config.backup_ssid) > 0) {
            Serial.printf("[Main] Monitored networks unavailable or without internet. Attempting failover to backup AP '%s'...\n",
                          _config.backup_ssid);

            if (connectWifiSpecific(_config.backup_ssid, _config.backup_pass, 15000)) {
                Serial.printf("[Main] Connected to backup AP '%s'! Uploading telemetry...\n", _config.backup_ssid);

                if (_ntp.shouldSync()) {
                    _ntp.syncTime();
                    cycle.timestamp = _ntp.getCurrentEpoch();
                }

                uploadSuccess = doUpload(_config.backup_ssid);
                WiFi.disconnect(true);
            } else {
                Serial.printf("[Main] Failed to connect to backup AP '%s'.\n", _config.backup_ssid);
            }
        }

        // 3. Fallback: If no visible networks succeeded and backup AP was not visible, attempt blind connect to configured networks (e.g. hidden SSID)
        if (!uploadSuccess && visibleMonitoredIndices.empty() && _config.network_count > 0 && !backupApVisible) {
            Serial.println(F("[Main] No visible networks detected; attempting blind connect fallback..."));
            for (uint8_t i = 0; i < _config.network_count; i++) {
                if (connectWifiSpecific(_config.networks[i].ssid, _config.networks[i].password, 10000)) {
                    uploadSuccess = doUpload(_config.networks[i].ssid);
                    WiFi.disconnect(true);
                    if (uploadSuccess) break;
                }
            }
        }

        // 4. Spool to raw flash if all upload attempts failed
        if (!uploadSuccess) {
            Serial.println(F("[Main] Offline: All upload paths failed. Writing cycle to raw flash binary buffer."));
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

        if (cycle.has_bluetooth) {
            JsonObject bt = doc["bluetooth"].to<JsonObject>();
            bt["scan_duration_ms"] = cycle.bluetooth.scan_duration_ms;
            bt["ble_total_packets"] = cycle.bluetooth.ble_total_packets;
            bt["ble_total_macs"] = cycle.bluetooth.ble_total_macs;
            bt["ble_unique_devices"] = cycle.bluetooth.ble_unique_devices;
            bt["ble_random_macs"] = cycle.bluetooth.ble_random_macs;
            bt["ble_public_macs"] = cycle.bluetooth.ble_public_macs;

            JsonObject cat = bt["ble_categories"].to<JsonObject>();
            cat["beacons"] = cycle.bluetooth.ble_beacons;
            cat["smartphones"] = cycle.bluetooth.ble_smartphones;
            cat["wearables"] = cycle.bluetooth.ble_wearables;
            cat["audio"] = cycle.bluetooth.ble_audio;
            cat["other"] = cycle.bluetooth.ble_other;

            JsonArray bleArr = bt["ble_devices"].to<JsonArray>();
            for (const auto &bd : cycle.bluetooth.ble_devices) {
                JsonObject o = bleArr.add<JsonObject>();
                o["address"] = bd.address;
                o["addr_type"] = bd.addr_type;
                if (!bd.name.isEmpty()) o["name"] = bd.name;
                o["vendor"] = bd.vendor;
                o["category"] = bd.category;
                o["rssi"] = bd.rssi;
                if (!bd.fingerprint.isEmpty()) o["fingerprint"] = bd.fingerprint;
            }

            bt["classic_total"] = cycle.bluetooth.classic_total;
            JsonArray btArr = bt["classic_devices"].to<JsonArray>();
            for (const auto &cd : cycle.bluetooth.classic_devices) {
                JsonObject o = btArr.add<JsonObject>();
                o["address"] = cd.address;
                if (!cd.name.isEmpty()) o["name"] = cd.name;
                o["cod"] = cd.cod;
                o["dev_class"] = cd.dev_class;
                o["rssi"] = cd.rssi;
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

        bool alreadyConnected = (WiFi.status() == WL_CONNECTED);
        if (!alreadyConnected) {
            if (!connectFirstAvailableWifi()) {
                if (_config.backup_enabled && strlen(_config.backup_ssid) > 0) {
                    Serial.printf("[Main] Monitored networks failed, attempting registration via backup AP '%s'...\n", _config.backup_ssid);
                    if (!connectWifiSpecific(_config.backup_ssid, _config.backup_pass, 15000)) {
                        Serial.println(F("[Main] Cannot register: Backup AP also failed."));
                        return;
                    }
                } else {
                    Serial.println(F("[Main] Cannot register: No Wi-Fi connection available."));
                    return;
                }
            }
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

        if (!alreadyConnected) {
            WiFi.disconnect(true);
        }
    }

    bool connectWifiSpecific(const char *ssid, const char *pass, uint32_t timeoutMs = 15000) {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);

        Serial.printf("[Main] Connecting to Wi-Fi '%s'...\n", ssid);
        if (pass && strlen(pass) > 0) {
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
            return false;
        }
    }

    bool connectFirstAvailableWifi(uint32_t timeoutMs = 15000) {
        for (uint8_t i = 0; i < _config.network_count; i++) {
            if (connectWifiSpecific(_config.networks[i].ssid, _config.networks[i].password, timeoutMs)) {
                return true;
            }
        }
        return false;
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

