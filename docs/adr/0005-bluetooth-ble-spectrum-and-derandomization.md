# ADR 0005: 2.4 GHz Bluetooth Classic & BLE Ingestion with Advertising Payload De-Randomization

## Status
Accepted

## Date
2026-11-16

## Context
The ESP32 microcontroller features a single 2.4 GHz radio transceiver shared between Wi-Fi (802.11 b/g/n), Bluetooth Low Energy (BLE), and Bluetooth Classic (BR/EDR). 

1. **RF Contention & Spectrum Visibility**:
   - While the `kreier/wifi` platform surveyed Wi-Fi access points and measured promiscuous 802.11 frame airtime across 13 channels, non-Wi-Fi 2.4 GHz interference from nearby Bluetooth devices (smartphones, audio headsets, IoT sensors, beacons) remained completely invisible to fleet telemetry.
   - Operators could not correlate unexplained 2.4 GHz airtime spikes with physical presence or local Bluetooth traffic.

2. **BLE MAC Address Randomization**:
   - Modern mobile platforms (iOS, Android, macOS, Windows) and wearable devices broadcast randomized Resolvable Private Addresses (RPA) that regenerate every 15 minutes to prevent tracking.
   - Simply counting unique MAC addresses over successive cycles produces wildly inflated, inaccurate device counts (e.g. hundreds of phantom devices per day).
   - In contrast, Bluetooth Classic utilizes fixed IEEE 802 MAC addresses and announces Class of Device (COD) descriptors, but Classic scanning does not capture low-power beacons, wearables, or mobile background pairing packets.

## Decision

### 1. Dual-Stack Bluetooth Scanning in 10-Minute Cycles
- Integrated `BluetoothScanner` (`firmware/include/bluetooth_scanner.h`) into the scheduled monitoring cycle (`AppMain::loop()`).
- Scanning executes sequentially when Wi-Fi is disconnected to avoid RF collision and resource contention:
  1. **BLE Active Scan (3 seconds)**: Broadcasts scan requests to capture Extended Advertising, Service UUIDs, and local device names.
  2. **Classic Bluetooth Inquiry (3 seconds)**: Queries discoverable BR/EDR devices, capturing fixed MAC addresses, Class of Device (COD), friendly names, and RSSI.
- Resources are safely de-initialized (`BLEDevice::deinit(false)` and `serialBT.end()`) to return the 2.4 GHz radio and heap memory cleanly to the Wi-Fi stack before HTTPS telemetry transmission.

### 2. Advertising Payload De-Randomization & Unique Device Fingerprinting
Rather than relying on MAC addresses, the firmware inspects the Type-Length-Value (TLV) structures of every BLE advertising packet to construct deterministic entity fingerprints:
- **Public & Static Addresses**: Tracked directly by their IEEE MAC (`PUB:<MAC>`).
- **Beacons & Trackers**:
  - **Apple iBeacon**: Fingerprinted by `IBEACON:<Major>:<Minor>`.
  - **Google Eddystone**: Fingerprinted by `EDDY:<Namespace_Instance>`.
  - **Apple Find My / AirTag**: Fingerprinted by status bytes and public key prefixes (`AIRTAG:<key_prefix>`).
  - **Tile**: Identified via Tile 16-bit Service UUID (`0xFEED`).
- **Named Peripherals**: Identified by `NAME:<DeviceName>:<CompanyId>:<ServiceUUID>`.
- **Anonymous Resolvable Private Addresses (RPA)**:
  - Clustered by Manufacturer Company ID (`0x004C` Apple, `0x00E0` Google, `0x0006` Microsoft, `0x0075` Samsung), continuity payload sub-type, primary service UUID, and RSSI spatial zone (18 dBm window).
- Computes `ble_unique_devices` alongside raw `ble_total_macs`, `ble_random_macs`, and category counts (smartphones, beacons, wearables, audio, other).

### 3. Edge Backend & Database Modeling (Migration 0010)
- **Database Schema**:
  - `station_bluetooth_summary`: Records 10-minute cycle rollups (`ble_unique_devices`, `ble_random_macs`, `ble_public_macs`, category counts, `classic_total`, `devices_json`).
  - `station_latest_bluetooth`: Single-row per-station cache for instant dashboard SSR.
- **Scheduled Cleanup (`cron.ts`)**: Daily maintenance purges records older than 30 days.

### 4. Interactive Web UI Visualization
- Added a dedicated **2.4 GHz Bluetooth & BLE Activity** card in `worker/src/frontend.ts`:
  - Quick KPI summary tiles (Unique BLE Devices, Observed MACs, RPA vs Public ratio, Classic BT total, Device Composition).
  - 24-Hour Trend Chart (Chart.js) comparing unique BLE devices, randomized MACs, and Classic devices over time.
  - Collapsible device drawer detailing detected device names, ecosystem vendors, categories, address privacy tags, and signal RSSI bars.

## Consequences

### Positive
- Direct visibility into 2.4 GHz RF contention driven by non-Wi-Fi devices.
- Meaningful foot traffic and local presence estimation despite aggressive MAC randomization.
- Zero extra hardware cost; utilizes ESP32 onboard Bluetooth hardware.
- Full parity between Cloudflare Worker SSR and GitHub Pages static mirror.

### Negative
- Binary flash usage increased to 97.0% of the 1.92 MB OTA partition due to the dual Bluetooth stack. The firmware remains within bounds, but future features will require partition tuning or NimBLE transition if classic BT is deprecated in IDF 5.
