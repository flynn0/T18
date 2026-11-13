#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "app_state.h"

struct ProbeTarget {
    const char *name;
    const char *url;
    const char *method; // "GET" or "HEAD"
};

static const ProbeTarget PROBE_TARGETS[] = {
    { "google",     "http://connectivitycheck.gstatic.com/generate_204", "GET"  },
    { "cloudflare", "http://cp.cloudflare.com/",                         "GET"  },
    { "microsoft",  "http://www.msftconnecttest.com/connecttest.txt",    "GET"  },
    { "facebook",   "http://www.facebook.com/",                          "HEAD" },
    { "netflix",    "http://fast.com/",                                  "HEAD" },
    { "apple",      "http://captive.apple.com/hotspot-detect.html",      "GET"  }
};
static const size_t NUM_PROBE_TARGETS = sizeof(PROBE_TARGETS) / sizeof(PROBE_TARGETS[0]);

class LatencyProber {
public:
    static NetworkMeasurement probeNetwork(const WifiNetworkConfig &netConfig, uint32_t connTimeoutMs = 15000) {
        NetworkMeasurement result;
        result.ssid = netConfig.ssid;
        result.bssid = "";
        result.channel = 0;
        result.assoc_ms = -1;
        result.dhcp_ms = -1;
        result.google_ms = -1;
        result.cloudflare_ms = -1;
        result.microsoft_ms = -1;
        result.facebook_ms = -1;
        result.netflix_ms = -1;
        result.apple_ms = -1;
        result.status = "failed";

        Serial.printf("[Prober] Starting fresh connection test to '%s'...\n", netConfig.ssid);

        // 1. Force fresh association
        WiFi.disconnect(true, true);
        delay(100);
        WiFi.mode(WIFI_STA);

        uint32_t tStart = millis();
        if (strlen(netConfig.password) > 0) {
            WiFi.begin(netConfig.ssid, netConfig.password);
        } else {
            WiFi.begin(netConfig.ssid);
        }

        // 2. Measure Association Time (Layer 2 link)
        uint32_t tAssocStart = millis();
        bool associated = false;
        while (millis() - tStart < connTimeoutMs) {
            wl_status_t status = WiFi.status();
            // WL_IDLE_STATUS, WL_SCAN_COMPLETED, or if we have BSSID we are associated
            if (WiFi.BSSIDstr().length() > 0 || status == WL_CONNECTED) {
                associated = true;
                result.assoc_ms = (int32_t)(millis() - tAssocStart);
                result.bssid = WiFi.BSSIDstr();
                result.channel = (uint8_t)WiFi.channel();
                break;
            }
            delay(10);
        }

        if (!associated) {
            Serial.printf("[Prober] Association to '%s' timed out\n", netConfig.ssid);
            result.status = "assoc_timeout";
            WiFi.disconnect(true);
            return result;
        }

        // 3. Measure DHCP IP Acquisition Time (Layer 3 lease)
        uint32_t tDhcpStart = millis();
        bool gotIp = false;
        while (millis() - tStart < connTimeoutMs) {
            if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
                gotIp = true;
                result.dhcp_ms = (int32_t)(millis() - tDhcpStart);
                break;
            }
            delay(10);
        }

        if (!gotIp) {
            Serial.printf("[Prober] DHCP lease acquisition for '%s' timed out\n", netConfig.ssid);
            result.status = "dhcp_timeout";
            WiFi.disconnect(true);
            return result;
        }

        Serial.printf("[Prober] Connected to '%s' (IP: %s, assoc: %d ms, dhcp: %d ms)\n",
                      netConfig.ssid, WiFi.localIP().toString().c_str(),
                      result.assoc_ms, result.dhcp_ms);

        result.status = "connected";

        // 4. Measure HTTP latency to 6 global targets (3000 ms timeout per target)
        WiFiClient client;
        for (size_t i = 0; i < NUM_PROBE_TARGETS; i++) {
            const ProbeTarget &tgt = PROBE_TARGETS[i];
            int32_t lat = measureHttpLatency(client, tgt.url, tgt.method, 3000);
            
            if (strcmp(tgt.name, "google") == 0) result.google_ms = lat;
            else if (strcmp(tgt.name, "cloudflare") == 0) result.cloudflare_ms = lat;
            else if (strcmp(tgt.name, "microsoft") == 0) result.microsoft_ms = lat;
            else if (strcmp(tgt.name, "facebook") == 0) result.facebook_ms = lat;
            else if (strcmp(tgt.name, "netflix") == 0) result.netflix_ms = lat;
            else if (strcmp(tgt.name, "apple") == 0) result.apple_ms = lat;

            Serial.printf("[Prober]   %-11s : %d ms\n", tgt.name, lat);
            delay(50); // Small breath between HTTP requests
        }

        return result;
    }

private:
    static int32_t measureHttpLatency(WiFiClient &client, const char *url, const char *method, uint32_t timeoutMs) {
        HTTPClient http;
        http.setTimeout(timeoutMs);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setReuse(false);

        if (!http.begin(client, url)) {
            return -1;
        }

        uint32_t t0 = millis();
        int httpCode = http.sendRequest(method);
        uint32_t elapsed = millis() - t0;
        http.end();

        if (httpCode > 0 && elapsed <= timeoutMs) {
            return (int32_t)elapsed;
        }

        return -1; // Timed out or network error
    }
};
