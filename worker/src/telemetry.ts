import { Env, TelemetryCyclePayload, MeasurementRecord, ScanSurveyRecord } from "./types";
import { Database } from "./db";
import { invalidateMemoryCache } from "./api";

export async function handleTelemetry(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response(JSON.stringify({ error: "Method not allowed" }), {
      status: 405,
      headers: { "Content-Type": "application/json" }
    });
  }

  // 1. Authenticate Bearer <device_token>
  const authHeader = request.headers.get("Authorization");
  if (!authHeader || !authHeader.startsWith("Bearer ")) {
    return new Response(JSON.stringify({ error: "Missing or malformed Authorization header" }), {
      status: 401,
      headers: { "Content-Type": "application/json" }
    });
  }

  const token = authHeader.substring(7).trim();
  const db = new Database(env);
  const station = await db.getStationByToken(token);

  if (!station) {
    return new Response(JSON.stringify({ error: "Invalid device token" }), {
      status: 401,
      headers: { "Content-Type": "application/json" }
    });
  }

  // 2. Parse payload: could be a single TelemetryCyclePayload or an array of cycles
  let rawBody: unknown;
  try {
    rawBody = await request.json();
  } catch {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const cycles: TelemetryCyclePayload[] = Array.isArray(rawBody) ? rawBody : [rawBody as TelemetryCyclePayload];

  const measurementsToInsert: MeasurementRecord[] = [];
  const surveysToInsert: ScanSurveyRecord[] = [];
  const serverNow = Math.floor(Date.now() / 1000);
  let latestTimestamp = station.last_seen || 0;
  let latestScanDuration: number | null = null;
  let insertedChannels = 0;

  for (const cycle of cycles) {
    let cycleTime = cycle.timestamp || serverNow;
    // Guard against future timestamps caused by NTP / clock skew on IoT probe
    if (cycleTime > serverNow + 30) {
      cycleTime = serverNow;
    }
    if (cycleTime > latestTimestamp) {
      latestTimestamp = cycleTime;
    }

    const scanDuration = typeof cycle.scan_duration_ms === "number" ? cycle.scan_duration_ms : null;
    if (scanDuration !== null) {
      latestScanDuration = scanDuration;
    }

    // Process network latency measurements
    if (Array.isArray(cycle.networks)) {
      for (const net of cycle.networks) {
        if (!net.ssid) continue;
        measurementsToInsert.push({
          station_id: station.station_id,
          timestamp: cycleTime,
          ssid: net.ssid,
          bssid: net.bssid || null,
          channel: typeof net.channel === "number" ? net.channel : null,
          assoc_ms: typeof net.assoc_ms === "number" ? net.assoc_ms : null,
          dhcp_ms: typeof net.dhcp_ms === "number" ? net.dhcp_ms : null,
          google_ms: typeof net.http_latencies?.google === "number" ? net.http_latencies.google : null,
          cloudflare_ms: typeof net.http_latencies?.cloudflare === "number" ? net.http_latencies.cloudflare : null,
          microsoft_ms: typeof net.http_latencies?.microsoft === "number" ? net.http_latencies.microsoft : null,
          facebook_ms: typeof net.http_latencies?.facebook === "number" ? net.http_latencies.facebook : null,
          netflix_ms: typeof net.http_latencies?.netflix === "number" ? net.http_latencies.netflix : null,
          apple_ms: typeof net.http_latencies?.apple === "number" ? net.http_latencies.apple : null,
          gateway_ms: typeof net.gateway_ms === "number" ? net.gateway_ms : null,
          dns_ipv4_ms: typeof net.dns_ipv4_ms === "number" ? net.dns_ipv4_ms : null,
          dns_ipv6_ms: typeof net.dns_ipv6_ms === "number" ? net.dns_ipv6_ms : null,
          scan_duration_ms: scanDuration,
          status: net.status || "connected"
        });
      }
    }

    // Process daily network diagnostics if present in this cycle
    if (cycle.diagnostics) {
      await db.saveNetworkDiagnostics(station.station_id, cycleTime, cycle.diagnostics);
    }

    // Process 30-minute promiscuous channel utilization metrics if present
    if (Array.isArray(cycle.channel_utilization) && cycle.channel_utilization.length > 0) {
      insertedChannels += await db.saveChannelUtilization(
        station.station_id,
        cycleTime,
        scanDuration,
        cycle.channel_utilization
      );
    }

    // Process scan surveys
    if (Array.isArray(cycle.scan_survey)) {
      for (const item of cycle.scan_survey) {
        surveysToInsert.push({
          station_id: station.station_id,
          timestamp: cycleTime,
          ssid: item.ssid || "<Hidden>",
          bssid: item.bssid || null,
          rssi: typeof item.rssi === "number" ? item.rssi : null,
          quality: typeof item.quality === "number" ? item.quality : null,
          encryption: item.encryption || "Unknown",
          phy_mode: item.phy_mode || "WIFI_4",
          channel: typeof item.channel === "number" ? item.channel : null,
          band: item.band || "2.4 GHz"
        });
      }
    }
  }

  // 3. Batch insert into D1 with optimized storage
  const insertedM = await db.insertMeasurements(measurementsToInsert);
  const insertedS = await db.saveSurveys(station.station_id, latestTimestamp, surveysToInsert, latestScanDuration);
  await db.updateStationLastSeen(station.station_id, serverNow);

  // 4. Invalidate overview caches so fresh data is visible immediately
  invalidateMemoryCache(station.station_id);
  await db.invalidateOverviewCache(station.station_id);

  return new Response(
    JSON.stringify({
      success: true,
      station_id: station.station_id,
      inserted_measurements: insertedM,
      inserted_surveys: insertedS,
      inserted_channels: insertedChannels,
      cycles_processed: cycles.length
    }),
    {
      status: 200,
      headers: { "Content-Type": "application/json" }
    }
  );
}
