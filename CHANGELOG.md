# Changelog

All notable changes to the `kreier/T18` platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.1] - 2026-11-16

### Added
- **Firmware (v1.0.1)**: Multi-network fallback data upload:
  - If the primary monitored network fails to connect or lacks internet access, the node automatically iterates through all other configured monitored networks (Networks 2, 3...) to upload telemetry.
- **Firmware (v1.0.1)**: Fallback backup network (unmonitored):
  - Preconfigured with default SSID `"S24"` and password `"12345678"`.
  - Configurable toggle in the initial setup Captive Portal, persisted to hardware NVS (`Preferences`).
  - This network is strictly used for uploading data when monitored networks fail or lack internet; it is never probed or monitored during telemetry cycles.
- **Firmware (v1.0.1)**: Post-sleep backlog upload via fallback:
  - When waking from a light sleep cycle, if unuploaded records are detected in the flash spool (`SpoolManager::hasPendingData()`), the station checks the 2.4 GHz spectrum scan survey for the fallback network (`S24`).
  - If visible, the station connects and immediately transmits the pending backlog before proceeding with the measurement cycle.
- **Worker & OTA (v1.0.1)**: Bumped stable fleet production OTA target to `1.0.1` (`STABLE_VERSION`).

---

## [1.0.0] - 2026-11-15

### Added
- **Edge Backend (v1.0.0)**: Cloudflare Worker interface deployed at `https://t18.kreier.workers.dev` backed by a dedicated Cloudflare D1 SQLite database (`t18-mon-db`).
- **OTA Updates (v1.0.0)**: Automated Over-The-Air firmware updater (`/api/ota` and streaming download `/api/ota/download/:filename`) with canary target routing and zero-redirect streaming proxy from GitHub Releases.
- **Hardware Profile Tailored for T18 & T8**:
  - **LILYGO T18 (Default)**: Status LED on **GPIO 5**, battery voltage sensing on **GPIO 35** via 100kΩ/100kΩ resistor divider (ratio 0.50f, $V_{\text{bat}} = 2.0 \times V_{\text{pin}}$).
  - **LILYGO T8**: Status LED on **GPIO 21**, battery voltage sensing on **GPIO 35** via 100kΩ/100kΩ resistor divider (ratio 0.50f).
  - **ESP32 DevKit V1**: Status LED on **GPIO 2**, battery monitoring disabled.
  - **Custom / ESP32-C5**: Configurable GPIO mappings persisted to NVS (`Preferences`).
- **Battery Telemetry & Discharge Analytics**:
  - Live battery voltage measurement on ADC pin IO35.
  - Automated remaining runtime estimation and discharge slope analysis.
  - Interactive battery discharge history chart with Day (24h), Week (7d), and Month (30d) timeframes.
- **Spectrum Survey & Promiscuous Mode Contention**:
  - 13-channel 2.4 GHz full spectrum sweeps measuring airtime % and frame distributions (Management, Data, Control).
  - 4-tier RSSI distribution histograms and unique transmitter MAC counts.
  - 10-minute spectrum scan duration variance tracking.
- **Local Network & Dual-Stack Diagnostics**:
  - Gateway TCP SYN-ACK round-trip latency (`WiFi.gatewayIP()`).
  - Dual-stack DNS benchmark (IPv4 `A` vs IPv6 `AAAA`).
  - Captive portal HTTP canary hijacking detection.
  - 24-hour gateway firewall and open port sweep.
- **Zero-Flash-Wear Telemetry & Raw Spooling**:
  - RAM-first transmission during healthy connectivity (zero SPI flash wear).
  - 192 KB raw binary SPI flash ring buffer (`raw_spool`) for offline resilience.
- **Dual Dashboard Mirror**:
  - Server-rendered Cloudflare Edge dashboard at [https://t18.kreier.workers.dev/](https://t18.kreier.workers.dev/).
  - Static client mirror at [https://kreier.github.io/T18/](https://kreier.github.io/T18/).
  - Client-side station deep-linking (`/:station_id`).

---

## [0.5.0] - 2026-11-12
### Added
- Multi-network Wi-Fi monitoring and initial GitHub commit data pipeline.
