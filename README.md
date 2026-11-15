# kreier/T18

[![Latest Release](https://img.shields.io/github/v/release/kreier/T18?color=blue&label=version)](https://github.com/kreier/T18/releases)
[![License](https://img.shields.io/badge/License-Apache_2.0-green.svg)](https://opensource.org/licenses/Apache-2.0)
[![Cloudflare Worker](https://img.shields.io/badge/Cloudflare-t18.kreier.workers.dev-F38020?logo=cloudflare)](https://t18.kreier.workers.dev/)
[![GitHub Pages](https://img.shields.io/badge/GitHub%20Pages-kreier.github.io%2FT18-22c55e?logo=github)](https://kreier.github.io/T18/)

> Autonomous Wi-Fi Spectrum & Latency Telemetry Fleet Node for ESP32 (LILYGO T18 & T8) with Cloudflare Worker, D1 SQLite Database, and GitHub Pages.

**Live Telemetry Dashboards**:
- 🌐 **Cloudflare Edge Dashboard**: [https://t18.kreier.workers.dev/](https://t18.kreier.workers.dev/)
- 📄 **GitHub Pages Mirror**: [https://kreier.github.io/T18/](https://kreier.github.io/T18/)

---

## Key Architectural Features

- **Serverless Edge Storage & Analytics**:
  All telemetry cycles, spectrum scans, channel congestion metrics, and daily rollups are stored in **Cloudflare D1** (`t18-mon-db`). Automated daily cron triggers aggregate 24-hour summaries and enforce 30-day raw data retention.
- **Hardware Targets: LILYGO T18 & T8 with IO35 Voltage Sensing**:
  Tailored out-of-the-box for the **LILYGO TTGO T18 (T-Energy v3.0)**, while also fully supporting the **LILYGO T8** and ESP32 DevKit V1:
  - **LILYGO T18**: Status LED on **GPIO 5**, battery ADC on **GPIO 35** via 100kΩ/100kΩ divider (ratio 0.50, $V_{\text{bat}} = 2.0 \times V_{\text{pin}}$).
  - **LILYGO T8**: Status LED on **GPIO 21**, battery ADC on **GPIO 35** via 100kΩ/100kΩ divider.
  - **ESP32 DevKit V1**: Status LED on **GPIO 2**, battery monitoring disabled.
- **Over-The-Air (OTA) Updates**:
  Automated edge OTA updates (`/api/ota`) with zero-redirect binary streaming proxies (`/api/ota/download/:filename`) backed by GitHub Releases and Cloudflare R2, with canary testing support.
- **Promiscuous Mode Channel Contention Profiling**:
  Measures all 13 channels every 30 minutes with 2-second dwell times using ESP-IDF promiscuous mode. Classifies frames (Management, Data, Control), records 4-bracket RSSI histograms, counts unique transmitter MACs, and estimates airtime percentage.
- **10-Minute Spectrum Scan Variance Tracking**:
  Monitors the duration of full-spectrum scans in milliseconds on every 10-minute cycle, correlating scan latency with RF contention.
- **Local Network & Dual-Stack Diagnostics**:
  Measures local gateway TCP SYN round-trip time, performs IPv4 vs. IPv6 DNS resolution benchmarking, detects captive portal HTTP canary hijacking, and executes automated daily gateway firewall audits.
- **Zero-Flash-Wear Telemetry & Raw Spooling**:
  Normal telemetry cycles transmit directly from memory without touching flash. During offline periods, records are safely buffered into a dedicated 192 KB raw SPI flash binary circular spool partition (`raw_spool`).

---

## Hardware Pinout Reference

| Target Board | Status LED Pin | Battery ADC Pin (Divider) | BOOT Button | Default Flag |
| :--- | :---: | :---: | :---: | :--- |
| **LILYGO T18** *(Default)* | **GPIO 5** | **GPIO 35** (100k/100k, 0.50f) | **GPIO 0** | `-D STATUS_LED_PIN=5` |
| **LILYGO T8** | **GPIO 21** | **GPIO 35** (100k/100k, 0.50f) | **GPIO 0** | `-D STATUS_LED_PIN=21` |
| **ESP32 DevKit V1** | **GPIO 2** | N/A | **GPIO 0** | `-D STATUS_LED_PIN=2` |

- **Battery Voltage Calculation**:
  On both T18 and T8, the battery voltage is measured on **IO35** using the onboard 100kΩ / 100kΩ resistor divider:
  $$V_{\text{battery}} = \frac{V_{\text{ADC}}}{0.50} = 2.0 \times V_{\text{ADC}}$$

---

## Cloudflare Worker & D1 Setup

### 1. Worker Configuration
- **Worker Name**: `t18` (accessible at `https://t18.kreier.workers.dev`)
- **D1 Database**: `t18-mon-db` (`ded19daf-64c6-4f58-bda8-1ffa02ba99f1`)

### 2. Deploy Worker & Apply Migrations
```bash
cd worker
npm install
npm run d1:migrate:remote
npm run build:docs
npm run deploy
```

---

## Firmware Build & Flashing

### Option A: PlatformIO (Recommended)
```bash
cd firmware

# Build and upload for LILYGO T18 (Default)
pio run -e t18 -t upload

# Build and upload for LILYGO T8
pio run -e t8 -t upload

# Build and upload for DevKit V1
pio run -e devkitv1 -t upload

# Monitor Serial Output
pio device monitor -b 115200
```

### Option B: Arduino IDE
1. Open Arduino IDE.
2. Open [`firmware/T18/T18.ino`](firmware/T18/T18.ino).
3. Under **Tools > Board**, select **ESP32 Dev Module**.
4. Set **Partition Scheme** to **Minimal SPIFFS (Large APPS with OTA)**.
5. Install the **ArduinoJson** library (v7.x).
6. Click **Upload**.

---

## API Endpoints

- `GET /` — Responsive Server-Rendered Telemetry Dashboard
- `POST /register` — Zero-Trust Station Token Provisioning
- `POST /data` — Ingestion endpoint for 10-minute telemetry cycles
- `GET /api/stations` — Public station list and health status
- `GET /api/station/:id` — Detailed station metrics, history, and diagnostics
- `GET /api/ota` — OTA check endpoint for firmware update dispatch
- `GET /api/ota/download/:filename` — Direct binary streaming proxy for ESP32 OTA updates
- `GET /api/export` — Telemetry export in CSV or JSON format

---

## License

This project is licensed under the **Apache License 2.0** — see the [LICENSE](LICENSE) file for details.
