# Contributing to kreier/wifi

Thank you for your interest in contributing to `kreier/wifi`!

This project is maintained as an open-source, edge-native Wi-Fi telemetry fleet monitoring platform. We welcome contributions from both human developers and autonomous AI coding agents.

---

## 1. Getting Started

### Prerequisites
* **Node.js**: v20 or v22+
* **npm**: v10+
* **Wrangler**: Installed locally via `worker/package.json` (`npx wrangler`)
* **Python**: 3.11+ (if compiling ESP32 firmware locally with PlatformIO)
* **PlatformIO Core**: `pip install platformio` (optional for local firmware builds)
* **Arduino IDE**: 2.x (optional for manual sketch flashing)

### Repository Setup
```bash
git clone https://github.com/kreier/wifi.git
cd wifi

# Install worker dependencies
cd worker
npm install
```

---

## 2. Development Workflows

### A. Cloudflare Worker Backend
The worker backend runs on Cloudflare Workers and D1 (serverless SQLite).

* **Run local development server**:
  ```bash
  cd worker
  npm run dev
  ```
* **Apply local D1 database migrations**:
  ```bash
  cd worker
  npm run d1:migrate:local
  ```
* **Type-check TypeScript**:
  ```bash
  cd worker
  npm run build
  ```

### B. Web Dashboard & Documentation Mirror
The dashboard is maintained as single-source TypeScript in `worker/src/frontend.ts`.

* Whenever you edit `worker/src/frontend.ts`, regenerate the GitHub Pages mirror:
  ```bash
  cd worker
  npm run build:docs
  ```
* This script compiles `frontend.ts`, evaluates `renderDashboardHtml()`, syntax-checks the embedded JavaScript using `node --check`, and updates `docs/index.html` and `docs/404.html`.

### C. ESP32 Firmware Development
* **PlatformIO**:
  ```bash
  cd firmware
  pio run -e esp32dev
  ```
* **Arduino IDE**:
  Open `firmware/wifi/wifi.ino` directly in the Arduino IDE and select board `ESP32 Dev Module`.

---

## 3. Contribution Rules & Architectural Invariants

Before submitting pull requests or committing changes, ensure your code complies with our architectural standards:

1. **Dual-Build Firmware Parity**: All embedded logic must live in `firmware/include/*.h`. Do not duplicate code into `main.cpp` or `wifi.ino`.
2. **Flash Wear Prevention**: Normal 10-minute telemetry cycles must transmit directly from RAM without touching SPI flash memory. Offline spooling uses the raw 192 KB binary partition (`min_spiffs.csv`).
3. **Template Literal Escaping in `frontend.ts`**: Any client-side `${...}` inside the HTML template string in `frontend.ts` must be escaped (`\${...}`) or constructed with string concatenation.
4. **Synchronized Version Bumps**: If updating firmware or web versions, follow the checklist in [`AGENTS.md`](AGENTS.md).
5. **Architectural Decisions**: For major architectural changes or schema redesigns, document the rationale by creating an ADR in `docs/adr/`.
6. **Changelog**: Keep [`CHANGELOG.md`](CHANGELOG.md) up to date following the Keep a Changelog standard.

---

## 4. Submitting a Pull Request

1. Create a feature branch: `git checkout -b feat/my-new-feature`
2. Ensure all builds pass:
   ```bash
   cd worker && npm run build && npm run build:docs
   ```
3. Commit using descriptive conventional commit messages (e.g. `feat(web): ...`, `fix(firmware): ...`).
4. Push your branch and open a Pull Request against `main`.
