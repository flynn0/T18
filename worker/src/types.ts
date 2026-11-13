export interface Env {
  DB: D1Database;
  MASTER_PHRASE: string;
  ENVIRONMENT?: string;
}

export interface StationRecord {
  station_id: string;
  description: string | null;
  device_token: string;
  created_at: number;
  last_seen: number | null;
}

export interface MeasurementRecord {
  id?: number;
  station_id: string;
  timestamp: number;
  ssid: string;
  bssid: string | null;
  channel: number | null;
  assoc_ms: number | null;
  dhcp_ms: number | null;
  google_ms: number | null;
  cloudflare_ms: number | null;
  microsoft_ms: number | null;
  facebook_ms: number | null;
  netflix_ms: number | null;
  apple_ms: number | null;
  status: string | null;
}

export interface ScanSurveyRecord {
  id?: number;
  station_id: string;
  timestamp: number;
  ssid: string | null;
  bssid: string | null;
  rssi: number | null;
  quality: number | null;
  encryption: string | null;
  phy_mode: string | null;
  channel: number | null;
  band: string | null;
}

export interface DailySummaryRecord {
  id?: number;
  station_id: string;
  date: string;
  ssid: string;
  sample_count: number;
  assoc_min: number;
  assoc_avg: number;
  assoc_max: number;
  dhcp_min: number;
  dhcp_avg: number;
  dhcp_max: number;
  cf_min: number;
  cf_avg: number;
  cf_max: number;
  google_min: number;
  google_avg: number;
  google_max: number;
}

export interface RegisterRequest {
  station_id: string;
  description?: string;
  bootstrap_secret: string;
}

export interface NetworkPayload {
  ssid: string;
  bssid?: string;
  channel?: number;
  assoc_ms?: number;
  dhcp_ms?: number;
  http_latencies?: {
    google?: number;
    cloudflare?: number;
    microsoft?: number;
    facebook?: number;
    netflix?: number;
    apple?: number;
  };
  status?: string;
}

export interface SurveyPayload {
  ssid?: string;
  bssid?: string;
  rssi?: number;
  quality?: number;
  encryption?: string;
  phy_mode?: string;
  channel?: number;
  band?: string;
}

export interface TelemetryCyclePayload {
  station_id?: string;
  timestamp?: number;
  networks?: NetworkPayload[];
  scan_survey?: SurveyPayload[];
}
