-- Migration 0010: Add Bluetooth Low Energy (BLE) and Classic Bluetooth telemetry
-- New tables: station_bluetooth_summary, station_latest_bluetooth

CREATE TABLE IF NOT EXISTS station_bluetooth_summary (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    station_id TEXT NOT NULL,
    timestamp INTEGER NOT NULL,
    ble_total_packets INTEGER NOT NULL,
    ble_total_macs INTEGER NOT NULL,
    ble_unique_devices INTEGER NOT NULL,
    ble_random_macs INTEGER NOT NULL,
    ble_public_macs INTEGER NOT NULL,
    ble_beacons INTEGER NOT NULL,
    ble_smartphones INTEGER NOT NULL,
    ble_wearables INTEGER NOT NULL,
    ble_audio INTEGER NOT NULL,
    ble_other INTEGER NOT NULL,
    classic_total INTEGER NOT NULL,
    scan_duration_ms INTEGER,
    devices_json TEXT,
    UNIQUE(station_id, timestamp)
);

CREATE INDEX IF NOT EXISTS idx_bt_summary_station_time ON station_bluetooth_summary (station_id, timestamp DESC);

CREATE TABLE IF NOT EXISTS station_latest_bluetooth (
    station_id TEXT PRIMARY KEY,
    timestamp INTEGER NOT NULL,
    ble_total_packets INTEGER NOT NULL,
    ble_total_macs INTEGER NOT NULL,
    ble_unique_devices INTEGER NOT NULL,
    ble_random_macs INTEGER NOT NULL,
    ble_public_macs INTEGER NOT NULL,
    ble_beacons INTEGER NOT NULL,
    ble_smartphones INTEGER NOT NULL,
    ble_wearables INTEGER NOT NULL,
    ble_audio INTEGER NOT NULL,
    ble_other INTEGER NOT NULL,
    classic_total INTEGER NOT NULL,
    scan_duration_ms INTEGER,
    devices_json TEXT
);
