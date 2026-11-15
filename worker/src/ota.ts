import { Env, StationRecord } from "./types";
import { Database } from "./db";

export async function handleOta(request: Request, env: Env): Promise<Response> {
  const url = new URL(request.url);

  // 1. Firmware Binary Download Endpoint: /api/ota/download/:filename
  if (url.pathname.startsWith("/api/ota/download/")) {
    const filename = decodeURIComponent(url.pathname.substring("/api/ota/download/".length));
    return await handleFirmwareDownload(filename, request, env);
  }

  // 2. OTA Check Endpoint: GET /api/ota?station_id=...&version=...&board=...
  if (request.method !== "GET") {
    return new Response(JSON.stringify({ error: "Method Not Allowed" }), {
      status: 405,
      headers: { "Content-Type": "application/json" }
    });
  }

  const stationId = url.searchParams.get("station_id")?.trim();
  const currentVersion = url.searchParams.get("version")?.trim() || "0.0.0";
  const board = url.searchParams.get("board")?.trim() || "esp32dev";

  if (!stationId) {
    return new Response(JSON.stringify({ error: "Missing required query parameter: station_id" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const db = new Database(env);
  const station = (await env.DB
    .prepare("SELECT station_id, firmware_version, target_version, ota_url, is_canary FROM stations WHERE station_id = ? LIMIT 1")
    .bind(stationId)
    .first()) as (StationRecord & { is_canary?: number; ota_url?: string }) | null;

  // Check if station has an explicit target_version assigned (e.g. for Canary testing)
  let targetVersion = station?.target_version?.trim();

  // If no canary/custom target version is assigned:
  if (!targetVersion) {
    // If station is designated as canary only and has no target, hold updates
    if (station && station.is_canary === 1) {
      return new Response(
        JSON.stringify({
          update_available: false,
          station_id: stationId,
          current_version: currentVersion,
          message: "Canary station holding on current version pending test assignment"
        }),
        { status: 200, headers: { "Content-Type": "application/json" } }
      );
    }
    // Otherwise use stable production version (fleet default)
    targetVersion = env.STABLE_VERSION || "1.0.2";
  }

  // Check if already running the target version
  if (currentVersion === targetVersion) {
    return new Response(
      JSON.stringify({
        update_available: false,
        station_id: stationId,
        current_version: currentVersion,
        target_version: targetVersion,
        message: "Firmware is already up to date"
      }),
      {
        status: 200,
        headers: { "Content-Type": "application/json" }
      }
    );
  }

  // Determine binary download URL:
  // Primary: Cloudflare R2 bucket proxy via Worker
  // Fallback: GitHub Releases direct link or Worker proxy
  const filename = `firmware-esp32-v${targetVersion}.bin`;
  let downloadUrl = station?.ota_url?.trim();

  if (!downloadUrl) {
    // Worker download route that serves from R2 or proxies from GitHub Releases
    downloadUrl = `${url.origin}/api/ota/download/${filename}`;
  }

  return new Response(
    JSON.stringify({
      update_available: true,
      station_id: stationId,
      current_version: currentVersion,
      target_version: targetVersion,
      board: board,
      url: downloadUrl
    }),
    {
      status: 200,
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "no-cache, no-store, must-revalidate"
      }
    }
  );
}

// Handles binary delivery with Cloudflare R2 as primary and GitHub Releases as backup
async function handleFirmwareDownload(filename: string, request: Request, env: Env): Promise<Response> {
  // 1. Try Cloudflare R2 Storage (Zero egress fees, high speed)
  if (env.OTA_BUCKET) {
    try {
      const r2Object = await env.OTA_BUCKET.get(filename);
      if (r2Object) {
        console.log(`[OTA] Serving ${filename} directly from Cloudflare R2 bucket`);
        const headers = new Headers();
        r2Object.writeHttpMetadata(headers);
        headers.set("Content-Type", "application/octet-stream");
        headers.set("ETag", r2Object.httpEtag);
        headers.set("Content-Disposition", `attachment; filename="${filename}"`);
        headers.set("Cache-Control", "public, max-age=86400");

        return new Response(r2Object.body, {
          status: 200,
          headers
        });
      }
    } catch (err) {
      console.warn("[OTA] R2 bucket lookup failed, falling back to GitHub Releases:", err);
    }
  }

  // 2. Backup / Fallback: Fetch directly from GitHub Releases and stream to ESP32 (Zero redirects for microcontroller)
  // Pattern: firmware-esp32-v0.9.0.bin -> tag v0.9.0
  const match = filename.match(/v(\d+\.\d+\.\d+)/);
  const tag = match ? `v${match[1]}` : "latest";
  const githubUrl = `https://github.com/kreier/wifi/releases/download/${tag}/${filename}`;

  console.log(`[OTA] Fetching firmware binary for ${filename} from GitHub Releases: ${githubUrl}`);
  try {
    const ghResponse = await fetch(githubUrl, {
      redirect: "follow",
      headers: { "User-Agent": "kreier-wifi-worker" }
    });

    if (!ghResponse.ok) {
      console.warn(`[OTA] GitHub Releases returned status ${ghResponse.status} for ${githubUrl}`);
      // Fallback to direct redirect if fetch failed
      return Response.redirect(githubUrl, 302);
    }

    const headers = new Headers();
    headers.set("Content-Type", "application/octet-stream");
    const len = ghResponse.headers.get("Content-Length");
    if (len) headers.set("Content-Length", len);
    headers.set("Content-Disposition", `attachment; filename="${filename}"`);
    headers.set("Cache-Control", "public, max-age=86400");

    return new Response(ghResponse.body, {
      status: 200,
      headers
    });
  } catch (fetchErr) {
    console.error("[OTA] Error proxying GitHub Releases binary:", fetchErr);
    return Response.redirect(githubUrl, 302);
  }
}

// Admin / Canary API: Set or clear canary target version for a station
export async function handleSetStationTargetVersion(stationId: string, request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response(JSON.stringify({ error: "Method Not Allowed" }), { status: 405 });
  }

  try {
    const body = (await request.json()) as {
      target_version?: string | null;
      is_canary?: boolean | number;
      ota_url?: string | null;
    };

    const targetVersion = body.target_version?.trim() || null;
    const isCanary = body.is_canary ? 1 : 0;
    const otaUrl = body.ota_url?.trim() || null;

    await env.DB
      .prepare(`
        UPDATE stations SET
          target_version = ?,
          is_canary = ?,
          ota_url = ?
        WHERE station_id = ?
      `)
      .bind(targetVersion, isCanary, otaUrl, stationId)
      .run();

    return new Response(
      JSON.stringify({
        success: true,
        station_id: stationId,
        target_version: targetVersion,
        is_canary: isCanary,
        ota_url: otaUrl
      }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    );
  } catch (err: unknown) {
    const errMsg = err instanceof Error ? err.message : String(err);
    return new Response(JSON.stringify({ error: "Failed to update target version", message: errMsg }), {
      status: 500,
      headers: { "Content-Type": "application/json" }
    });
  }
}
