-- Migration 0002: Optimize telemetry storage, indexing, and cached overview
-- Tables: station_overview_cache, station_latest_scan, station_discovered_aps, station_latest_measurements

-- 1. Table for caching the generated station overview (temporary dataset with 10-minute validity)
CREATE TABLE IF NOT EXISTS station_overview_cache (
    station_id TEXT PRIMARY KEY,
    cached_at INTEGER NOT NULL,
    data TEXT NOT NULL
);

-- 2. Store the latest WiFi spectrum scan per station as a single document/snapshot (1 write per 10m instead of 70+ rows)
CREATE TABLE IF NOT EXISTS station_latest_scan (
    station_id TEXT PRIMARY KEY,
    timestamp INTEGER NOT NULL,
    scan_json TEXT NOT NULL
);

-- 3. Unique AP tracking table to replace full-table scans for inactive APs
CREATE TABLE IF NOT EXISTS station_discovered_aps (
    station_id TEXT NOT NULL,
    bssid TEXT NOT NULL,
    ssid TEXT,
    channel INTEGER,
    rssi INTEGER,
    quality INTEGER,
    encryption TEXT,
    phy_mode TEXT,
    band TEXT,
    last_seen INTEGER NOT NULL,
    PRIMARY KEY (station_id, bssid)
);

CREATE INDEX IF NOT EXISTS idx_discovered_aps_station_seen ON station_discovered_aps (station_id, last_seen);

-- 4. Fast lookup for the latest network latency metrics per station/SSID (1-3 rows per station)
CREATE TABLE IF NOT EXISTS station_latest_measurements (
    station_id TEXT NOT NULL,
    ssid TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    bssid TEXT,
    channel INTEGER,
    assoc_ms INTEGER,
    dhcp_ms INTEGER,
    google_ms INTEGER,
    cloudflare_ms INTEGER,
    microsoft_ms INTEGER,
    facebook_ms INTEGER,
    netflix_ms INTEGER,
    apple_ms INTEGER,
    status TEXT,
    PRIMARY KEY (station_id, ssid)
);

-- 5. Additional index on measurements for fast descending chronological retrieval
CREATE INDEX IF NOT EXISTS idx_measurements_station_time_desc ON measurements (station_id, timestamp DESC);
