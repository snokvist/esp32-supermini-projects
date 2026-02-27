# Task Log - supermini-oled-hello

Use this file as a running implementation log.

## Entries

### 2026-02-27 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `esp32-c3-devkitm-1`
- Env: `esp32c3_supermini`
- Initial firmware: LED blink + serial output

### 2026-02-27 - OLED Hello World Implementation

- Replaced blink sketch with SSD1306 I2C firmware for a 0.96" OLED (`128x64`).
- Added fixed I2C pin mapping for ESP32-C3 SuperMini:
  - `SDA = GPIO4`
  - `SCL = GPIO5`
  - address `0x3C`
- Added `Adafruit SSD1306` and `Adafruit GFX` dependencies in `platformio.ini`.
- Added runtime behavior:
  - centered `Hello,` / `World!` text
  - on-screen uptime updated once per second
  - serial diagnostics for pin/address, init status, and 1Hz uptime logs
- Updated `README.md` and `docs/HARDWARE.md` with complete wiring and usage instructions.
- Validation:
  - `pio run` (from `projects/supermini-oled-hello`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - runtime serial confirmation successful via `/dev/ttyACM0`:
    - observed: `Uptime: 10s`, `Uptime: 11s`, `Uptime: 12s`, `Uptime: 13s`, `Uptime: 14s`

### 2026-02-27 - BOOT Toggle + 30 FPS Graph Mode

- Added display mode state machine with two runtime modes:
  - `HELLO`: centered hello text + 1Hz uptime
  - `GRAPH`: animated sound-style equalizer bars with ~30 FPS target
- Added onboard BOOT button control:
  - `GPIO9` (active-low, debounced) toggles modes on press
- Added optional serial toggle control:
  - sending `t` toggles modes (useful for headless verification)
- Added graph diagnostics:
  - serial prints measured `Graph FPS: X.X` once per second while in graph mode
- Updated project docs to include BOOT control behavior and graph-mode expectations.
- Validation:
  - `pio run` (from `projects/supermini-oled-hello`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - runtime serial verification successful via `/dev/ttyACM0`:
    - observed: `Mode -> GRAPH (serial)`
    - observed: `Graph FPS: 31.3`, `Graph FPS: 30.3`
    - observed: `Mode -> HELLO (serial)`

### 2026-02-27 - Dead-Pixel Flash Test Mode

- Added a third runtime mode: `PIXEL_TEST`.
- Updated mode cycle to three states:
  - `HELLO -> GRAPH -> PIXEL_TEST -> HELLO`
- Implemented pixel-test behavior:
  - full-screen white/black flash every `2000ms`
  - useful for spotting dead or stuck pixels
- Added serial diagnostics in pixel-test mode:
  - `Pixel flash: ON`
  - `Pixel flash: OFF`
- Updated README and hardware docs with third-mode usage and behavior.
- Validation:
  - `pio run` (from `projects/supermini-oled-hello`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - runtime serial verification successful via `/dev/ttyACM0`:
    - observed: `Mode -> PIXEL_TEST (serial)`
    - observed: `Pixel flash: OFF`
    - observed: `Pixel flash: ON`

### 2026-02-27 - Stuck-Pixel Recovery Mode

- Added a fourth runtime mode: `RECOVERY`.
- Updated mode cycle to four states:
  - `HELLO -> GRAPH -> PIXEL_TEST -> RECOVERY -> HELLO`
- Implemented recovery behavior:
  - fast full-white/full-black frames
  - animated checkerboard phase shifts
  - randomized noise blocks
- Added serial diagnostics in recovery mode:
  - `Recovery FPS: X.X (pattern=N)` once per second
- Updated README and hardware docs with recovery-mode usage guidance.
- Validation:
  - `pio run` (from `projects/supermini-oled-hello`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - runtime serial verification successful via `/dev/ttyACM0`:
    - observed: `Mode -> RECOVERY (serial)`
    - observed: `Recovery FPS: 38.7 (pattern=3)`
    - observed: `Recovery FPS: 38.0 (pattern=0)`
