# ADR 0004: Automated Fleet OTA Updates, Action Runner Modernization, and Battery Telemetry History

## Status
Accepted

## Date
2026-11-15

## Context
As the `kreier/wifi` platform expanded to remote, unattended battery-powered and solar-powered stations (e.g. LILYGO T18 nodes deployed outdoors on Hofkoh / Baumhaus), two critical operational challenges arose:

1. **Firmware Distribution & OTA Orchestration**:
   - Manually pulling nodes from field locations for physical flashing is impractical.
   - Microcontrollers need reliable over-the-air (OTA) update discovery without exposing GitHub API secrets or placing high memory/TLS redirect burdens on the ESP32.
   - When new firmware (such as v1.0.4) is compiled and released via GitHub Actions (`build-release.yml`), fleet nodes must automatically detect, verify, and stream the update during scheduled cycles.
   - If updates fail due to transient network or TLS handshakes, nodes must gracefully recover without bricking or endlessly spinning in error loops.
   - GitHub Actions workflows running on Node 20 runtimes triggered deprecation alerts and forced execution under Node 24 runners.

2. **Battery Sensing & Time-Series Voltage Analytics**:
   - Field stations running on single-cell LiPo batteries (e.g. T18 with 100k/100k divider on ADC pin IO35) measure and transmit voltage every 10 minutes.
   - Previously, only the latest instantaneous `battery_mv` was recorded on the station metadata record. Without dedicated time-series storage, operators could not observe discharge trends, evaluate runtimes, or inspect voltage history over days, weeks, or months.

## Decision

### 1. Zero-Redirect Fleet OTA Delivery Pipeline
- **Cloudflare Edge OTA API**:
  Stations query `GET /api/ota?station_id=...&version=...&board=...` on boot and every hour.
  The Cloudflare Worker checks target assignments:
  - If a station has an assigned `target_version` (canary testing), that version is targeted.
  - Otherwise, the worker evaluates `env.STABLE_VERSION` (production fleet default).
  - Stations running older versions receive `update_available: true` with a streaming download link (`/api/ota/download/:filename`).
- **Binary Streaming via Worker Proxy**:
  The Cloudflare Worker acts as a direct streaming proxy for GitHub Releases (and optional Cloudflare R2 bucket storage). This delivers the binary over a single HTTPS stream with zero HTTP redirects, avoiding memory exhaustion and TLS re-negotiation failures on the ESP32 `httpUpdate` client.
- **Microcontroller Resilience & Retries**:
  `OtaManager::checkForUpdate` configures non-reusable HTTPS connections (`http.setReuse(false)`) and executes an automated single-retry backoff upon transient connection failures (`httpCode < 0`), eliminating spurious `-1` socket drops right after Wi-Fi association.

### 2. Time-Series Battery Ingestion & Dashboard Modeling (Migration 0008)
- **Database Schema**:
  Added migration `0008_battery_history.sql` introducing `station_battery_history` (`station_id`, `timestamp`, `battery_mv`) and adding `battery_mv` to the `measurements` table.
- **Continuous Sampling**:
  The ESP32 samples IO35 via `BatteryReader::readBatteryMilliVolts` every 10-minute cycle and includes `battery_mv` in cycle telemetry.
- **30-Day Retention**:
  The API returns up to 30 days of battery readings. Automated cron maintenance (`cron.ts`) purges records older than 30 days.
- **Discharge Trend & Runtime Estimation**:
  The web UI (released as v2.0.4) models LiPo discharge curves (3.3V to 4.2V):
  - Displays a 4-bar SVG battery indicator with calculated percentage left of the status badge.
  - Automatically identifies discharging phases and computes:
    - `"Running on battery for XXXh (DD days X hours)"`
    - `"Estimated runtime YYYh (DD days Y hours)"`
  - Provides a collapsible battery voltage chart with timeframe toggles (**Day**, **Week**, **Month**).

### 3. GitHub Actions Modernization (Node.js 24)
- Updated `.github/workflows/build-release.yml` and `.github/workflows/deploy-pages.yml` to the latest Node 24-native action releases:
  - `actions/checkout@v5` (upgraded from `@v4`)
  - `actions/setup-python@v6` (upgraded from `@v5`)
  - `softprops/action-gh-release@v3` (upgraded from `@v2`)
- Replaced hard-coded static version badges in `README.md` with dynamic Shields.io endpoints tracking the latest GitHub release.

## Consequences

### Positive
- Autonomous fleet nodes upgrade smoothly over the air within 1 hour of release deployment.
- Zero-redirect binary streaming eliminates HTTP 302 redirect memory exhaustion on the ESP32.
- Operators gain clear visibility into battery life, discharge rates, and power bank performance across day, week, and month timeframes.
- CI/CD pipelines run cleanly on Node 24 without runner deprecation warnings.

### Negative
- When bumping `STABLE_VERSION`, the Cloudflare Worker must be deployed alongside firmware releases so the edge endpoint advertises the new fleet version.
