-- Migration 0006: Add channel utilization history and Wi-Fi spectrum scan duration
-- Tables modified: measurements, station_latest_measurements, station_latest_scan
-- New table: station_channel_utilization

ALTER TABLE measurements ADD COLUMN scan_duration_ms INTEGER;
ALTER TABLE station_latest_measurements ADD COLUMN scan_duration_ms INTEGER;
ALTER TABLE station_latest_scan ADD COLUMN scan_duration_ms INTEGER;

CREATE TABLE IF NOT EXISTS station_channel_utilization (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    channel INTEGER NOT NULL,
    mgmt_frames INTEGER NOT NULL,
    data_frames INTEGER NOT NULL,
    ctrl_frames INTEGER NOT NULL,
    rssi_hist TEXT NOT NULL,
    unique_transmitters INTEGER NOT NULL,
    airtime_pct REAL NOT NULL,
    scan_duration_ms INTEGER,
    UNIQUE(station_id, timestamp, channel)
);

CREATE INDEX IF NOT EXISTS idx_channel_util_station_time ON station_channel_utilization (station_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_channel_util_station_ch_time ON station_channel_utilization (station_id, channel, timestamp DESC);
