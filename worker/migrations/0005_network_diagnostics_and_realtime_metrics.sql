-- Migration 0005: Add network diagnostics and real-time gateway/DNS metrics
-- Tables modified: measurements, station_latest_measurements
-- New table: station_network_diagnostics

ALTER TABLE measurements ADD COLUMN gateway_ms INTEGER;
ALTER TABLE measurements ADD COLUMN dns_ipv4_ms INTEGER;
ALTER TABLE measurements ADD COLUMN dns_ipv6_ms INTEGER;

ALTER TABLE station_latest_measurements ADD COLUMN gateway_ms INTEGER;
ALTER TABLE station_latest_measurements ADD COLUMN dns_ipv4_ms INTEGER;
ALTER TABLE station_latest_measurements ADD COLUMN dns_ipv6_ms INTEGER;

CREATE TABLE IF NOT EXISTS station_network_diagnostics (
    station_id TEXT NOT NULL,
    ssid TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    gateway_ip TEXT,
    gateway_vendor TEXT,
    web_exposed INTEGER DEFAULT 0,
    login_url TEXT,
    https_redirect INTEGER DEFAULT 0,
    mtu_1500_ok INTEGER DEFAULT 1,
    mtu_1492_ok INTEGER DEFAULT 1,
    mtu_1420_ok INTEGER DEFAULT 1,
    dns_ipv4_ms INTEGER,
    dns_ipv6_ms INTEGER,
    dns_ipv6_timeout INTEGER DEFAULT 0,
    dns_rebind_blocked INTEGER DEFAULT 0,
    captive_portal_detected INTEGER DEFAULT 0,
    open_ports TEXT,
    details_json TEXT,
    PRIMARY KEY (station_id, ssid)
);

CREATE INDEX IF NOT EXISTS idx_diagnostics_station_time ON station_network_diagnostics (station_id, timestamp DESC);
