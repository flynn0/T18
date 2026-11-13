# ADR 0002: Zero-Trust Per-Device Token Provisioning via Bootstrap Secret

## Status
Accepted

## Date
2026-11-13

## Context
Deploying IoT nodes across diverse physical locations (offices, schools, server rooms, labs) creates physical security challenges. If all devices share a single static API key or if a universal write key is embedded into the firmware image or stored in flash storage:
1. **Extraction Vulnerability**: An attacker with physical access to an ESP32 can easily extract flash contents via `esptool.py`, recovering the shared secret and obtaining full write access to the telemetry backend.
2. **Revocation Blast Radius**: If the shared key is compromised, rotating it requires re-flashing or reconfiguring every deployed device in the field.
3. **Impersonation**: A compromised device can spoof data under the identity of other stations, corrupting metric integrity.

## Decision
We implement a **Zero-Trust Per-Device Token Provisioning** model using a temporary bootstrap secret (`MASTER_PHRASE`):

1. **Captive Portal Bootstrap**:
   - During initial commissioning (or when forced via the 3-second `BOOT` button hold), the device operator connects to the soft-AP (`WIFI-Mon-XXXX`) captive portal at `192.168.4.1`.
   - The operator inputs `STATION_ID`, `STATION_DESC`, monitored Wi-Fi SSIDs/passwords, Worker base URL, and a transient `MASTER_PHRASE`.
   - The `MASTER_PHRASE` is held only temporarily in volatile memory / temporary LittleFS config.

2. **One-Time Registration Handshake (`POST /register`)**:
   - The ESP32 connects to the first configured Wi-Fi network and makes an HTTPS request to `POST /register` with:
     ```json
     {
       "station_id": "lab-floor-2",
       "description": "Lab 2nd floor ceiling probe",
       "bootstrap_secret": "<MASTER_PHRASE>"
     }
     ```
   - The Cloudflare Worker validates `bootstrap_secret === env.MASTER_PHRASE`.
   - If valid, the Worker generates a unique, cryptographically strong 32-byte UUID (`crypto.randomUUID()`), stores `station_id` and `device_token` in the D1 `stations` table, and returns:
     ```json
     {
       "device_token": "a4f89d32-8419-4b68-b732-23c72b22f180"
     }
     ```

3. **Secure Persistence & Secret Erasure**:
   - On receiving `200 OK` with the token, the ESP32 writes `device_token` to hardware Non-Volatile Storage (NVS) using the ESP32 `Preferences` API.
   - The ESP32 **permanently erases** `MASTER_PHRASE` from LittleFS storage (`config.json`) and zeroes out the string in RAM.
   - If registration fails (e.g. invalid bootstrap secret or offline network), `MASTER_PHRASE` is retained only to retry on the next cycle or can be re-entered in the portal.

4. **Telemetry Ingestion (`POST /data`)**:
   - All subsequent telemetry submissions must provide `Authorization: Bearer <device_token>`.
   - The Worker validates the token against D1 and rejects unauthorized payloads with `401 Unauthorized`.

## Consequences

### Positive
- **No Master Secret on Device Flash**: After initial provisioning, the device contains only its own unique `device_token`. Dumping flash reveals zero secrets capable of provisioning other devices or altering foreign data.
- **Granular Revocation**: A lost, stolen, or rogue node can be invalidated in D1 by deleting or updating its individual token in seconds without impacting the rest of the fleet.
- **Auditable Registration**: The Worker records creation timestamps and metadata for every registered station.

### Negative
- Initial onboarding requires the operator to know the `MASTER_PHRASE`.
- If an ESP32 is completely wiped (`erase_flash`), it must be re-provisioned via the captive portal.
