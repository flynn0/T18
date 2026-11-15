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
    int32_t gateway_ms;    // Local gateway RTT (TCP/ICMP)
    int32_t dns_ipv4_ms;   // DNS IPv4 (A) resolution time
    int32_t dns_ipv6_ms;   // DNS IPv6 (AAAA) resolution time (-1 on timeout/fail)
    bool captive_detected; // True if HTTP canary was intercepted or redirected
    String status; // e.g. "connected", "assoc_timeout", "dhcp_timeout"
};

struct NetworkDiagnostics {
    String ssid;
    String gateway_ip;
    String gateway_vendor; // "pfSense", "OpenWrt", "Generic Gateway", "None / Secured"
    bool web_exposed;
    String login_url;
    bool https_redirect;
    bool mtu_1500_ok;
    bool mtu_1492_ok;
    bool mtu_1420_ok;
    int32_t dns_ipv4_ms;
    int32_t dns_ipv6_ms;
    bool dns_ipv6_timeout;
    bool dns_rebind_blocked;
    bool captive_portal_detected;
    String open_ports;
    String details_json;

    NetworkDiagnostics() :
        ssid(""), gateway_ip(""), gateway_vendor("None / Secured"),
        web_exposed(false), login_url(""), https_redirect(false),
        mtu_1500_ok(true), mtu_1492_ok(true), mtu_1420_ok(true),
        dns_ipv4_ms(-1), dns_ipv6_ms(-1), dns_ipv6_timeout(false),
        dns_rebind_blocked(false), captive_portal_detected(false),
        open_ports(""), details_json("") {}
};

struct CycleTelemetry {
    String station_id;
    uint64_t timestamp; // UTC epoch seconds
    std::vector<NetworkMeasurement> measurements;
    std::vector<SurveyItem> survey;
    bool has_diagnostics;
    NetworkDiagnostics diagnostics;

    CycleTelemetry() : timestamp(0), has_diagnostics(false) {}
};
