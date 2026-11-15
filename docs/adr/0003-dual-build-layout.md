# ADR 0003: Dual-Compatible Firmware Build Layout (PlatformIO & Arduino IDE)

## Status
Accepted

## Date
2026-11-13

## Context
Firmware for the ESP32 in open-source and educational ecosystems is developed and maintained by users with varying toolchains:
- **PlatformIO**: Standard in modern embedded CI/CD pipelines, supporting automated dependency management (`platformio.ini`), strict board and framework pinning, multi-target compilation with distinct compiler preprocessor flags, and headless command-line flashing.
- **Arduino IDE**: Widely used by hobbyists, students, and lab technicians who prefer a graphical single-click workflow (`.ino` sketch) without Python or Node toolchain dependencies.

Historically, supporting both workflows resulted either in maintaining two separate diverging copies of the code or forcing Arduino IDE users to learn PlatformIO CLI. Arduino IDE strictly requires sketches to reside in a directory sharing the sketch's base name (e.g. `wifi/wifi.ino`), whereas PlatformIO adheres to the standard `src/` and `include/` directory structure.

## Decision
We adopt a **modular single-source layout** centered around shared C++ modules in `firmware/include/` (and shared headers), unified across both build systems:

```
firmware/
├── platformio.ini         # Multi-target PlatformIO configuration
├── src/
│   └── main.cpp           # PlatformIO entrypoint: imports shared modular logic
├── wifi/
│   └── wifi.ino           # Arduino IDE sketch: imports shared modular logic
└── include/
    ├── app_config.h       # Configuration schemas & NVS (Preferences) persistence
    ├── app_state.h        # Runtime state & metrics data structures
    ├── board_pins.h       # Dynamic hardware profiles (T18, DevKit V1, T8) & ADC battery sensing
    ├── captive_portal.h   # Soft-AP, captive DNS server, & setup web UI
    ├── led_controller.h   # Non-blocking LED status indicator (2 Hz blink / solid ON)
    ├── ntp_sync.h         # SNTP synchronization engine (24-hour cycle)
    ├── wifi_scanner.h     # 802.11 Wi-Fi scan engine & phy mode analysis
    ├── latency_prober.h   # Association, DHCP, and HTTP HEAD/204 probe engine
    ├── channel_analyzer.h # Promiscuous 13-channel utilization & airtime analyzer
    ├── spool_manager.h    # In-memory first, raw SPI flash partition circular binary spool
    ├── api_client.h       # HTTPS client for /register and /data endpoints
    ├── ota_manager.h      # Over-the-air firmware updater & streaming binary client
    └── app_main.h         # Master state machine orchestrating boot, setup, & sleep
```

### Build Environment Integration

1. **PlatformIO Integration**:
   - `platformio.ini` defines three environments corresponding to the target hardware variants:
     - `[env:t18]` with `build_flags = -D STATUS_LED_PIN=5`
     - `[env:devkitv1]` with `build_flags = -D STATUS_LED_PIN=2`
     - `[env:t8]` with `build_flags = -D STATUS_LED_PIN=21`
   - Common configuration includes `board = esp32dev`, `framework = arduino`, and `include_dir = include`.
   - `src/main.cpp` simply includes `#include "app_main.h"`, providing the `setup()` and `loop()` implementations.

2. **Arduino IDE Integration**:
   - The user opens `firmware/wifi/wifi.ino`.
   - `wifi.ino` includes `"../include/app_main.h"` and provides compile-time defaults for boards without preprocessor flags (`STATUS_LED_PIN` default to pin 5 or selectable via a single `#define` at the top of the file).
   - In environments where parent directory includes (`../include/`) are not directly in the include search path, `platformio.ini` and Arduino library configurations explicitly link or include the directory.

## Consequences

### Positive
- **Zero Code Duplication**: Core Wi-Fi scanning, probing, JSON serialization, and power management logic exist in exactly one place.
- **Immediate Parity**: Any bug fix or feature added to a modular header immediately updates both PlatformIO and Arduino IDE builds.
- **Multi-Board Flexibility**: Adding a new hardware target (e.g. TTGO, Feather ESP32) only requires adding a build environment in `platformio.ini` or selecting a pin in `wifi.ino`.
- **Clean CI/CD**: Headless builds can easily compile all variants using `pio run`.

### Negative
- Arduino IDE users must preserve the directory structure (keeping `firmware/include` alongside `firmware/wifi`) rather than copying `wifi.ino` in isolation.
