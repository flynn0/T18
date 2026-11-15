-- Migration 0003: Drop legacy scan_surveys table after successful migration
DROP INDEX IF EXISTS idx_scan_surveys_station_time;
DROP INDEX IF EXISTS idx_scan_surveys_timestamp;
DROP TABLE IF EXISTS scan_surveys;
