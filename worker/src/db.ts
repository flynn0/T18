import { Env, StationRecord, MeasurementRecord, ScanSurveyRecord, InactiveSurveyRecord } from "./types";

export class Database {
  private db: D1Database;

  constructor(env: Env) {
    this.db = env.DB;
  }

  async getStationByToken(deviceToken: string): Promise<StationRecord | null> {
    const result = await this.db
      .prepare("SELECT * FROM stations WHERE device_token = ? LIMIT 1")
      .bind(deviceToken)
      .first<StationRecord>();
    return result;
  }

  async getStationById(stationId: string): Promise<StationRecord | null> {
    const result = await this.db
      .prepare("SELECT * FROM stations WHERE station_id = ? LIMIT 1")
      .bind(stationId)
      .first<StationRecord>();
    return result;
  }

  async upsertStation(stationId: string, description: string, deviceToken: string): Promise<void> {
    const now = Math.floor(Date.now() / 1000);
    await this.db
      .prepare(`
        INSERT INTO stations (station_id, description, device_token, created_at, last_seen)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(station_id) DO UPDATE SET
          description = excluded.description,
          device_token = excluded.device_token,
          last_seen = excluded.last_seen
      `)
      .bind(stationId, description, deviceToken, now, now)
      .run();
  }

  async updateStationLastSeen(stationId: string, timestamp: number): Promise<void> {
    await this.db
      .prepare("UPDATE stations SET last_seen = ? WHERE station_id = ?")
      .bind(timestamp, stationId)
      .run();
  }

  async insertMeasurements(measurements: MeasurementRecord[]): Promise<number> {
    if (measurements.length === 0) return 0;
    
    // D1 supports batch executions
    const statements = measurements.map(m =>
      this.db
        .prepare(`
          INSERT INTO measurements (
            station_id, timestamp, ssid, bssid, channel,
            assoc_ms, dhcp_ms, google_ms, cloudflare_ms,
            microsoft_ms, facebook_ms, netflix_ms, apple_ms, status
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `)
        .bind(
          m.station_id,
          m.timestamp,
          m.ssid,
          m.bssid ?? null,
          m.channel ?? null,
          m.assoc_ms ?? null,
          m.dhcp_ms ?? null,
          m.google_ms ?? null,
          m.cloudflare_ms ?? null,
          m.microsoft_ms ?? null,
          m.facebook_ms ?? null,
          m.netflix_ms ?? null,
          m.apple_ms ?? null,
          m.status ?? "unknown"
        )
    );

    await this.db.batch(statements);
    return measurements.length;
  }

  async insertSurveys(surveys: ScanSurveyRecord[]): Promise<number> {
    if (surveys.length === 0) return 0;

    const statements = surveys.map(s =>
      this.db
        .prepare(`
          INSERT INTO scan_surveys (
            station_id, timestamp, ssid, bssid, rssi,
            quality, encryption, phy_mode, channel, band
          ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        `)
        .bind(
          s.station_id,
          s.timestamp,
          s.ssid ?? null,
          s.bssid ?? null,
          s.rssi ?? null,
          s.quality ?? null,
          s.encryption ?? null,
          s.phy_mode ?? null,
          s.channel ?? null,
          s.band ?? "2.4 GHz"
        )
    );

    await this.db.batch(statements);
    return surveys.length;
  }

  async listStations(): Promise<StationRecord[]> {
    const result = await this.db
      .prepare("SELECT station_id, description, created_at, last_seen FROM stations ORDER BY last_seen DESC")
      .all<StationRecord>();
    return result.results;
  }

  async getLatestMeasurements(stationId: string): Promise<MeasurementRecord[]> {
    const result = await this.db
      .prepare(`
        SELECT m.* FROM measurements m
        INNER JOIN (
          SELECT ssid, MAX(timestamp) as max_time
          FROM measurements
          WHERE station_id = ?
          GROUP BY ssid
        ) latest ON m.ssid = latest.ssid AND m.timestamp = latest.max_time
        WHERE m.station_id = ?
        ORDER BY m.ssid ASC
      `)
      .bind(stationId, stationId)
      .all<MeasurementRecord>();
    return result.results;
  }

  async getLatestSurvey(stationId: string): Promise<ScanSurveyRecord[]> {
    const latestTime = await this.db
      .prepare("SELECT MAX(timestamp) as max_t FROM scan_surveys WHERE station_id = ?")
      .bind(stationId)
      .first<{ max_t: number | null }>();

    if (!latestTime || !latestTime.max_t) return [];

    const result = await this.db
      .prepare("SELECT * FROM scan_surveys WHERE station_id = ? AND timestamp = ? ORDER BY rssi DESC")
      .bind(stationId, latestTime.max_t)
      .all<ScanSurveyRecord>();
    return result.results;
  }

  async getInactiveSurveys(stationId: string): Promise<InactiveSurveyRecord[]> {
    const latestTime = await this.db
      .prepare("SELECT MAX(timestamp) as max_t FROM scan_surveys WHERE station_id = ?")
      .bind(stationId)
      .first<{ max_t: number | null }>();

    if (!latestTime || !latestTime.max_t) return [];

    const since24h = latestTime.max_t - 86400; // Prior 24 hours of scan history

    const result = await this.db
      .prepare(`
        SELECT 
          s.ssid,
          s.bssid,
          s.channel,
          s.phy_mode,
          s.encryption,
          s.band,
          MAX(s.rssi) AS rssi,
          MAX(s.quality) AS quality,
          MAX(s.timestamp) AS last_seen
        FROM scan_surveys s
        WHERE s.station_id = ?
          AND s.bssid IS NOT NULL
          AND s.timestamp >= ?
          AND s.bssid NOT IN (
            SELECT bssid FROM scan_surveys WHERE station_id = ? AND timestamp = ? AND bssid IS NOT NULL
          )
        GROUP BY s.bssid
        ORDER BY last_seen DESC
      `)
      .bind(stationId, since24h, stationId, latestTime.max_t)
      .all<InactiveSurveyRecord>();
    return result.results;
  }

  async getRecentMeasurements(stationId: string, limit: number = 288): Promise<MeasurementRecord[]> {
    const result = await this.db
      .prepare(`
        SELECT * FROM measurements
        WHERE station_id = ?
        ORDER BY timestamp DESC
        LIMIT ?
      `)
      .bind(stationId, limit)
      .all<MeasurementRecord>();
    return result.results.reverse(); // Chronological order
  }

  async getDailySummaries(stationId: string, limitDays: number = 30) {
    const result = await this.db
      .prepare(`
        SELECT * FROM daily_summaries
        WHERE station_id = ?
        ORDER BY date DESC
        LIMIT ?
      `)
      .bind(stationId, limitDays * 5)
      .all();
    return result.results;
  }
}
