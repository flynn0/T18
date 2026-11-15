import { Env, StationRecord, MeasurementRecord, ScanSurveyRecord, InactiveSurveyRecord, NetworkDiagnosticsPayload, NetworkDiagnosticsRecord, ChannelUtilizationPayload, ChannelUtilizationRecord } from "./types";

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

  async renameStation(oldId: string, newId: string): Promise<void> {
    if (!oldId || !newId || oldId === newId) return;
    const statements = [
      this.db.prepare("UPDATE stations SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE measurements SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_latest_measurements SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_latest_scan SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_discovered_aps SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_channel_utilization SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_network_diagnostics SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE daily_summaries SET station_id = ? WHERE station_id = ?").bind(newId, oldId),
      this.db.prepare("UPDATE station_overview_cache SET station_id = ? WHERE station_id = ?").bind(newId, oldId)
    ];
    await this.db.batch(statements);
  }

  async updateStationDescription(stationId: string, description: string): Promise<void> {
    await this.db
      .prepare("UPDATE stations SET description = ? WHERE station_id = ?")
      .bind(description, stationId)
      .run();
  }

  async updateStationLastSeen(
    stationId: string,
    timestamp: number,
    meta?: { firmware_version?: string; board_type?: string; battery_mv?: number }
  ): Promise<void> {
    if (meta && (meta.firmware_version || meta.board_type || meta.battery_mv !== undefined)) {
      await this.db
        .prepare(`
          UPDATE stations SET
            last_seen = ?,
            firmware_version = COALESCE(?, firmware_version),
            board_type = COALESCE(?, board_type),
            battery_mv = COALESCE(?, battery_mv)
          WHERE station_id = ?
        `)
        .bind(
          timestamp,
          meta.firmware_version ?? null,
          meta.board_type ?? null,
          meta.battery_mv ?? null,
          stationId
        )
        .run();
    } else {
      await this.db
        .prepare("UPDATE stations SET last_seen = ? WHERE station_id = ?")
        .bind(timestamp, stationId)
        .run();
    }
  }

  async insertMeasurements(measurements: MeasurementRecord[]): Promise<number> {
    if (measurements.length === 0) return 0;
    
    // Batch insert historical measurements and upsert latest measurements
    const statements: D1PreparedStatement[] = [];

    for (const m of measurements) {
      // 1. Append to historical measurements (144/day per SSID, deduplicating retries)
      statements.push(
        this.db
          .prepare(`
            INSERT INTO measurements (
              station_id, timestamp, ssid, bssid, channel,
              assoc_ms, dhcp_ms, google_ms, cloudflare_ms,
              microsoft_ms, facebook_ms, netflix_ms, apple_ms,
              gateway_ms, dns_ipv4_ms, dns_ipv6_ms, scan_duration_ms, status
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(station_id, ssid, timestamp) DO UPDATE SET
              bssid = excluded.bssid,
              channel = excluded.channel,
              assoc_ms = excluded.assoc_ms,
              dhcp_ms = excluded.dhcp_ms,
              google_ms = excluded.google_ms,
              cloudflare_ms = excluded.cloudflare_ms,
              microsoft_ms = excluded.microsoft_ms,
              facebook_ms = excluded.facebook_ms,
              netflix_ms = excluded.netflix_ms,
              apple_ms = excluded.apple_ms,
              gateway_ms = excluded.gateway_ms,
              dns_ipv4_ms = excluded.dns_ipv4_ms,
              dns_ipv6_ms = excluded.dns_ipv6_ms,
              scan_duration_ms = excluded.scan_duration_ms,
              status = excluded.status
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
            m.gateway_ms ?? null,
            m.dns_ipv4_ms ?? null,
            m.dns_ipv6_ms ?? null,
            m.scan_duration_ms ?? null,
            m.status ?? "unknown"
          )
      );

      // 2. Fast upsert into station_latest_measurements (1 row per SSID)
      statements.push(
        this.db
          .prepare(`
            INSERT INTO station_latest_measurements (
              station_id, ssid, timestamp, bssid, channel,
              assoc_ms, dhcp_ms, google_ms, cloudflare_ms,
              microsoft_ms, facebook_ms, netflix_ms, apple_ms,
              gateway_ms, dns_ipv4_ms, dns_ipv6_ms, scan_duration_ms, status
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(station_id, ssid) DO UPDATE SET
              timestamp = excluded.timestamp,
              bssid = excluded.bssid,
              channel = excluded.channel,
              assoc_ms = excluded.assoc_ms,
              dhcp_ms = excluded.dhcp_ms,
              google_ms = excluded.google_ms,
              cloudflare_ms = excluded.cloudflare_ms,
              microsoft_ms = excluded.microsoft_ms,
              facebook_ms = excluded.facebook_ms,
              netflix_ms = excluded.netflix_ms,
              apple_ms = excluded.apple_ms,
              gateway_ms = excluded.gateway_ms,
              dns_ipv4_ms = excluded.dns_ipv4_ms,
              dns_ipv6_ms = excluded.dns_ipv6_ms,
              scan_duration_ms = excluded.scan_duration_ms,
              status = excluded.status
          `)
          .bind(
            m.station_id,
            m.ssid,
            m.timestamp,
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
            m.gateway_ms ?? null,
            m.dns_ipv4_ms ?? null,
            m.dns_ipv6_ms ?? null,
            m.scan_duration_ms ?? null,
            m.status ?? "unknown"
          )
      );
    }

    await this.db.batch(statements);
    return measurements.length;
  }

  async saveSurveys(stationId: string, timestamp: number, surveys: ScanSurveyRecord[], scanDurationMs?: number | null): Promise<number> {
    if (surveys.length === 0) return 0;

    const statements: D1PreparedStatement[] = [];

    // 1. Snapshot the latest survey as a single compact JSON document (1 row instead of 70+ rows!)
    statements.push(
      this.db
        .prepare(`
          INSERT INTO station_latest_scan (station_id, timestamp, scan_json, scan_duration_ms)
          VALUES (?, ?, ?, ?)
          ON CONFLICT(station_id) DO UPDATE SET
            timestamp = excluded.timestamp,
            scan_json = excluded.scan_json,
            scan_duration_ms = excluded.scan_duration_ms
        `)
        .bind(stationId, timestamp, JSON.stringify(surveys), scanDurationMs ?? null)
    );

    // 2. Upsert each AP into station_discovered_aps for tracking unique APs and inactive detection
    for (const s of surveys) {
      if (!s.bssid) continue;
      statements.push(
        this.db
          .prepare(`
            INSERT INTO station_discovered_aps (
              station_id, bssid, ssid, channel, rssi, quality, encryption, phy_mode, band, last_seen
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(station_id, bssid) DO UPDATE SET
              ssid = excluded.ssid,
              channel = excluded.channel,
              rssi = excluded.rssi,
              quality = excluded.quality,
              encryption = excluded.encryption,
              phy_mode = excluded.phy_mode,
              band = excluded.band,
              last_seen = excluded.last_seen
          `)
          .bind(
            stationId,
            s.bssid,
            s.ssid ?? null,
            s.channel ?? null,
            s.rssi ?? null,
            s.quality ?? null,
            s.encryption ?? null,
            s.phy_mode ?? null,
            s.band ?? "2.4 GHz",
            timestamp
          )
      );
    }

    await this.db.batch(statements);
    return surveys.length;
  }

  // Deprecated legacy insertSurveys kept for backward compatibility
  async insertSurveys(surveys: ScanSurveyRecord[]): Promise<number> {
    if (surveys.length === 0) return 0;
    const first = surveys[0];
    return await this.saveSurveys(first.station_id, first.timestamp, surveys);
  }

  async listStations(): Promise<StationRecord[]> {
    const result = await this.db
      .prepare("SELECT station_id, description, created_at, last_seen, firmware_version, board_type, battery_mv FROM stations ORDER BY last_seen DESC")
      .all<StationRecord>();
    return result.results;
  }

  async getLatestMeasurements(stationId: string): Promise<MeasurementRecord[]> {
    // 1. Fast path: lookup from station_latest_measurements (1-3 rows read)
    const fastResult = await this.db
      .prepare("SELECT * FROM station_latest_measurements WHERE station_id = ? ORDER BY ssid ASC")
      .bind(stationId)
      .all<MeasurementRecord>();

    if (fastResult.results && fastResult.results.length > 0) {
      return fastResult.results;
    }

    // 2. Fallback to legacy query if fast table has not been populated yet
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
    const cached = await this.db
      .prepare("SELECT scan_json FROM station_latest_scan WHERE station_id = ?")
      .bind(stationId)
      .first<{ scan_json: string }>();

    if (cached && cached.scan_json) {
      try {
        return JSON.parse(cached.scan_json);
      } catch {}
    }

    return [];
  }

  async getInactiveSurveys(stationId: string): Promise<InactiveSurveyRecord[]> {
    const latestScan = await this.db
      .prepare("SELECT timestamp FROM station_latest_scan WHERE station_id = ?")
      .bind(stationId)
      .first<{ timestamp: number }>();

    if (latestScan && latestScan.timestamp) {
      const since24h = latestScan.timestamp - 86400;
      const result = await this.db
        .prepare(`
          SELECT ssid, bssid, channel, phy_mode, encryption, band, rssi, quality, last_seen
          FROM station_discovered_aps
          WHERE station_id = ?
            AND last_seen >= ?
            AND last_seen < ?
          ORDER BY last_seen DESC
        `)
        .bind(stationId, since24h, latestScan.timestamp)
        .all<InactiveSurveyRecord>();
      return result.results;
    }

    return [];
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

  // 10-minute Overview Temporary Dataset Cache
  async getOverviewCache(stationId: string, maxAgeSec: number = 600): Promise<string | null> {
    const minCachedAt = Math.floor(Date.now() / 1000) - maxAgeSec;
    const result = await this.db
      .prepare("SELECT data FROM station_overview_cache WHERE station_id = ? AND cached_at >= ?")
      .bind(stationId, minCachedAt)
      .first<{ data: string }>();
    return result?.data ?? null;
  }

  async setOverviewCache(stationId: string, data: string): Promise<void> {
    const now = Math.floor(Date.now() / 1000);
    await this.db
      .prepare(`
        INSERT INTO station_overview_cache (station_id, cached_at, data)
        VALUES (?, ?, ?)
        ON CONFLICT(station_id) DO UPDATE SET
          cached_at = excluded.cached_at,
          data = excluded.data
      `)
      .bind(stationId, now, data)
      .run();
  }

  async invalidateOverviewCache(stationId: string): Promise<void> {
    await this.db
      .prepare("DELETE FROM station_overview_cache WHERE station_id = ?")
      .bind(stationId)
      .run();
  }

  async saveNetworkDiagnostics(stationId: string, timestamp: number, diag: NetworkDiagnosticsPayload): Promise<void> {
    const ssid = diag.ssid || "primary";
    await this.db
      .prepare(`
        INSERT INTO station_network_diagnostics (
          station_id, ssid, timestamp, gateway_ip, gateway_vendor,
          web_exposed, login_url, https_redirect,
          mtu_1500_ok, mtu_1492_ok, mtu_1420_ok,
          dns_ipv4_ms, dns_ipv6_ms, dns_ipv6_timeout,
          dns_rebind_blocked, captive_portal_detected, open_ports, details_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(station_id, ssid) DO UPDATE SET
          timestamp = excluded.timestamp,
          gateway_ip = excluded.gateway_ip,
          gateway_vendor = excluded.gateway_vendor,
          web_exposed = excluded.web_exposed,
          login_url = excluded.login_url,
          https_redirect = excluded.https_redirect,
          mtu_1500_ok = excluded.mtu_1500_ok,
          mtu_1492_ok = excluded.mtu_1492_ok,
          mtu_1420_ok = excluded.mtu_1420_ok,
          dns_ipv4_ms = excluded.dns_ipv4_ms,
          dns_ipv6_ms = excluded.dns_ipv6_ms,
          dns_ipv6_timeout = excluded.dns_ipv6_timeout,
          dns_rebind_blocked = excluded.dns_rebind_blocked,
          captive_portal_detected = excluded.captive_portal_detected,
          open_ports = excluded.open_ports,
          details_json = excluded.details_json
      `)
      .bind(
        stationId,
        ssid,
        timestamp,
        diag.gateway_ip ?? null,
        diag.gateway_vendor ?? "Unknown",
        diag.web_exposed ? 1 : 0,
        diag.login_url ?? null,
        diag.https_redirect ? 1 : 0,
        diag.mtu_1500_ok ? 1 : 0,
        diag.mtu_1492_ok ? 1 : 0,
        diag.mtu_1420_ok ? 1 : 0,
        diag.dns_ipv4_ms ?? null,
        diag.dns_ipv6_ms ?? null,
        diag.dns_ipv6_timeout ? 1 : 0,
        diag.dns_rebind_blocked ? 1 : 0,
        diag.captive_portal_detected ? 1 : 0,
        diag.open_ports ?? null,
        diag.details_json ?? null
      )
      .run();
  }

  async getLatestDiagnostics(stationId: string): Promise<NetworkDiagnosticsRecord[]> {
    const result = await this.db
      .prepare("SELECT * FROM station_network_diagnostics WHERE station_id = ? ORDER BY timestamp DESC")
      .bind(stationId)
      .all<NetworkDiagnosticsRecord>();
    return result.results || [];
  }

  async saveChannelUtilization(
    stationId: string,
    timestamp: number,
    scanDurationMs: number | null,
    metrics: ChannelUtilizationPayload[]
  ): Promise<number> {
    if (!metrics || metrics.length === 0) return 0;

    const statements: D1PreparedStatement[] = [];
    for (const m of metrics) {
      const rssiStr = Array.isArray(m.rssi_hist) ? JSON.stringify(m.rssi_hist) : "[]";
      statements.push(
        this.db
          .prepare(`
            INSERT INTO station_channel_utilization (
              station_id, timestamp, channel, mgmt_frames, data_frames,
              ctrl_frames, rssi_hist, unique_transmitters, airtime_pct, scan_duration_ms
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(station_id, timestamp, channel) DO UPDATE SET
              mgmt_frames = excluded.mgmt_frames,
              data_frames = excluded.data_frames,
              ctrl_frames = excluded.ctrl_frames,
              rssi_hist = excluded.rssi_hist,
              unique_transmitters = excluded.unique_transmitters,
              airtime_pct = excluded.airtime_pct,
              scan_duration_ms = excluded.scan_duration_ms
          `)
          .bind(
            stationId,
            timestamp,
            m.channel,
            m.mgmt_frames || 0,
            m.data_frames || 0,
            m.ctrl_frames || 0,
            rssiStr,
            m.unique_transmitters || 0,
            m.airtime_pct || 0,
            scanDurationMs ?? null
          )
      );
    }

    await this.db.batch(statements);
    return metrics.length;
  }

  async getRecentChannelUtilization(stationId: string, hours: number = 24): Promise<ChannelUtilizationRecord[]> {
    const sinceTime = Math.floor(Date.now() / 1000) - (hours * 3600);
    const result = await this.db
      .prepare(`
        SELECT * FROM station_channel_utilization
        WHERE station_id = ? AND timestamp >= ?
        ORDER BY timestamp ASC, channel ASC
      `)
      .bind(stationId, sinceTime)
      .all<ChannelUtilizationRecord>();
    return result.results || [];
  }

  async getLatestChannelUtilization(stationId: string): Promise<ChannelUtilizationRecord[]> {
    const latestTimestampRow = await this.db
      .prepare(`
        SELECT timestamp FROM station_channel_utilization
        WHERE station_id = ?
        ORDER BY timestamp DESC
        LIMIT 1
      `)
      .bind(stationId)
      .first<{ timestamp: number }>();

    if (!latestTimestampRow || !latestTimestampRow.timestamp) return [];

    const result = await this.db
      .prepare(`
        SELECT * FROM station_channel_utilization
        WHERE station_id = ? AND timestamp = ?
        ORDER BY channel ASC
      `)
      .bind(stationId, latestTimestampRow.timestamp)
      .all<ChannelUtilizationRecord>();
    return result.results || [];
  }

  async getLatestScanDuration(stationId: string): Promise<number | null> {
    const row = await this.db
      .prepare(`
        SELECT scan_duration_ms FROM station_latest_scan
        WHERE station_id = ?
      `)
      .bind(stationId)
      .first<{ scan_duration_ms: number | null }>();
    return row?.scan_duration_ms ?? null;
  }
}
