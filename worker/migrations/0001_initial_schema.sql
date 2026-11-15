-- Migration 0001: Initial Schema for kreier/wifi
-- Tables: stations, measurements, scan_surveys, daily_summaries

CREATE TABLE IF NOT EXISTS stations (
    station_id TEXT PRIMARY KEY,
    description TEXT,
    device_token TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    last_seen INTEGER
);

CREATE INDEX IF NOT EXISTS idx_stations_device_token ON stations (device_token);

CREATE TABLE IF NOT EXISTS measurements (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    ssid TEXT NOT NULL,
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
    status TEXT
);

CREATE INDEX IF NOT EXISTS idx_measurements_station_time ON measurements (station_id, timestamp);
CREATE INDEX IF NOT EXISTS idx_measurements_timestamp ON measurements (timestamp);

CREATE TABLE IF NOT EXISTS scan_surveys (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    ssid TEXT,
    bssid TEXT,
    rssi INTEGER,
    quality INTEGER,
    encryption TEXT,
    phy_mode TEXT,
    channel INTEGER,
    band TEXT
);

CREATE INDEX IF NOT EXISTS idx_scan_surveys_station_time ON scan_surveys (station_id, timestamp);
CREATE INDEX IF NOT EXISTS idx_scan_surveys_timestamp ON scan_surveys (timestamp);

CREATE TABLE IF NOT EXISTS daily_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    date TEXT NOT NULL,
    ssid TEXT NOT NULL,
    sample_count INTEGER NOT NULL,
    assoc_min INTEGER,
    assoc_avg REAL,
    assoc_max INTEGER,
    dhcp_min INTEGER,
    dhcp_avg REAL,
    dhcp_max INTEGER,
    cf_min INTEGER,
    cf_avg REAL,
    cf_max INTEGER,
    google_min INTEGER,
    google_avg REAL,
    google_max INTEGER,
    UNIQUE(station_id, date, ssid)
);

CREATE INDEX IF NOT EXISTS idx_daily_summaries_station_date ON daily_summaries (station_id, date);
