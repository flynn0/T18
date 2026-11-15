# kreier/wifi

[![Version](https://img.shields.io/badge/version-0.8.0-blue.svg)](https://github.com/kreier/wifi/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](https://opensource.org/licenses/Apache-2.0)
[![Cloudflare Worker](https://img.shields.io/badge/Cloudflare-wifi.kreier.workers.dev-F38020?logo=cloudflare)](https://wifi.kreier.workers.dev/)
[![GitHub Pages](https://img.shields.io/badge/GitHub%20Pages-kreier.github.io%2Fwifi-22c55e?logo=github)](https://kreier.github.io/wifi/)

> Autonomous Wi-Fi Spectrum & Latency Telemetry Fleet Node for ESP32 with Cloudflare Worker, D1 SQLite Database, and GitHub Pages.

**Live Telemetry Dashboards**:
- 🌐 **Cloudflare Edge Dashboard**: [https://wifi.kreier.workers.dev/](https://wifi.kreier.workers.dev/)
- 📄 **GitHub Pages Mirror**: [https://kreier.github.io/wifi/](https://kreier.github.io/wifi/)

**Direct Station Access**:
Both the Cloudflare Worker and GitHub Pages support deep-linking directly to specific monitoring stations:
- View Station **9E1-9**: [https://wifi.kreier.workers.dev/9E1-9](https://wifi.kreier.workers.dev/9E1-9) or [https://kreier.github.io/wifi/9E1-9](https://kreier.github.io/wifi/9E1-9)

---

## Key Architectural Features

The `kreier/wifi` platform is built on an edge-native, zero-trust, and power-optimized architecture designed for autonomous continuous Wi-Fi fleet telemetry:

- **Serverless Edge Storage & Analytics ([ADR 0001: Cloudflare D1 Storage](docs/adr/0001-d1-over-git-storage.md))**:
  All telemetry cycles, spectrum scans, channel congestion metrics, and daily rollups are stored in **Cloudflare D1** (serverless SQLite at edge). Automated daily cron triggers aggregate 24-hour summaries and enforce 30-day raw data retention.
- **Zero-Trust Device Provisioning ([ADR 0002: Per-Device Token Provisioning](docs/adr/0002-per-device-token-provisioning.md))**:
  Devices are commissioned via an on-device captive portal using a one-time bootstrap phrase (`MASTER_PHRASE`). Upon the first successful connection, a unique cryptographically-random Bearer token is negotiated with the backend and stored in hardware NVS; the master phrase is permanently erased from LittleFS flash and RAM.
- **Dual-Compatible Firmware Layout ([ADR 0003: Single-Source Dual Build](docs/adr/0003-dual-build-layout.md))**:
  Maintains a unified modular codebase in `firmware/include/` that compiles seamlessly in both **PlatformIO** (automated CI/CD pipelines, multi-board target flags) and **Arduino IDE** (single-click desktop flashing without external CLI dependencies).
- **2.4 GHz Promiscuous RF Contention Profiling**:
  Measures all 13 channels every 30 minutes with 2-second dwell times using ESP-IDF promiscuous mode. Classifies frames (Management, Data, Control), records 4-bracket RSSI histograms, counts unique transmitter MACs, and estimates PHY-modeled airtime percentage.
- **10-Minute Spectrum Scan Variance Tracking**:
  Monitors the duration of full-spectrum scans in milliseconds on every 10-minute cycle, correlating scan latency with RF contention.
- **Local Network & Dual-Stack Diagnostics**:
  Measures local gateway TCP SYN round-trip time, performs IPv4 vs. IPv6 DNS resolution benchmarking, detects captive portal HTTP canary hijacking, and executes automated daily gateway firewall audits.
- **Offline Resilience via LittleFS Ring Buffer**:
  When Wi-Fi connectivity is unavailable, cycles are appended as JSONL into a dedicated LittleFS spool ring buffer (capped at 500 KB FIFO), ensuring zero data loss during network outages.
- **Dual Web Dashboards**:
  Edge-hosted server-rendered dashboard on Cloudflare Worker (`wifi.kreier.workers.dev`) coupled with a GitHub Pages static client mirror (`kreier.github.io/wifi`), both supporting client-side station deep-linking and theme switching.

---

## Architecture Overview

```mermaid
flowchart TD
    subgraph Commissioning ["1. Commissioning Phase"]
        AP["Soft-AP: WIFI-Mon-XXXX<br/>IP: 192.168.4.1"]
        Portal["Captive Portal Web UI<br/>(SSIDs, URL, MASTER_PHRASE)"]
        Reg["POST /register<br/>{station_id, bootstrap_secret}"]
        AP --> Portal --> Reg
    end

    subgraph ESP32 ["2. ESP32 Node Lifecycle (Every 10 min)"]
        Boot["Cold Boot or Wake"]
        NTP["NTP Sync (every 24h)<br/>pool.ntp.org"]
        Scan["Wi-Fi 2.4 GHz Scan Survey<br/>(SSID, RSSI, Phy Mode, Enc)"]
        Bench["Sequential Benchmarks (up to 5 SSIDs)<br/>- Assoc Time (ms)<br/>- DHCP Lease Time (ms)<br/>- HTTP Latency (5 targets + Gateway)"]
        Spool["LittleFS Ring Buffer<br/>/spool/pending.jsonl (Max 500 KB)"]
        Upload["POST /data<br/>Authorization: Bearer <device_token>"]
        Sleep["ESP32 Light Sleep<br/>(Remainder of 10 min)"]

        Boot --> NTP --> Scan --> Bench --> Spool --> Upload --> Sleep
        Sleep -.->|Timer Wakeup| Boot
    end

    subgraph Backend ["3. Cloudflare Edge Backend & Dashboards"]
        Worker["Cloudflare Worker<br/>(TypeScript API & Router)"]
        D1[("Cloudflare D1<br/>SQLite Database")]
        Cron["Daily Cron Trigger (00:15 UTC)<br/>- 30-day Raw Purge<br/>- Daily Rollup Aggregation"]
        CFUI["Cloudflare Edge UI<br/>https://wifi.kreier.workers.dev"]
        GHUI["GitHub Pages UI<br/>https://kreier.github.io/wifi"]

        Reg --> Worker
        Upload --> Worker
        Worker <--> D1
        Cron --> D1
        D1 --> CFUI
        Worker -.->|API /api/stations| GHUI
    end
```

---

## Repository Structure

```
kreier/wifi/
├── .github/
│   └── workflows/
│       └── deploy-pages.yml  # GitHub Actions workflow deploying to GitHub Pages
├── docs/                     # Static GitHub Pages web files & ADRs
│   ├── index.html            # Standalone responsive web dashboard
│   ├── 404.html              # SPA client router for direct station routes (e.g. /9E1-9)
│   └── adr/                  # Architecture Decision Records
│       ├── 0001-d1-over-git-storage.md
│       ├── 0002-per-device-token-provisioning.md
│       └── 0003-dual-build-layout.md
├── firmware/                 # Dual-compatible firmware source
│   ├── platformio.ini        # PlatformIO targets: t18, devkitv1, t8
│   ├── src/
│   │   └── main.cpp          # PlatformIO compilation entrypoint
│   ├── wifi/
│   │   └── wifi.ino          # Arduino IDE sketch entrypoint
│   └── include/              # Shared modular firmware components
│       ├── app_config.h      # LittleFS config & NVS token management
│       ├── app_main.h        # Top-level state machine & lifecycle
│       ├── app_state.h       # Shared data models & structures
│       ├── api_client.h      # HTTPS /register and /data client
│       ├── board_pins.h      # Compile-time board LED & pin mappings
│       ├── captive_portal.h  # Soft-AP, captive DNS server, & setup UI
│       ├── latency_prober.h  # Association, DHCP, and 5-target HTTP probes
│       ├── channel_analyzer.h # Promiscuous 13-channel utilization & airtime analyzer
│       ├── led_controller.h  # Non-blocking LED status indicator
│       ├── ntp_sync.h        # SNTP synchronization engine (24h interval)
│       ├── spool_manager.h   # LittleFS ring buffer (500 KB ceiling)
│       └── wifi_scanner.h    # 802.11 b/g/n spectrum analyzer
├── worker/                   # Cloudflare Worker & D1 Backend
│   ├── wrangler.toml         # Worker bindings, D1 config, & cron schedule
│   ├── package.json          # TypeScript & Cloudflare Worker dependencies
│   ├── tsconfig.json         # TypeScript compiler configuration
│   ├── migrations/           # D1 migrations (0001–0006: telemetry, diagnostics, channel utilization)
│   └── src/
│       ├── index.ts          # Request router and scheduled event handler
│       ├── types.ts          # Domain interfaces & payloads
│       ├── db.ts             # D1 database query abstraction layer
│       ├── register.ts       # POST /register endpoint handler
│       ├── telemetry.ts      # POST /data telemetry ingestion handler
│       ├── api.ts            # Public GET /api endpoints (stations, details, export)
│       ├── cron.ts           # Daily rollup aggregation & 30-day retention pruning
│       └── frontend.ts       # Responsive single-page dashboard HTML/CSS/JS
├── data/                     # Reserved for future permanent telemetry exports
│   └── .gitkeep
├── LICENSE                   # Apache 2.0 License
└── README.md
```

---

## Hardware Targets & Pinout

Designed for generic ESP32 modules (4 MB Flash, 520 KB SRAM, no external PSRAM required). Three pre-configured environments define board-specific status LED pins:

| Environment | Target Board | Status LED Pin | Default Flag |
| :--- | :--- | :---: | :--- |
| `t18` | **T18 Custom Board** | **GPIO 5** | `-D STATUS_LED_PIN=5` *(Default)* |
| `devkitv1` | **ESP32 DevKit V1** (NodeMCU-32S) | **GPIO 2** | `-D STATUS_LED_PIN=2` |
| `t8` | **T8 ESP32 Board** | **GPIO 21** | `-D STATUS_LED_PIN=21` |

- **BOOT Button**: Standard active-low button on **GPIO 0**.
  - Hold for **3 seconds** at boot or wake to force the device into Captive Portal mode.

---

## Direct Station Routing

Both dashboards support deep-linking directly to a station's real-time metrics and survey:

- **Cloudflare Edge**: `https://wifi.kreier.workers.dev/:station_id` (e.g. [`https://wifi.kreier.workers.dev/9E1-9`](https://wifi.kreier.workers.dev/9E1-9))
- **GitHub Pages**: `https://kreier.github.io/wifi/:station_id` (e.g. [`https://kreier.github.io/wifi/9E1-9`](https://kreier.github.io/wifi/9E1-9))

Switching stations dynamically updates the URL via `history.pushState` without reloading the page.

---

## Zero-Trust Token Provisioning

To prevent secret leakage from physical extraction of flash dumps:
1. Operator inputs `MASTER_PHRASE` (bootstrap secret) into the captive portal.
2. ESP32 connects to Wi-Fi and issues `POST /register`:
   ```json
   {
     "station_id": "9E1-9",
     "description": "Sky Garden",
     "bootstrap_secret": "<MASTER_PHRASE>"
   }
   ```
3. Worker validates `bootstrap_secret === env.MASTER_PHRASE`, generates a cryptographic 32-byte UUID `device_token`, upserts the station in D1, and returns:
   ```json
   { "device_token": "a4f89d32-8419-4b68-b732-23c72b22f180" }
   ```
4. ESP32 saves `device_token` to Non-Volatile Storage (`Preferences`), zeroes out `MASTER_PHRASE` in RAM, and removes it permanently from LittleFS `/config.json`.
5. All future telemetry uses `Authorization: Bearer <device_token>`.

---

## Measurement Engine (10-Minute Cadence)

Every 10 minutes, the probe executes:

1. **24-Hour NTP Sync**: Checks and resynchronizes local time against `pool.ntp.org` once every 24 hours.
2. **Spectrum Survey**: Scans all visible 2.4 GHz BSSIDs recording:
   - SSID (including `<Hidden>`)
   - BSSID (`XX:XX:XX:XX:XX:XX`)
   - RSSI (dBm) & estimated Link Quality % (`clamp(2 * (rssi + 100), 0, 100)`)
   - Encryption (`Open`, `WEP`, `WPA`, `WPA2`, `WPA3`, `WPA2/WPA3`)
   - 802.11 Phy Mode (`WIFI_1` for 11b, `WIFI_3` for 11g, `WIFI_4` for 11n)
   - Channel (1 to 14) and Band (`2.4 GHz`)
3. **Sequential Network Benchmarks** (Up to 5 configured SSIDs):
   - **Association Latency** ($T_{\text{assoc}}$ in ms): Layer 2 connection time.
   - **DHCP IP Acquisition** ($T_{\text{dhcp}}$ in ms): Layer 3 lease assignment time.
   - **HTTP Latency** (3000 ms timeout per target):
     - **Google**: `http://connectivitycheck.gstatic.com/generate_204`
     - **Cloudflare**: `http://cp.cloudflare.com/`
     - **Microsoft**: `http://www.msftconnecttest.com/connecttest.txt`
     - **Facebook**: `http://www.facebook.com/`
     - **Netflix**: `http://fast.com/`
4. **Spool Ring Buffer**: Records are spooled to `/spool/pending.jsonl` (capped at 500 KB FIFO ring buffer).
5. **Telemetry Flush**: Uploads spooled data to `POST /data`. Only after receiving `200 OK` is the spool buffer cleared.
6. **ESP32 Light Sleep**: Subsystems and radio are deactivated; CPU enters light sleep for the remainder of the 600-second window.

---

## Cloudflare Worker & D1 Setup

### 1. Install & Authenticate
```bash
cd worker
npm install
npx wrangler login
```

### 2. Create D1 Database & Run Migrations
```bash
# Create the D1 database
npx wrangler d1 create wifi-mon-db

# Apply schema migrations
npx wrangler d1 migrations apply wifi-mon-db --remote
```

### 3. Set Master Bootstrap Secret
```bash
npx wrangler secret put MASTER_PHRASE
# Enter your secure bootstrap secret
```

### 4. Deploy Worker
```bash
npx wrangler deploy
```

---

## Firmware Build & Flash

### Option A: PlatformIO (Recommended)
```bash
cd firmware

# Build and upload for T18 (LED on GPIO 5)
pio run -e t18 -t upload

# Build and upload for DevKit V1 (LED on GPIO 2)
pio run -e devkitv1 -t upload

# Build and upload for T8 (LED on GPIO 21)
pio run -e t8 -t upload

# Monitor Serial Output
pio device monitor -b 115200
```

### Option B: Arduino IDE
1. Open Arduino IDE 2.x.
2. Open [`firmware/wifi/wifi.ino`](firmware/wifi/wifi.ino).
3. Under **Tools > Board**, select **ESP32 Dev Module**.
4. Set **Partition Scheme** to **Default 4MB with spiffs/littlefs**.
5. Install the **ArduinoJson** library (v7.x) via the Library Manager.
6. Select your target LED pin at the top of `wifi.ino` if not using T18 defaults:
   ```cpp
   #define STATUS_LED_PIN 5  // Pin 5 for T18, 2 for DevKitV1, 21 for T8
   ```
7. Click **Upload**.

---

## API Reference

### `POST /register`
Registers a new station using the bootstrap secret.
- **Request Body**:
  ```json
  {
    "station_id": "9E1-9",
    "description": "Sky Garden",
    "bootstrap_secret": "<MASTER_PHRASE>"
  }
  ```
- **Response `200 OK`**:
  ```json
  {
    "success": true,
    "station_id": "9E1-9",
    "device_token": "d748f220-4f51-4e08-9df8-0d12e6973347"
  }
  ```

### `POST /data`
Submits raw telemetry cycle data.
- **Header**: `Authorization: Bearer <device_token>`
- **Request Body**: Single telemetry object or JSON array of spooled cycles.
- **Response `200 OK`**:
  ```json
  {
    "success": true,
    "station_id": "9E1-9",
    "inserted_measurements": 3,
    "inserted_surveys": 39,
    "cycles_processed": 1
  }
  ```

### `GET /api/stations`
Returns public list of registered stations, their descriptions, and last-seen timestamps.

### `GET /api/station/:id`
Returns detailed station metadata, latest monitored network measurements, visible 2.4 GHz spectrum survey, 24-hour raw history, and 30-day daily rollups.

### `GET /api/export?station_id=:id&format=csv|json`
Downloads recent telemetry for analysis or archival into the `data/` repository directory.

---

## Network Diagnostics & Firewall Monitoring (v0.7.0)

Version `0.7.0` introduces real-time gateway and dual-stack DNS monitoring alongside automated daily security and firewall audits:

1. **Real-Time Measurement Additions (Every 10-Minute Polling Cycle)**:
   - **Gateway RTT**: Measures local TCP SYN-ACK RTT to default gateway (`WiFi.gatewayIP()`). Immediately differentiates local AP/firewall bottlenecks from ISP uplink degradation.
   - **Dual-Stack DNS Resolution (IPv4 `A` vs IPv6 `AAAA`)**: Measures latency to resolve hostnames via IPv4 vs IPv6. Accurately detects broken dual-stack networks where upstream routers advertise IPv6 DNS but blackhole traffic, causing 30-second application timeouts.
   - **Captive Portal / HTTP Canary Trap**: Detects HTTP redirection or 302/200 hijack when probing standard canary endpoints (`connectivitycheck.gstatic.com/generate_204`).

2. **Automated Daily In-Depth Diagnostics (Every 24 Hours)**:
   - **Gateway & Firewall Inspection**: Probes gateway port 80 & 443 to detect web management interfaces (`pfSense`, `OpenWrt`, `OPNsense`, or generic router WebUIs) and checks whether HTTP-to-HTTPS redirect traps are present.
   - **Path MTU (PMTUD) & MSS Clamping**: Sweeps packet reachability across 1500 (Standard), 1492 (PPPoE), and 1420 (VPN) MTUs to diagnose silent packet drops and TLS handshake stalls.
   - **DNS Rebinding Protection**: Audits whether public queries resolving to private RFC 1918 ranges are suppressed.
   - **Egress Firewall Rules**: Validates outbound connectivity to critical ports (DNS 53, HTTP 80, HTTPS 443, NTP 123, SSH 22, DoT 853).

3. **Dashboard Summary & Expandable View**:
   - High-level status bar with color-coded pills for Gateway RTT, Firewall Detection (e.g. pfSense Exposed vs Secured Gateway), Dual-Stack DNS Health, and Captive Interception.
   - Expandable **Detailed Audit Drawer** displaying comprehensive 4-quadrant diagnostic breakdowns for all station types.
   - Integrated **Gateway RTT** plotted alongside Google, Cloudflare, Microsoft, Netflix, and Facebook on the 24-hour latency trend graphs.

---

## 2.4 GHz Promiscuous Spectrum Congestion & Scan Time Analysis (v0.8.0)

Version `0.8.0` introduces RF channel utilization and contention measurement via ESP-IDF promiscuous mode, resolving the limitation of only tracking station count:

1. **Promiscuous Mode Channel Contention Profiling (Every 30 Minutes)**:
   - **13-Channel Full Spectrum Sweep**: Sweeps 2.4 GHz channels 1 through 13 with a 2.0-second dwell per channel (26 seconds total measurement time).
   - **Traffic Classification**: Separately tallies 802.11 **Management frames** (beacons, probe requests/responses), **Data frames** (QoS & user payloads), and **Control frames** (RTS, CTS, ACK, BlockACK).
   - **Unique Transmitter Identification**: Extracts 802.11 Transmitter Addresses (TA / SA) from frame MAC headers to quantify active competing transmitters per channel.
   - **RSSI Histogram Distribution**: Categorizes packet signal strength into 4 buckets (`< -80 dBm`, `-80 to -71 dBm`, `-70 to -61 dBm`, `>= -60 dBm`).
   - **PHY-Modeled Airtime Utilization %**: Computes channel airtime consumption based on PLCP preamble duration, packet length, and modulation coding scheme (Legacy OFDM/DSSS or 802.11n HT MCS 0–7) with SIFS/ACK interframe spacing.

2. **Spectrum Scan Duration Variance Tracking (Every 10 Minutes)**:
   - Records the exact duration in milliseconds required for `WiFi.scanNetworks(false, true)` across all available channels.
   - Correlates scan time variance with surrounding RF congestion and channel density.

3. **Interactive 24-Hour Dashboard Visualization**:
   - **Channel View Selector**: Instant toggles between:
     - **All Channels Combined**: Overall average 2.4 GHz airtime consumption curve.
     - **Channels 1, 6, 11 (Non-Overlapping)**: Isolates the primary non-overlapping channels in Sky Blue, Amber, and Emerald.
     - **All Channels Individual (1–13)**: Displays 13 distinct channel lines with dedicated color mapping.
   - **Scan Duration Overlay**: Optional secondary Y-axis overlay plotting spectrum scan duration (ms) against channel utilization.
   - **13-Channel Breakdown Matrix Table**: Expandable drawer with per-channel progress bars, frame type breakdowns, unique transmitters, and 4-tier stacked RSSI distribution mini-bars.

---

## License

This project is licensed under the **Apache License 2.0** — see the [LICENSE](LICENSE) file for details.
