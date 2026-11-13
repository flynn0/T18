#pragma once

#include <Arduino.h>
#include <vector>

#define MAX_MONITORED_NETWORKS 5
#define DEFAULT_WORKER_URL "https://wifi.kreier.workers.dev"

struct WifiNetworkConfig {
    char ssid[33];
    char password[65];
};

struct AppConfig {
    char station_id[64];
    char station_desc[128];
    char worker_url[128];
    char master_phrase[64]; // Cleared once device_token is acquired
    WifiNetworkConfig networks[MAX_MONITORED_NETWORKS];
    uint8_t network_count;
    bool is_configured;

    AppConfig() {
        memset(station_id, 0, sizeof(station_id));
        memset(station_desc, 0, sizeof(station_desc));
        strncpy(worker_url, DEFAULT_WORKER_URL, sizeof(worker_url) - 1);
        memset(master_phrase, 0, sizeof(master_phrase));
        memset(networks, 0, sizeof(networks));
        network_count = 0;
        is_configured = false;
    }
};

struct SurveyItem {
    String ssid;
    String bssid;
    int16_t rssi;
    uint8_t quality;
    String encryption;
    String phy_mode;
    uint8_t channel;
    String band;
};

struct NetworkMeasurement {
    String ssid;
    String bssid;
    uint8_t channel;
    int32_t assoc_ms;
    int32_t dhcp_ms;
    int32_t google_ms;
    int32_t cloudflare_ms;
    int32_t microsoft_ms;
    int32_t facebook_ms;
    int32_t netflix_ms;
    int32_t apple_ms;
    String status; // e.g. "connected", "assoc_timeout", "dhcp_timeout"
};

struct CycleTelemetry {
    String station_id;
    uint64_t timestamp; // UTC epoch seconds
    std::vector<NetworkMeasurement> measurements;
    std::vector<SurveyItem> survey;
};
