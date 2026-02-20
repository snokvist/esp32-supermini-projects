# Task Log - hdzero-headtracker-monitor

Use this file as a running implementation log.

## Entries

### 2026-02-14 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `esp32-c3-devkitm-1`
- Env: `esp32c3_supermini`
- Initial firmware: LED blink + serial output

### 2026-02-14 - Implement PPM Monitor

- Replaced blink sketch with interrupt-based PPM decoder on `GPIO2`
- Added serial output for channel widths, frame rate, invalid pulse count
- Added BoxPro+ channel interpretation note (`ch1 pan`, `ch2 roll`, `ch3 tilt`)
- Updated hardware doc with 3.5mm TS wiring and bring-up checklist
- Validation:
  - `pio run` successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - Device serial output confirmed (`No PPM frame detected yet...`) before jack wiring

### 2026-02-14 - First Live Signal Validation

- Confirmed live PPM stream from BoxPro+ on ESP32 monitor
- Observed stable `ch=3` and `rate=50.0Hz`
- Confirmed channel values update with headset movement
- Finalized project README with explicit usage + interaction instructions

### 2026-02-19 - Default CRSF Output + BOOT Mode Toggle

- Added CRSF RC frame output as default runtime mode:
  - Serial address `0xC8`, type `0x16` (`RC_CHANNELS_PACKED`)
  - 22-byte packed payload for 16x 11-bit channels
  - CRC-8 DVB-S2 over `type + payload`
- Kept existing PPM ISR decode path and LED signal/no-signal blink behavior.
- Kept human-readable PPM monitor output as alternate mode.
- Added BOOT button (`GPIO9`, active low + pull-up) runtime mode toggle with debounce and edge detection.
- Mapped decoded pulse widths to CRSF channel values (`~172..1811`, center `~992`) and filled missing channels with center.
- Updated project docs for default CRSF behavior, runtime toggle workflow, and `GPIO9` boot/strapping caution.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.

### 2026-02-19 - Enhanced 5Hz Monitor Diagnostics

- Kept monitor mode at 5Hz (human-readable debug cadence unchanged).
- Added monitor-side frame-rate health checks:
  - `inst` (instantaneous Hz from last frame interval)
  - `ema` (smoothed frame-rate estimate)
  - `win` (windowed Hz over each 5Hz print interval)
  - `win` now shows `(ok/warn)` against expected `45..55Hz`.
- Added additional debug metrics to each monitor line:
  - frame `age` in milliseconds
  - invalid pulse total with per-window delta (`invalid(+delta)`)
  - channel pulse span (`span=min..max`) per latest decoded frame
- Reset monitor-window counters when switching from CRSF mode into monitor mode so first stats line is meaningful.
- Initialize monitor print timer on mode switch so first `win` rate sample is based on a real ~200ms window.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.

### 2026-02-19 - CRSF UART1 Mirror Output

- Added CRSF packet mirroring to a hardware UART while keeping existing USB CDC output.
- CRSF now transmits in CRSF mode to:
  - USB CDC `Serial` (`/dev/ttyACM0`)
  - UART1 TX `GPIO21` at `420000` baud
- Added compile-time knobs in firmware:
  - `CRSF_UART_ENABLED`
  - `CRSF_UART_TX_PIN`
  - `CRSF_UART_RX_PIN`
  - `CRSF_UART_BAUD`
- Kept mode behavior unchanged:
  - CRSF mode outputs binary CRSF frames
  - monitor mode remains 5Hz human-readable text
- Updated README and hardware docs with UART wiring and USB-vs-UART behavior note.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.
  - `pio run -t upload --upload-port /dev/ttyACM0` successful.

### 2026-02-20 - UART1 RX/TX Wiring + PWM Pin Plan

- Updated CRSF UART1 config to use both pins:
  - TX: `GPIO21`
  - RX: `GPIO20`
  - Baud: `420000`
- RX handling is currently non-parsing and non-blocking: incoming UART bytes are drained/ignored to keep room for future telemetry support.
- Reserved 3 PWM output pins for upcoming CRSF->servo mapping at `100Hz`:
  - `GPIO3`, `GPIO4`, `GPIO5`
- Updated README and hardware docs with:
  - explicit UART RX/TX wiring
  - note that RX is currently ignored
  - planned PWM pin assignments
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.

### 2026-02-20 - CRSF RX To 3x Servo PWM Outputs

- Replaced UART1 RX drain-only behavior with CRSF frame parsing on `GPIO20` (RX).
- Implemented CRSF `0x16` (`RC_CHANNELS_PACKED`) decode path for incoming channel data.
- Added 3 servo PWM outputs via LEDC:
  - `GPIO3` <- CRSF CH1
  - `GPIO4` <- CRSF CH2
  - `GPIO5` <- CRSF CH3
  - frequency: `100Hz`
- Added CRSF RX timeout safety:
  - if no valid CRSF RX packet for >`500ms`, all servo outputs return to center (`1500us`).
- Added monitor-mode debug fields:
  - CRSF RX status (`none/ok/stale`)
  - CRSF RX age
  - RX packet/invalid counters
  - live servo pulse outputs
- Updated README and hardware docs with servo wiring and runtime behavior details.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.
