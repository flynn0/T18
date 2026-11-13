import { Env } from "./types";
import { Database } from "./db";

export async function handleApiStations(_request: Request, env: Env): Promise<Response> {
  const db = new Database(env);
  const stations = await db.listStations();

  return new Response(JSON.stringify({ stations }), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Access-Control-Allow-Origin": "*"
    }
  });
}

export async function handleApiStationDetail(stationId: string, _request: Request, env: Env): Promise<Response> {
  const db = new Database(env);
  const station = await db.getStationById(stationId);

  if (!station) {
    return new Response(JSON.stringify({ error: "Station not found" }), {
      status: 404,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*"
      }
    });
  }

  // Remove secret token before returning public metadata
  const publicMeta = {
    station_id: station.station_id,
    description: station.description,
    created_at: station.created_at,
    last_seen: station.last_seen
  };

  const latestMeasurements = await db.getLatestMeasurements(stationId);
  const latestSurvey = await db.getLatestSurvey(stationId);
  const inactiveSurvey = await db.getInactiveSurveys(stationId);
  const recentMeasurements = await db.getRecentMeasurements(stationId, 500); // 24 hours of 10-min samples across monitored networks
  const dailySummaries = await db.getDailySummaries(stationId, 30);

  return new Response(
    JSON.stringify({
      station: publicMeta,
      latest_measurements: latestMeasurements,
      latest_survey: latestSurvey,
      inactive_survey: inactiveSurvey,
      recent_history: recentMeasurements,
      daily_summaries: dailySummaries
    }),
    {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*"
      }
    }
  );
}

export async function handleApiExport(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);
  const stationId = url.searchParams.get("station_id");
  const format = url.searchParams.get("format") || "json";

  const db = new Database(env);
  if (!stationId) {
    return new Response(JSON.stringify({ error: "station_id parameter required" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const measurements = await db.getRecentMeasurements(stationId, 1000);

  if (format === "csv") {
    let csv = "timestamp,ssid,assoc_ms,dhcp_ms,cloudflare_ms,google_ms,microsoft_ms,facebook_ms,netflix_ms,apple_ms,status\n";
    for (const m of measurements) {
      csv += `${m.timestamp},"${m.ssid}",${m.assoc_ms ?? ""},${m.dhcp_ms ?? ""},${m.cloudflare_ms ?? ""},${m.google_ms ?? ""},${m.microsoft_ms ?? ""},${m.facebook_ms ?? ""},${m.netflix_ms ?? ""},${m.apple_ms ?? ""},"${m.status ?? ""}"\n`;
    }
    return new Response(csv, {
      status: 200,
      headers: {
        "Content-Type": "text/csv",
        "Content-Disposition": `attachment; filename="${stationId}-telemetry.csv"`
      }
    });
  }

  return new Response(JSON.stringify(measurements, null, 2), {
    status: 200,
    headers: {
      "Content-Type": "application/json",
      "Content-Disposition": `attachment; filename="${stationId}-telemetry.json"`
    }
  });
}
