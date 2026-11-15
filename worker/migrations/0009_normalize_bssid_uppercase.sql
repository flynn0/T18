-- Migration 0009: Normalize BSSID to uppercase across all tables and deduplicate discovered APs
-- Tables modified: station_discovered_aps, measurements, station_latest_measurements, station_overview_cache

-- 1. Deduplicate station_discovered_aps by (station_id, UPPER(bssid)) keeping the newest last_seen
CREATE TABLE IF NOT EXISTS station_discovered_aps_new (
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

INSERT OR REPLACE INTO station_discovered_aps_new (
    station_id, bssid, ssid, channel, rssi, quality, encryption, phy_mode, band, last_seen
)
SELECT
    station_id,
    UPPER(bssid) AS bssid,
    ssid,
    channel,
    rssi,
    quality,
    encryption,
    phy_mode,
    band,
    MAX(last_seen) AS last_seen
FROM station_discovered_aps
GROUP BY station_id, UPPER(bssid);

DROP TABLE station_discovered_aps;
ALTER TABLE station_discovered_aps_new RENAME TO station_discovered_aps;
CREATE INDEX IF NOT EXISTS idx_discovered_aps_station_seen ON station_discovered_aps (station_id, last_seen);

-- 2. Uppercase bssid in measurements and station_latest_measurements
UPDATE measurements SET bssid = UPPER(bssid) WHERE bssid IS NOT NULL;
UPDATE station_latest_measurements SET bssid = UPPER(bssid) WHERE bssid IS NOT NULL;

-- 3. Clear cache so the normalized data is loaded immediately
DELETE FROM station_overview_cache;
