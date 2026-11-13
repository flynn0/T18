import { Env, RegisterRequest } from "./types";
import { Database } from "./db";

export async function handleRegister(request: Request, env: Env): Promise<Response> {
  if (request.method !== "POST") {
    return new Response(JSON.stringify({ error: "Method not allowed" }), {
      status: 405,
      headers: { "Content-Type": "application/json" }
    });
  }

  let body: RegisterRequest;
  try {
    body = await request.json<RegisterRequest>();
  } catch {
    return new Response(JSON.stringify({ error: "Invalid JSON body" }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }

  const { station_id, description, bootstrap_secret } = body;

  if (!station_id || !bootstrap_secret) {
    return new Response(
      JSON.stringify({ error: "station_id and bootstrap_secret are required" }),
      { status: 400, headers: { "Content-Type": "application/json" } }
    );
  }

  // Validate bootstrap secret against Worker environment secret
  const expectedSecret = env.MASTER_PHRASE;
  if (!expectedSecret || bootstrap_secret !== expectedSecret) {
    return new Response(
      JSON.stringify({ error: "Invalid bootstrap secret" }),
      { status: 401, headers: { "Content-Type": "application/json" } }
    );
  }

  // Generate cryptographically secure UUIDv4 / 32-byte token
  const deviceToken = crypto.randomUUID();

  // Persist station and token in D1
  const db = new Database(env);
  await db.upsertStation(station_id, description || "", deviceToken);

  return new Response(
    JSON.stringify({
      success: true,
      station_id,
      device_token: deviceToken,
      message: "Station registered successfully. Save token in NVS and purge bootstrap secret."
    }),
    {
      status: 200,
      headers: { "Content-Type": "application/json" }
    }
  );
}
