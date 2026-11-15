-- Migration 0008: Add battery voltage history tracking
-- Tables modified: measurements, station_latest_measurements
-- New table: station_battery_history

ALTER TABLE measurements ADD COLUMN battery_mv INTEGER;
ALTER TABLE station_latest_measurements ADD COLUMN battery_mv INTEGER;

CREATE TABLE IF NOT EXISTS station_battery_history (
    station_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    battery_mv INTEGER NOT NULL,
    PRIMARY KEY (station_id, timestamp)
);

CREATE INDEX IF NOT EXISTS idx_battery_history_station_time ON station_battery_history (station_id, timestamp DESC);
