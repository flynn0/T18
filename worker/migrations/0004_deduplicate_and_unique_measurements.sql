-- Migration 0004: Deduplicate measurements and add unique constraint
DELETE FROM measurements
WHERE id NOT IN (
    SELECT MIN(id)
    FROM measurements
    GROUP BY station_id, ssid, timestamp
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_measurements_unique_station_ssid_time 
ON measurements (station_id, ssid, timestamp);
