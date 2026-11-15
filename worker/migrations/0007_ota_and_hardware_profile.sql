-- Migration 0007: OTA Targeting and Hardware Profile tracking
-- Adds fields to track firmware versions, canary targeting, battery, and hardware model

ALTER TABLE stations ADD COLUMN firmware_version TEXT;
ALTER TABLE stations ADD COLUMN target_version TEXT;
ALTER TABLE stations ADD COLUMN board_type TEXT;
ALTER TABLE stations ADD COLUMN battery_mv INTEGER;
ALTER TABLE stations ADD COLUMN is_canary INTEGER DEFAULT 0;
ALTER TABLE stations ADD COLUMN ota_url TEXT;

CREATE INDEX IF NOT EXISTS idx_stations_target_version ON stations (target_version);
