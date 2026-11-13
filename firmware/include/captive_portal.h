#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "app_state.h"
#include "app_config.h"
#include "led_controller.h"

class CaptivePortalManager {
public:
    CaptivePortalManager(AppConfigManager &configMgr, LedController &led)
        : _configMgr(configMgr), _led(led), _server(80), _clientConnected(false),
          _portalDone(false), _apStartTime(0), _timeoutPaused(false) {}

    bool startPortal(AppConfig &config, uint32_t timeoutSeconds = 60) {
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.mode(WIFI_AP);

        // Derive SSID from efuse MAC
        uint64_t chipId = ESP.getEfuseMac();
        char apSsid[32];
        snprintf(apSsid, sizeof(apSsid), "WIFI-Mon-%04X", (uint16_t)(chipId & 0xFFFF));

        IPAddress apIP(192, 168, 4, 1);
        IPAddress netMask(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apIP, netMask);
        WiFi.softAP(apSsid);

        Serial.printf("[Portal] Started Soft-AP '%s' at IP: %s\n", apSsid, WiFi.softAPIP().toString().c_str());

        // DNS Server for captive redirection
        _dnsServer.start(53, "*", apIP);

        // Set LED to 2 Hz blink while waiting for client
        _led.setBlinkRateHz(2.0f);

        _clientConnected = false;
        _timeoutPaused = false;
        _portalDone = false;
        _apStartTime = millis();
        _timeoutMs = timeoutSeconds * 1000;

        setupWebServer(config);
        _server.begin();
        Serial.println(F("[Portal] Web server listening on port 80"));

        // Event loop
        while (!_portalDone) {
            _dnsServer.processNextRequest();
            _server.handleClient();
            _led.update();

            // Check if any station connected to Soft-AP
            int stationNum = WiFi.softAPgetStationNum();
            if (stationNum > 0 && !_clientConnected) {
                _clientConnected = true;
                _timeoutPaused = true;
                _led.setSolid(true); // Switch to solid ON
                Serial.printf("[Portal] Client connected! (%d active). Paused AP timeout.\n", stationNum);
            }

            // Check timeout if no client has connected yet
            if (!_timeoutPaused) {
                if (millis() - _apStartTime >= _timeoutMs) {
                    Serial.println(F("[Portal] Timeout reached without client connection. Exiting AP mode."));
                    break;
                }
            }

            delay(10);
        }

        // Cleanup
        _server.stop();
        _dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        _led.setOff();
        Serial.println(F("[Portal] Soft-AP mode stopped."));

        return _portalDone;
    }

private:
    void setupWebServer(AppConfig &config) {
        // Captive portal detection endpoints
        auto redirectHandler = [this]() {
            _server.sendHeader("Location", "http://192.168.4.1/", true);
            _server.send(302, "text/plain", "");
        };

        _server.on("/generate_204", redirectHandler);
        _server.on("/hotspot-detect.html", redirectHandler);
        _server.on("/canonical.html", redirectHandler);
        _server.on("/connecttest.txt", redirectHandler);
        _server.on("/ncsi.txt", redirectHandler);

        // Portal Root UI
        _server.on("/", HTTP_GET, [this, &config]() {
            _server.send(200, "text/html", buildHtmlForm(config));
        });

        // Form Submission Handler
        _server.on("/save", HTTP_POST, [this, &config]() {
            handleFormSave(config);
        });
    }

    String buildHtmlForm(const AppConfig &config) {
        String html = F("<!DOCTYPE html><html lang='en'><head>"
            "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>WIFI Monitor Setup</title><style>"
            ":root{--bg:#0f172a;--card:#1e293b;--text:#f8fafc;--muted:#94a3b8;--border:#334155;"
            "--primary:#38bdf8;--primary-hover:#0284c7;--success:#22c55e}"
            "*{box-sizing:border-box;margin:0;padding:0}"
            "body{background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,sans-serif;"
            "padding:1.5rem;display:flex;justify-content:center}"
            ".card{background:var(--card);border:1px solid var(--border);border-radius:1rem;padding:2rem;"
            "width:100%;max-width:540px;box-shadow:0 10px 25px -5px rgba(0,0,0,0.5)}"
            "h1{font-size:1.5rem;color:var(--primary);margin-bottom:0.5rem;display:flex;align-items:center;gap:0.5rem}"
            "p.sub{color:var(--muted);font-size:0.875rem;margin-bottom:1.5rem}"
            ".section-title{font-size:1rem;font-weight:600;margin:1.25rem 0 0.75rem;color:var(--primary);"
            "border-bottom:1px solid var(--border);padding-bottom:0.25rem}"
            ".field{margin-bottom:1rem}"
            "label{display:block;font-size:0.825rem;font-weight:500;margin-bottom:0.35rem;color:var(--muted)}"
            "input[type='text'],input[type='password'],input[type='url']{"
            "width:100%;background:#090d16;border:1px solid var(--border);border-radius:0.5rem;"
            "color:var(--text);padding:0.65rem 0.75rem;font-size:0.9rem;outline:none;transition:border-color 0.2s}"
            "input:focus{border-color:var(--primary)}"
            ".net-row{background:#131c2e;border:1px solid var(--border);border-radius:0.5rem;padding:0.75rem;margin-bottom:0.75rem}"
            ".net-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:0.5rem;font-size:0.8rem;color:var(--muted)}"
            ".pwd-wrap{position:relative}"
            ".toggle-btn{position:absolute;right:0.75rem;top:50%;transform:translateY(-50%);background:none;"
            "border:none;color:var(--muted);cursor:pointer;font-size:0.8rem;user-select:none}"
            "button[type='submit']{background:var(--primary);color:#0f172a;font-weight:600;width:100%;"
            "padding:0.85rem;border:none;border-radius:0.5rem;font-size:1rem;cursor:pointer;margin-top:1.25rem;"
            "transition:background 0.2s}"
            "button[type='submit']:hover{background:var(--primary-hover)}"
            "</style></head><body><div class='card'>"
            "<h1><svg width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'>"
            "<path d='M5 12.55a11 11 0 0 1 14.08 0'/><path d='M1.42 9a16 16 0 0 1 21.16 0'/>"
            "<path d='M8.53 16.11a6 6 0 0 1 6.95 0'/><line x1='12' y1='20' x2='12.01' y2='20'/></svg>"
            "WIFI Monitor</h1>"
            "<p class='sub'>Configure device station ID, monitored networks, and worker telemetry.</p>"
            "<form action='/save' method='POST'>");

        // Station Metadata
        html += F("<div class='field'><label for='station_id'>Station ID *</label>"
                  "<input type='text' id='station_id' name='station_id' required maxlength='63' value='");
        html += String(config.station_id);
        html += F("' placeholder='e.g. lab-building-a'></div>");

        html += F("<div class='field'><label for='station_desc'>Station Description</label>"
                  "<input type='text' id='station_desc' name='station_desc' maxlength='127' value='");
        html += String(config.station_desc);
        html += F("' placeholder='e.g. 2nd Floor Server Room Ceiling'></div>");

        // Monitored Networks (up to 5)
        html += F("<div class='section-title'>Monitored Wi-Fi Networks (Up to 5)</div>");
        for (int i = 0; i < MAX_MONITORED_NETWORKS; i++) {
            String netSSID = (i < config.network_count) ? String(config.networks[i].ssid) : "";
            String netPass = (i < config.network_count) ? String(config.networks[i].password) : "";
            html += "<div class='net-row'><div class='net-hdr'><span>Network #" + String(i + 1) + "</span></div>";
            html += "<div class='field'><label>SSID" + String(i == 0 ? " *" : "") + "</label>";
            html += "<input type='text' name='ssid_" + String(i) + "' value='" + netSSID + "' placeholder='Network SSID'" + String(i == 0 ? " required" : "") + "></div>";
            html += "<div class='field'><label>Password</label><div class='pwd-wrap'>";
            html += "<input type='password' id='pwd_" + String(i) + "' name='pass_" + String(i) + "' value='" + netPass + "' placeholder='Password (or leave blank for Open)'>";
            html += "<button type='button' class='toggle-btn' onclick='togglePwd(\"pwd_" + String(i) + "\")'>Show</button></div></div></div>";
        }

        // Worker & Bootstrap Security
        html += F("<div class='section-title'>Cloudflare Worker & Provisioning</div>");
        html += F("<div class='field'><label for='worker_url'>Worker Base URL *</label>"
                  "<input type='url' id='worker_url' name='worker_url' required maxlength='127' value='");
        html += String(strlen(config.worker_url) > 0 ? config.worker_url : DEFAULT_WORKER_URL);
        html += F("'></div>");

        html += F("<div class='field'><label for='master_phrase'>Master Phrase (Bootstrap Secret) *</label>"
                  "<div class='pwd-wrap'>"
                  "<input type='password' id='master_phrase' name='master_phrase' required maxlength='63' value='");
        html += String(config.master_phrase);
        html += F("' placeholder='Worker MASTER_PHRASE for token provisioning'>"
                  "<button type='button' class='toggle-btn' onclick='togglePwd(\"master_phrase\")'>Show</button></div></div>");

        // Submit Button & JavaScript
        html += F("<button type='submit'>Save and Start Monitoring</button></form></div>"
                  "<script>"
                  "function togglePwd(id){"
                  "  const inp = document.getElementById(id);"
                  "  const btn = event.target;"
                  "  if(inp.type==='password'){inp.type='text';btn.innerText='Hide';}"
                  "  else{inp.type='password';btn.innerText='Show';}"
                  "}"
                  "</script></body></html>");

        return html;
    }

    void handleFormSave(AppConfig &config) {
        String stId = _server.arg("station_id");
        String stDesc = _server.arg("station_desc");
        String workerUrl = _server.arg("worker_url");
        String masterPhrase = _server.arg("master_phrase");

        stId.trim();
        stDesc.trim();
        workerUrl.trim();
        masterPhrase.trim();

        if (stId.length() == 0 || workerUrl.length() == 0) {
            _server.send(400, "text/plain", "Missing required fields: Station ID or Worker URL");
            return;
        }

        strlcpy(config.station_id, stId.c_str(), sizeof(config.station_id));
        strlcpy(config.station_desc, stDesc.c_str(), sizeof(config.station_desc));
        strlcpy(config.worker_url, workerUrl.c_str(), sizeof(config.worker_url));
        if (masterPhrase.length() > 0) {
            strlcpy(config.master_phrase, masterPhrase.c_str(), sizeof(config.master_phrase));
        }

        config.network_count = 0;
        for (int i = 0; i < MAX_MONITORED_NETWORKS; i++) {
            String ssid = _server.arg("ssid_" + String(i));
            String pass = _server.arg("pass_" + String(i));
            ssid.trim();
            if (ssid.length() > 0) {
                strlcpy(config.networks[config.network_count].ssid, ssid.c_str(), sizeof(config.networks[0].ssid));
                strlcpy(config.networks[config.network_count].password, pass.c_str(), sizeof(config.networks[0].password));
                config.network_count++;
            }
        }

        config.is_configured = (config.network_count > 0);
        bool saved = _configMgr.saveConfig(config);

        String resp = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                        "<title>Configuration Saved</title><style>"
                        "body{background:#0f172a;color:#f8fafc;font-family:sans-serif;padding:2rem;text-align:center}"
                        ".box{background:#1e293b;border-radius:1rem;padding:2rem;max-width:400px;margin:auto}"
                        "h2{color:#22c55e;margin-bottom:1rem}p{color:#94a3b8;line-height:1.5}"
                        "</style></head><body><div class='box'>"
                        "<h2>Settings Saved!</h2>"
                        "<p>The ESP32 is now switching to monitoring mode and connecting to your network.</p>"
                        "</div></body></html>");

        _server.send(200, "text/html", resp);
        Serial.printf("[Portal] Saved configuration for station '%s'. Closing AP...\n", config.station_id);

        delay(1000);
        _portalDone = true;
    }

    AppConfigManager &_configMgr;
    LedController &_led;
    WebServer _server;
    DNSServer _dnsServer;
    bool _clientConnected;
    bool _portalDone;
    uint32_t _apStartTime;
    uint32_t _timeoutMs;
    bool _timeoutPaused;
};
