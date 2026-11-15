#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "app_state.h"

#define SPOOL_DIR "/spool"
#define SPOOL_FILE_PATH "/spool/pending.jsonl"
#define SPOOL_TMP_PATH "/spool/pending.tmp"
#define MAX_SPOOL_BYTES (500 * 1024) // 500 KB ceiling

class SpoolManager {
public:
    static bool begin() {
        if (!LittleFS.begin(true)) {
            Serial.println(F("[Spool] ERROR: LittleFS mount failed"));
            return false;
        }
        if (!LittleFS.exists(SPOOL_DIR)) {
            LittleFS.mkdir(SPOOL_DIR);
        }
        return true;
    }

    static bool appendCycle(const CycleTelemetry &cycle) {
        if (!begin()) return false;

        // Serialize cycle to single JSON string (one line in JSONL)
        JsonDocument doc;
        doc["station_id"] = cycle.station_id;
        doc["timestamp"] = cycle.timestamp;

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
            lat["apple"] = net.apple_ms;

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

        String jsonLine;
        serializeJson(doc, jsonLine);
        jsonLine += "\n";

        size_t lineLen = jsonLine.length();

        // Check current file size and enforce 500 KB ring buffer ceiling
        size_t currentSize = getSpoolSize();
        if (currentSize + lineLen > MAX_SPOOL_BYTES) {
            Serial.printf("[Spool] Spool buffer (%u bytes) near 500 KB limit. Pruning oldest records...\n", (unsigned)currentSize);
            pruneOldestRecords(MAX_SPOOL_BYTES / 2); // Prune down to 250 KB
        }

        File file = LittleFS.open(SPOOL_FILE_PATH, "a");
        if (!file) {
            Serial.println(F("[Spool] Failed to open spool file for appending"));
            return false;
        }

        size_t bytesWritten = file.print(jsonLine);
        file.close();

        Serial.printf("[Spool] Appended telemetry cycle (%u bytes written, total spool: %u bytes)\n",
                      (unsigned)bytesWritten, (unsigned)getSpoolSize());
        return bytesWritten > 0;
    }

    static size_t getSpoolSize() {
        if (!LittleFS.exists(SPOOL_FILE_PATH)) return 0;
        File file = LittleFS.open(SPOOL_FILE_PATH, "r");
        if (!file) return 0;
        size_t sz = file.size();
        file.close();
        return sz;
    }

    static bool hasPendingData() {
        return getSpoolSize() > 0;
    }

    static String readNextBatchJson(size_t maxBytes = 32 * 1024) {
        if (!hasPendingData()) return "";

        File file = LittleFS.open(SPOOL_FILE_PATH, "r");
        if (!file) return "";

        // Read all JSONL lines and assemble them into a JSON array: [ {cycle1}, {cycle2}, ... ]
        String output = "[";
        bool first = true;

        while (file.available() && output.length() < maxBytes) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() > 0) {
                if (!first) output += ",";
                output += line;
                first = false;
            }
        }
        file.close();

        output += "]";
        return output;
    }

    static void clearSpool() {
        if (LittleFS.exists(SPOOL_FILE_PATH)) {
            LittleFS.remove(SPOOL_FILE_PATH);
            Serial.println(F("[Spool] Spool buffer cleared after successful upload."));
        }
    }

private:
    static void pruneOldestRecords(size_t targetMaxBytes) {
        if (!LittleFS.exists(SPOOL_FILE_PATH)) return;

        File src = LittleFS.open(SPOOL_FILE_PATH, "r");
        if (!src) return;

        // Skip bytes from the beginning until remaining data fits targetMaxBytes
        size_t total = src.size();
        size_t skipBytes = (total > targetMaxBytes) ? (total - targetMaxBytes) : 0;
        src.seek(skipBytes, SeekSet);

        // Advance to the next newline to ensure clean JSON line boundary
        if (skipBytes > 0) {
            src.readStringUntil('\n');
        }

        File dst = LittleFS.open(SPOOL_TMP_PATH, "w");
        if (!dst) {
            src.close();
            return;
        }

        // Copy remainder
        uint8_t buf[256];
        while (src.available()) {
            size_t n = src.read(buf, sizeof(buf));
            dst.write(buf, n);
        }

        src.close();
        dst.close();

        LittleFS.remove(SPOOL_FILE_PATH);
        LittleFS.rename(SPOOL_TMP_PATH, SPOOL_FILE_PATH);
    }
};
