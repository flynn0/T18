# Changelog

All notable changes to the `flynn0/T18` platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.1.8] - 2026-11-20

### Added & Enhanced
- **Automatic Energy-Preserving Switch on Power Bank Keep-Alive**:
  - When `powerbank_keepalive` is enabled (even on boards without battery voltage sensing like the ESP32-C3 SuperMini where Vbat appears permanently powered), the firmware now automatically switches from aggressive 2-minute AC mains cadence to battery-optimized 10-minute cadence.
  - Promiscuous 13-channel congestion survey and network latency probing are automatically throttled to the 30-minute boundary (`is30mBoundary`), reducing radio active duty cycle from 27.7% to only ~3%.
  - Optimizes 13-channel survey dwell time: reduced from 2000 ms to 800 ms per channel in battery/power bank modes, saving 15.6 seconds of active radio time per 30 minutes ($10.4\text{ s}$ total vs $26.0\text{ s}$).
  - Maximizes light sleep dwelling between cycles, boosting ESP32-C3 power bank runtime from ~7.1 days to **62.7+ days** on a 10,000 mAh 5V power bank!
- **20-Second Keep-Alive Pulse Optimization**:
  - Extended the power bank keep-alive wake pulse interval from 15 seconds to **20 seconds** (`INTERVAL_SLEEP_MS = 20000`).
  - Reduces keep-alive wakeups by 25% (from 240 to 180 pulses/hour), lowering quiescent sleep draw from $1.85\text{ mA} \to \mathbf{1.664\text{ mA}}$ on ESP32-C3.
- **18-Day Offline 30-Minute Average Ring Buffer Storage**:
  - Offline spooling policy updated: intermediate cycles accumulate solely in RAM. Only once every 30 minutes at the boundary is a consolidated slot persisted to flash ring buffer (`spool_manager.h` and `channel_history.h`).
  - Streamlined binary serialization: 30-minute offline records omit verbose AP scan lists and retain essential 30-minute metrics (13-channel airtimes, unique transmitters, monitored network reliability, BLE unique devices, and battery mV), shrinking records to ~110-120 bytes.
  - 176 KB circular flash buffer now holds **1,500+ records**, guaranteeing **over 31 days** (well exceeding the 18 days / 864 slots requirement) of offline telemetry during complete network blackout.
  - Drastically cuts flash write wear to at most 48 writes per day (giving the NOR flash over 5,700 years of life).
- **Comprehensive Power Model Documentation**:
  - Added `docs/power-consumption.md` detailing exact mathematical modeling, measured light sleep benchmarks (1.0 mA on C3, 2.5 mA on T18 LiPo, 17.0 mA on DevKit V1), duty cycle derivations, and hardware runtime projections across 2000 mAh LiPo and 10,000 mAh power bank configurations.

---

## [1.1.7] - 2026-11-20

### Added & Enhanced
- **Iterative Chunked Backlog Upload & Flash Drain Protection**:
  - Replaced the single-batch 12-record spool upload in `app_main.h` with an iterative drain loop (`while (SpoolManager::hasPendingData())`) uploading in chunks of 12.
  - Implemented transactional batch confirmation: `SpoolManager::confirmBatchUploaded()` advances `read_offset` only after the Cloudflare Worker backend confirms successful HTTP 200 ingestion.
  - If network or server encounters an error mid-upload, unconfirmed records remain stored in flash and resume on subsequent wakeups.
  - Automatic circular pointer reset to base address when all backlogged records are completely drained, preventing flash memory fragmentation and unnecessary write cycles.
- **Streamlined 192 KB Circular Flash Ring Buffer**:
  - Overhauled offline binary serialization in `spool_manager.h` (v2 header format): each record efficiently stores channel airtime congestion across 13 channels, monitored station connection & probe reliability (assoc, DHCP, gateway RTT, DNS, HTTP probes), BLE unique device counts, and newly detected APs with RF characteristics.
  - Sizing & Retention: Allocates 176 KB (180,224 bytes) dedicated to the raw circular spool buffer, fitting ~880–900 complete telemetry records.
  - Provides **~6.1 days (146+ hours)** of continuous offline storage on standard 10-minute battery intervals, or **~18.3 days (440+ hours)** on 30-minute intervals.
  - Dedicated rolling 24-hour `ChannelHistory` (48 slots) stored in independent 12 KB flash sectors 1..3 with slot-distance UTC timestamp calibration immune to light-sleep timer drift.
- **BLE Unique Devices in Channel History & Real-Time Aggregation**:
  - Guaranteed `ble_unique_devices` is captured, recorded into `ChannelHistoryManager`, and transmitted within the `bluetooth` telemetry payload for all 30-minute intervals.
  - Added real-time rollup processing (`upsertDaily30MinSummary()`) in backend `telemetry.ts` and `db.ts` so historical and live cycles immediately populate `daily_30min_summaries`.
- **Peak RSSI & Quality Tracking in Historical Discovered APs**:
  - Updated D1 database upsert logic to compute `MAX(rssi)` and `MAX(quality)` across scans, preserving the highest observed signal strength and signal quality for every network across all time.
- **Dual-View Discovered AP Drawer (Last 24h Missing vs Complete Archive)**:
  - Added interactive view toggle in the Previously Detected Networks drawer: switch seamlessly between "Last 24h Missing" and "All Discovered APs (Archive)" with live count badges.
  - Cloudflare D1 retains the full historical catalog of all APs observed by the station.

---

## [1.1.6] - 2026-11-20

### Added & Enhanced
- **Dynamic Flexible Maximum Target for BLE Heatmaps**:
  - Replaced the hardcoded 25-device ceiling in Bluetooth heatmaps with automatic dynamic scaling calculated directly from the station's observed peak activity.
  - **24-Hour Congestion Heatmap**: Dynamically calculates peak BLE devices across the 48 half-hour timeslots, scales palette color thresholds proportionally across all 6 tiers (`0`, `1–15%`, `16–35%`, `36–60%`, `61–85%`, and `≥86% Peak`), and dynamically updates legend swatch tooltips and peak badge (`Peak X (Scale Y)`).
  - **7-Day Historical Heatmap (Bluetooth Activity)**: Scans all 336 timeslots (7 days × 48 half-hour windows) to determine the weekly maximum, applies proportionate threshold bins, and renders an active inline color scale legend with live peak indicator in the drawer header.
  - Environments with low BLE presence (e.g. 5–8 devices) now utilize the full color palette instead of remaining clamped at the bottom tiers; high-density environments (e.g. 30–120+ devices) scale smoothly without saturation clipping.
- **ESP32-C3 Active Power Bank Keep-Alive Routine**:
  - Implemented high-current keep-alive pulse mechanism in `app_main.h` (every 15 s) during light sleep: powers on Wi-Fi modem (`WiFi.mode(WIFI_STA)`), transmits active 802.11 probe frames at +20 dBm via `WiFi.scanNetworks(true, false)`, activates LED, and executes a 120 ms CPU burn loop.
  - Spikes ESP32-C3 current draw from ~18 mA baseline to >120–180 mA peak, reliably resetting auto-off timers on low-threshold USB power banks.

### Fixed
- **Web Serial Flasher "invalid header" Bootloop**:
  - Fixed `invalid header: 0xffffffff` watchdog reboot loop on ESP32-C3 after web flashing caused by `new_install_prompt_erase: false` triggering automatic flash erase.
  - Configured `"new_install_skip_erase": true` across all update manifests (`manifest-update.json`, `manifest-c3-update.json`, `manifest-esp32-update.json`) so NVS credentials and settings are safely retained without flash wipe.
  - Included full structural partition sets in update manifests: bootloader (`0x0` for C3, `0x1000` for ESP32), partition table (`0x8000`), `boot_app0.bin` (`0xe000` resetting `otadata`), and application firmware (`0x10000`).
  - Updated DOM instantiation in `frontend.ts` to fully reconstruct `<esp-web-install-button>` upon radio toggles, eliminating stale manifest caching.

---

## [1.1.5] - 2026-11-20

### Added & Enhanced
- **ESP32-C3 Super Mini Target & Multi-Architecture Build Pipeline**:
  - Added `BOARD_C3_SUPERMINI` hardware profile with status LED on GPIO 8 (active-low inverted via `LedController::setActiveLow(true)`), BOOT interrupt on GPIO 9, and battery sensing disabled.
  - Tailored lightweight firmware build for RISC-V architecture without Classic Bluetooth (`BluetoothSerial.h`), saving >400 KB flash space and reducing DRAM footprint by 31% (16.7% vs 24.3%).
  - Adapted deep sleep and light sleep wakeups for RISC-V core using `gpio_wakeup_enable` and `esp_sleep_enable_gpio_wakeup()` (replacing unsupported Xtensa `esp_sleep_enable_ext0_wakeup`).
  - Added ESP32-C3 preset option to Captive Portal configuration dropdown (`captive_portal.h`).
  - Added `[env:esp32c3]` PlatformIO target using `esp32-c3-devkitm-1` and `TARGET_C3_SUPERMINI=1` flag.
  - Updated Web Serial Flasher manifests (`manifest.json` and `manifest-update.json`) to define dual-build entries for both `ESP32` and `ESP32-C3`, enabling automatic chip interrogation and one-click in-browser flashing.
- **Dedicated Station Configuration Modal & Canary Gating**:
  - Replaced browser `prompt()` / `confirm()` dialogs in Fleet Settings with a responsive `#editStationModal` dialog.
  - Added "⚙️ Configure" shortcut button directly to the Station view header banner.
  - Added semantic version comparison (`isFirmwareAtLeast(fw, "1.1.4")`) to disable and grey out the Canary enrollment checkbox on nodes running older firmware (e.g. v1.1.3 on station `C703`) with an explicit explanatory notice.
  - Added direct controls for station description modification and explicit OTA version pinning.
- **Enhanced Multi-Architecture OTA Routing**:
  - Updated backend OTA streaming proxy (`worker/src/ota.ts`) to inspect `board` query parameter and stream architecture-specific binaries (`firmware-esp32-v*.bin` vs `firmware-esp32c3-v*.bin`).
  - Added binary download links and `esptool.py` flashing commands for both standard ESP32 and ESP32-C3 in the Flasher modal.

### Fixed
- **Unique BLE Device Limit Ceiling**:
  - Removed the arbitrary 25-device ceiling on BLE scanning.
  - Expanded live scan tracking buffer in `bluetooth_scanner.h` to 120 unique BLE devices and top 30 prominent devices.
  - Expanded RAM accumulation buffer in `app_main.h` from 25 to 60 devices for long-run multi-cycle collection before upload.

---

## [1.1.4] - 2026-11-20

### Added & Enhanced
- **Canary Firmware Enrollment & NVS State**:
  - Added persistent `is_canary` flag to `AppConfig` and `AppConfigManager` (`setCanaryTrack`, `isCanaryTrack`) stored in ESP32 NVS.
  - Added "Canary / Beta Release Channel" enrollment checkbox to captive portal setup UI (`captive_portal.h`) and serialized `is_canary` into telemetry cycle payloads.
- **Unified OTA Firmware Update Check & Dual-Verification**:
  - Combined OTA check (`GET /api/ota`) with canary entitlement verification and bidirectional synchronization.
  - Server reports station canary entitlement (`is_canary`) and targeted release channel (`track: "canary" | "stable"`).
  - Firmware automatically synchronizes local NVS `is_canary` preference if updated by administrator in Cloudflare D1.
  - Independent firmware verification safeguard: firmware refuses to flash canary / beta candidate binaries unless device is explicitly enrolled in the canary channel.
  - Enhanced semantic version parsing and comparison in both Cloudflare Worker backend (`worker/src/ota.ts`) and embedded firmware (`ota_manager.h`) to support pre-release tags (e.g. `1.1.4-canary.1`).
- **Dashboard & Admin Visual Indicators**:
  - Displayed prominent amber `CANARY` badge on station header and fleet management overview table when a station is enrolled in canary testing.
  - Added `CANARY_VERSION` environment configuration to `wrangler.toml` enabling independent canary fleet deployments alongside `STABLE_VERSION = "1.1.4"`.
- **UI Heatmap Palette Schemes (Color Scheme Selector)**:
  - Added color scheme dropdown in the 24-Hour Spectrum & BLE Heatmap header with 4 scientific color palettes: `Spectrum (Classic)`, `Inferno (Heat)`, `Viridis (Emerald)`, and `Plasma (Neon)`.
  - Dynamically updates active airtime and Bluetooth legend swatches and applies chosen palette across 24h and 7d heatmaps.
  - Choice is persisted in browser `localStorage`.
- **7-Day Historical Heatmaps (Wi-Fi Bands & Bluetooth Activity)**:
  - Added expandable drawer featuring two 7-day × 48-timeslot matrices (Wi-Fi 2.4 GHz peak channel congestion and Bluetooth unique device activity).
  - Dual-index matching by UTC timestamp (`start_timestamp`) and date string (`date`) ensuring timeslots align regardless of client timezone offsets.
- **Unified Multi-Section Timeframe Selectors (24h / 48h / 7d)**:
  - **Channel Utilisation & Spectrum Congestion**: Dynamic airtime averaging and KPI stats across 24h, 48h, and 7d windows.
  - **2.4 GHz Bluetooth & BLE Activity**: Timeframe buttons with dynamic multi-day X-axis labels and adaptive point density.
  - **Network Latency Trends**: Timeframe selector with dynamic chart headers, multi-day labels, and smooth sampling for long histories.
- **Telemetry Query Retrospectives**:
  - Expanded Worker backend query windows in `api.ts` and `db.ts` to 168 hours (7 days) and up to 2,500 measurement records, providing full historical visibility without truncation.

### Fixed
- **Randomized BLE MACs Periodic Drop to Zero**:
  - Root cause: In 30-minute historical flash rollups (`channel_history.h`), `ChannelSlotRecord` tracked unique BLE and classic devices but omitted random BLE MACs (`ble_random_macs`), submitting `0` on half-hour boundaries and clobbering database entries.
  - Firmware fix: Added `ble_random_macs` to `ChannelSlotRecord`, tracked peak RPA counts during `recordCycle()`, and transmitted values during 30-minute batch submissions.
  - Backend fix: Hardened `db.ts` so `station_bluetooth_summary` upserts preserve non-zero RPA counts and default missing counts safely to `ble_unique_devices - ble_public_macs`.
  - Frontend fix: Implemented smart interpolation in `renderBluetoothChart()` so historical zero entries smoothly reflect surrounding activity without sharp artificial dips.
- **Periodic Wi-Fi Spectrum Scan Time Fluctuations (4,006 ms vs 26,000 ms)**:
  - Removed redundant hardcoded `scan_duration_ms: 26000` constant from 30-minute rolling channel history uploads in `channel_history.h`.
  - Fixed scan duration accumulation in `app_main.h` so single-scan durations are stored (`_ramScanDurationMs = scanDurationMs;`) rather than compounding subcycle times.
  - Frontend ignores legacy anomalous values $\ge 15{,}000\text{ ms}$ for Wi-Fi spectrum scan card metric.
- **Bluetooth Tracking Buffer Expansion**:
  - Expanded unique BLE device tracking buffer to 60 devices and reported details to top 20 prominent devices.

---

## [1.1.3] - 2026-11-18

### Added
- **24-Hour Station Reliability Timeline (48 × 30-Minute Interval Bar)**:
  - Added a 48-cell interactive reliability matrix directly inside each network card below the HTTP probe latencies.
  - Granular color-coded failure classification:
    - 🟢 Green: 100% probes and connectivity healthy.
    - 🟣 Purple: One or more HTTP probe endpoints timed out.
    - 🟡 Amber: DNS IPv4 or IPv6 resolution timed out.
    - 🟠 Orange: Gateway ICMP/TCP round-trip timed out.
    - 🔴 Red: DHCP lease acquisition timed out.
    - ⚫ Charcoal: Association timeout / failure.
    - ⚪ Muted Gray: Idle period with no measurement recorded.
  - Interactive hover tooltips displaying 30-minute time intervals, relative elapsed time, and detailed error summaries.
- **Collapsible Scan Surveys**:
  - Made "Surrounding 2.4 GHz Spectrum Survey" and "Previously Detected Networks" collapsible with click-to-expand headers, collapsed by default for a clean, streamlined dashboard.

### Fixed & Improved
- **Restored 24-Hour Spectrum & BLE Heatmap Geometry**:
  - Isolated the Settings Year Activity Heatmap styles (`.cal-*`), completely resolving a CSS selector conflict that inadvertently reduced the 24-hour spectrum heatmap cells from 18px height and added horizontal borders.
- **Calendar Heatmap Layout & Legend Refinements**:
  - Widened the Settings & Fleet Management dialog to 1040px to eliminate horizontal scrollbars on 52-week activity views.
  - Transformed legend indicators into strict squares.
  - Replaced generic "Less / More" labels with dynamic dataset-driven indicators (`0 cycles / day` to `${max} cycles / day (peak)`).
- **Target Stations Cleanup Selection Integrity**:
  - Fixed station dropdown population logic in Settings to dynamically refresh on every overview reload, properly reflecting real-time database state and preventing decommissioned stations from lingering in options.

---

## [1.1.2] - 2026-11-18

### Fixed
- **Subcycle Heap Fragmentation & Bluetooth Map Node Exhaustion**:
  - Replaced dynamic associative containers (`std::map<String, BleDeviceRecord>` and `std::set<String>`) in `BluetoothScanner::scanBle` with linear deduplication in `std::vector` and 64-bit integer MAC hashing, eliminating >600 heap allocations and RB-tree node fragmentation per scan.
  - Capped maximum unique tracked BLE devices in scan phase to 25 records.
  - Capped RAM accumulation across 2-minute subcycles in powered mode: `_ramSurvey` strictly capped at 60 APs, `_ramBt.ble_devices` capped at 15, and `_ramBt.classic_devices` capped at 10.
  - Explicitly scoped Wi-Fi scan results vector in `app_main.h` so 50+ SurveyItems are deallocated from stack/heap before `BluetoothScanner::performScan` starts.
  - Added `shrink_to_fit()` on RAM accumulators upon 10-minute cycle reset.

---

## [1.1.1] - 2026-11-18

### Fixed
- **Bluetooth Scan Heap Exhaustion & OOM Panic**:
  - Fixed an out-of-memory crash (`abort() was called at PC 0x40201c97 on core 0`) caused by simultaneously running the Wi-Fi station stack and the Bluedroid BLE/Classic Bluetooth stack during congested scans.
  - Automatically turns off the Wi-Fi radio (`WiFi.mode(WIFI_OFF)`) and deletes scan cache immediately after the Wi-Fi spectrum scan completes, reclaiming >40 KB of internal DRAM for the Bluetooth stack.
  - Added defensive free heap threshold guards in `BluetoothScanner::scanBle` (min 40 KB) and `BluetoothScanner::scanClassic` (min 35 KB) to prevent fatal `std::bad_alloc` aborts in crowded RF environments.
  - Added detailed free heap diagnostics logging before and after Bluetooth execution.

---

## [1.1.0] - 2026-11-17

### Added
- **Power-Aware Adaptive Measurement Matrix**:
  - Senses operating power supply via ADC voltage on IO35 ($V_{\text{bat}} = 2.0 \times V_{\text{pin}}$).
  - **Battery Mode** ($\le 4.20\text{ V}$): Wi-Fi scan and BLE + Classic BT scan every 10 minutes; 13-channel promiscuous congestion survey and station speed/latency probes every 30 minutes; non-existent networks are omitted from connection attempts to avoid timeout penalties.
  - **Powered Mode** ($> 4.20\text{ V}$ / USB / no battery): 2-minute cycle cadence running Wi-Fi scan, BLE + Classic scan, and 13-channel survey, accumulating peak maximums in RAM over 10-minute intervals before testing latency and uploading telemetry.
- **Zero-Flash-Wear Invariant When Online**:
  - Telemetry is transmitted directly from RAM over HTTP; zero bytes written to flash when upload succeeds every 10 minutes.
  - Offline buffering safely engages only when upload fails or no networks are visible: commits 48-slot (24-hour) 30-minute buffer (`ChannelHistory`) and binary records to raw circular flash spool (`SpoolManager`).
  - Automatic backlog drainage and spool clearing upon network reconnection.
- **D1 Daily 30-Minute Summaries (Migration 0011)**:
  - Added `daily_30min_summaries` table storing 48 half-hour intervals of the previous UTC day aggregated by daily cron trigger at 00:15 UTC (max 13-channel airtime utilization array, max unique BLE devices, max Classic BT devices, and battery voltage).
- **Change-Only Network Environment & Firewall Diagnostics History**:
  - Added `station_network_diagnostics_history` table recording long-term audit snapshots once on initial detection and only appending records when gateway or firewall configuration changes, strictly ignoring `dns_ipv4_ms` and `dns_ipv6_ms` response time jitter.
- **Architecture Overview & ADR 0008**:
  - Added comprehensive multi-tier Mermaid architecture diagram in `README.md`.
  - Documented ADR 0008 detailing power-aware measurement matrix, 30-minute rollups, and zero-flash-wear invariants.

### Changed
- **Web UI Enhancements**:
  - 24-Hour Spectrum & BLE Heatmap drawer is expanded by default.
  - Battery voltage and discharge profile graph is expanded by default when battery telemetry is detected.
  - Removed "Previous 24 Hours" button/list from the Bluetooth table in favor of real-time cycle inspection.
  - Added live KPI breakdown badges for Key Vendors (Apple, Microsoft, Samsung) and Categories (Smartphone, Computer, Beacon).
  - Repaired footer styling, centered layout, and font inheritance broken by dangling CSS syntax.

---

## [1.0.4] - 2026-11-17

### Changed
- **Main Repository & Domain Migration to flynn0**:
  - Re-anchored primary repository to `flynn0/T18`.
  - Updated Cloudflare Worker host to `https://t18.dhsp.workers.dev` and static mirror to `https://flynn0.github.io/T18`.
  - Updated default worker URL and preset domain for authentication in firmware to `https://t18.dhsp.workers.dev`.
  - Added automatic NVS migration detecting legacy `*.kreier.workers.dev` configurations and resetting device tokens for clean re-authentication with `t18.dhsp.workers.dev`.
- **Bluetooth Scan Optimization**:
  - Removed legacy Bluetooth Classic inquiry and `BluetoothSerial` dependency from `BluetoothScanner`, significantly reducing firmware binary footprint and execution latency.
  - Scan focuses entirely on 2.4 GHz Bluetooth Low Energy (BLE) with de-randomization and categorization.

### Added
- **24-Hour Unique BLE Device Local Flash Storage**:
  - Extended `ChannelHistoryManager` (`ChannelSlotRecord` v2) to store and average unique BLE device counts across each of the 48 half-hour slots (24 hours total).
  - Synchronizes offline BLE unique device count alongside 13-channel utilization metrics directly to the worker backend upon network reconnection.

---

## [1.0.3] - 2026-11-17

### Fixed
- **Telemetry Ingestion & Station Registration Routing**:
  - Corrected compiled firmware binaries and portal defaults so new stations register and upload directly to `https://t18.kreier.workers.dev` instead of the legacy `wifi` worker, ensuring new stations appear in the T18 project.
  - Added automatic NVS migration in `AppConfigManager` and `CaptivePortalManager` that detects and converts legacy `wifi.kreier.workers.dev` configurations to `https://t18.kreier.workers.dev` and resets device token for clean zero-trust re-registration.

### Added
- **Preserve Settings & Data Flashing Option (Firmware Update Only)**:
  - Added installation mode selector in the Web Serial Flasher UI allowing operators to choose between **⚡ Update Firmware Only (Keep Settings & Data)** (default) and **🧹 Clean Install (Full Flash & Partitions)**.
  - Implemented `manifest-update.json` targeting only the application slot at offset `0x10000`, preserving NVS Wi-Fi credentials, tokens, and offline channel utilization history.
  - Added `"new_install_prompt_erase": true` in `manifest.json` so full installs prompt for confirmation before erasing flash.
- **24-Hour 13-Channel Utilization Average Ring Storage & Offline Backlog Sync**:
  - Implemented `ChannelHistoryManager` storing 48 half-hour average records (13 channels × 48 slots = 24 hours of 30-minute averages) persistently in dedicated flash sectors (`0x1000` to `0x3FFF`).
  - Rolling average accumulation across 10-minute cycles within each 30-minute interval.
  - Timestamps recorded during offline periods are automatically calibrated to UTC half-hour epochs upon NTP synchronization.
  - Automatic transmission of all un-transmitted records (`transmitted == false`) to the T18 worker `/data` endpoint as soon as network connectivity is restored, marking slots as transmitted upon successful worker receipt.
  - Fixed flash sector boundary erase handling in `SpoolManager` to prevent circular buffer sector over-erasure.

## [1.0.2] - 2026-11-16

### Added
- **In-Browser Web Serial Firmware Flasher**:
  - Integrated ESP Web Tools (`<esp-web-install-button>`) directly into the Web UI dashboard with a 3-tab modal dialog (`Web Serial Install`, `Direct Downloads & CLI`, and `First-Boot Setup`).
  - Supports one-click USB flashing directly from Google Chrome, Microsoft Edge, Brave, or Opera over Web Serial.
  - Generates and serves `manifest.json` and static binaries (`firmware.bin`, `partitions.bin`, `bootloader.bin`, `boot_app0.bin`, and an all-in-one `firmware-esp32-merged.bin`) from both Cloudflare Edge Worker and GitHub Pages.
  - Deep-link support via `/?flash=1` and `/flash` routes.
- **2.4 GHz Bluetooth Low Energy (BLE) & Classic Spectrum Scanning**:
  - Sequential dual-stack Bluetooth scan in every 10-minute cycle (`BluetoothScanner::performScan`).
  - Active 3-second BLE scan capturing Advertising, Service UUIDs, and local device names.
  - 3-second Classic Bluetooth Inquiry querying discoverable BR/EDR devices, capturing fixed MACs, Class of Device (COD), and RSSI.
  - Advanced payload de-randomization establishing deterministic fingerprints for Resolvable Private Addresses (RPA) across Apple (Find My / AirTag, iBeacon), Google (Eddystone), Tile, Samsung, and Microsoft devices.
  - D1 database migration (`0010_bluetooth_telemetry.sql`) adding `station_bluetooth_summary` and `station_latest_bluetooth`.
  - Web UI visualization: dedicated **2.4 GHz Bluetooth & BLE Activity** card with KPIs, 24-hour trend graph, collapsible device inspector, and Bluetooth heatmap row in the channel utilization view.
- **Dynamic LED & Boot Survey Sequence**:
  - 13-channel promiscuous congestion survey runs immediately on boot/wake with active LED feedback: solid ON while dwelling on a channel, 100 ms blink OFF during channel transitions.
  - Non-blocking BOOT button monitoring during channel scans: pressing BOOT immediately halts the scan and launches the Soft-AP captive portal setup without waiting.
- **Backup Hotspot Internet Failover**:
  - Automatic detection of backup AP (e.g. "S24") in the Wi-Fi scan survey.
  - If primary monitored Wi-Fi networks have no internet connection or fail to upload telemetry, the station automatically connects to the backup hotspot to transmit queued offline telemetry and current cycle data.
  - Setup portal includes backup hotspot credentials configuration section.
- **Backend & OTA Enhancements**:
  - Enforced strict SemVer comparison checks in firmware and Worker OTA to prevent unintended fleet downgrades.
  - Worker endpoints for `/manifest.json`, `/firmware/manifest.json`, and `/firmware/*` binary streaming with CORS.
  - Automated 30-day data retention cleanup for Bluetooth summaries in daily cron maintenance.

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
