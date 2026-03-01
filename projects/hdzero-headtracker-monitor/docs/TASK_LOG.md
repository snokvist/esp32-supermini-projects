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

### 2026-02-20 - AP Web UI + Persistent Runtime Settings

- Added onboard AP + web UI configuration interface:
  - SSID: `waybeam-backpack`
  - password: `waybeam-backpack`
  - IP/subnet: `10.100.0.1/24`
  - DHCP: enabled via ESP32 SoftAP built-in DHCP server (`10.100.0.x` clients)
- Added HTTP endpoints:
  - `/` (single-page Web UI)
  - `/api/settings` (GET/POST live + persistent config)
  - `/api/status` (runtime counters and health snapshot)
- Implemented runtime settings model and live-apply pipeline:
  - pins (LED/PPM/button/UART/servo)
  - CRSF UART params
  - CRSF/PPM mapping and timeout thresholds
  - servo channel mapping, PWM frequency, pulse bounds
  - output-mode defaults
- Implemented persistent settings storage in NVS (`Preferences`) with schema versioning.
- Added settings validation and pin-conflict protection before apply/save.
- Kept runtime behavior:
  - CRSF serial forwarding
  - CRSF RX -> servo PWM mapping
  - BOOT button mode toggle
  - monitor diagnostics
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.
  - `pio run -t upload --upload-port /dev/ttyACM0` successful.

### 2026-02-20 - Web UI Reset To Defaults Action

- Added `Reset to defaults` button in the web UI action row.
- Added backend endpoint `POST /api/reset_defaults`:
  - restores firmware default settings
  - applies defaults live immediately
  - persists defaults to NVS
- Added confirmation prompt client-side before reset action.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.

### 2026-02-20 - Web UI Sectioned Layout

- Reorganized the web UI inputs into logical sections for faster navigation:
  - Pins
  - Modes
  - CRSF / UART
  - Servo Outputs
  - PPM Decode
  - Timing / Health
- Preserved all existing field names and API payload structure so backend behavior is unchanged.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.

### 2026-02-20 - Production Hardening Pass

- Hardened persistence and startup behavior:
  - added explicit `Preferences` readiness tracking (`nvs_ready`)
  - settings save now checks write success and reports failure to API callers
  - boot now validates persisted settings and auto-restores firmware defaults if invalid/corrupt
- Hardened runtime settings validation:
  - added configurable GPIO allowlist for ESP32-C3 SuperMini use (`GPIO0..10, GPIO20, GPIO21`)
  - keeps USB pins / unsupported GPIOs out of Web UI-applied pin assignments
- Hardened main loop behavior:
  - bounded CRSF UART RX parse work per loop iteration to avoid starvation under heavy serial input
- Hardened web behavior:
  - added no-cache headers for UI/settings/status responses
  - improved Web UI fetch error handling and reporting
  - added SoftAP startup failure logging
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful.
  - `pio run -t upload --upload-port /dev/ttyACM0` successful.

### 2026-02-27 - OLED Status Screen + Servo Pin Reassignment

- Added SSD1306 OLED support using the same display stack as `supermini-oled-hello`:
  - `Adafruit SSD1306`
  - `Adafruit GFX`
  - I2C on `GPIO4`/`GPIO5` at address `0x3C`
- Added a HDZero-specific six-line status layout instead of the OLED demo mode cycle:
  - inverse header with output mode + signal state
  - `PAN`, `ROL`, `TIL` latest PPM pulse widths
  - `S1`, `S2`, `S3` live servo outputs
  - smoothed PPM rate, CRSF RX freshness, and AP IP
- Kept BOOT button behavior unchanged:
  - BOOT still toggles `CRSF` vs `PPM monitor`
  - OLED mirrors runtime state instead of introducing separate display modes
- Reassigned default servo outputs to avoid the new OLED I2C reservation:
  - Servo1 -> `GPIO6`
  - Servo2 -> `GPIO7`
  - Servo3 -> `GPIO10`
- Reserved `GPIO4/5` in runtime validation so the web UI cannot reassign OLED pins to other functions.
- Kept firmware resilient if no OLED is connected:
  - serial warning on init failure
  - CRSF/servo/web features continue running
- Updated README and hardware docs with the new default pin map and OLED verification steps.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=2 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=6,7,10`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
- direct visual confirmation of the OLED contents is still pending

### 2026-02-27 - Adjacent Servo Pin Defaults

- Changed the default pin map to place all three servo outputs on adjacent GPIOs:
  - Servo1 -> `GPIO0`
  - Servo2 -> `GPIO1`
  - Servo3 -> `GPIO2`
- Moved default BoxPro PPM input from `GPIO2` to `GPIO10` to free the contiguous servo block.
- Bumped the NVS settings schema version from `1` to `2` so previously stored defaults are replaced with the new adjacent servo / PPM pin map after flash.
- Kept the OLED reservation unchanged:
  - `GPIO4` = SDA
  - `GPIO5` = SCL
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`

### 2026-02-27 - Three OLED Screens + Debug-Only AP

- Reworked BOOT-button mode handling into three runtime screens:
  - `HDZ>CRSF`
  - `UART>PWM`
  - `DEBUG CFG`
- Replaced the previous single text status page with screen-specific OLED layouts:
  - `HDZ>CRSF`: three centered bar graphs for `PAN`, `ROL`, `TIL`
  - `UART>PWM`: three centered bar graphs for `S1`, `S2`, `S3`
  - `DEBUG CFG`: compact text summary for PPM health, CRSF RX health, AP state, and active pin map
- Kept the underlying signal pipeline active:
  - PPM still maps to CRSF output
  - UART1 CRSF RX still drives servo PWM outputs
- Restricted AP/web activity to the debug screen only:
  - entering `DEBUG CFG` starts SoftAP + WebServer
  - leaving `DEBUG CFG` stops both
- Kept the existing web field names for compatibility, but `output_mode_live` and `output_mode_default` now select the three OLED/runtime screens instead of the old 2-state mode.
- Extended status JSON with:
  - `web_ui_active`
  - `output_mode_label`
- Updated README and hardware docs to describe the new three-screen workflow.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
  - direct visual confirmation of BOOT-cycled screen changes and debug-only AP behavior is still pending

### 2026-02-27 - Short-Press Main Screens + Long-Press Debug

- Reworked BOOT handling again for cleaner day-to-day operation:
  - short press toggles only between `HDZ>CRSF` and `UART>PWM`
  - long press (`>3s`) enters `DEBUG CFG`
  - short press in `DEBUG CFG` returns to the last graph screen
- Kept debounce handling and added explicit press-duration tracking with one-shot long-press detection.
- Simplified PPM frame-rate reporting:
  - removed the expected-min/expected-max comparison from runtime behavior
  - debug serial output now reports measured `hz` directly instead of `win=...Hz(ok/warn)`
- Polished the graph screens:
  - added edge and quarter-position guide marks
  - kept the center line
  - added a live marker that cuts through the filled bar for faster visual reading
- Updated README and hardware docs to describe the final button interaction model and simplified PPM reporting.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
  - direct manual confirmation of short-press/long-press behavior is still pending

### 2026-02-27 - Debug Screen PPM Smoothing

- Simplified the OLED `DEBUG CFG` PPM row further:
  - removed the rapidly changing frame-age millisecond value from the display
  - changed the OLED debug PPM line to show the same windowed measured Hz used by serial output
- Kept serial debug output detailed; this change aligns the OLED debug line with the already-stable serial measurement.
- Updated README and hardware notes to describe the windowed debug PPM display.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial output after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
    - observed short-press transitions:
      - `Screen -> UART>PWM (button-short)`
      - `Screen -> HDZ>CRSF (button-short)`

### 2026-02-27 - OLED Refresh Reduced To 10Hz

- Reduced the OLED redraw cap from `20Hz` (`50ms`) to `10Hz` (`100ms`) to make the debug display less visually jumpy.
- Kept the existing mode-switch redraw behavior instant by preserving the forced refresh on screen changes.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful

### 2026-02-27 - PPM Window Cleanup + Servo Write Reduction

- Removed the now-dead expected frame-Hz threshold settings from runtime state and NVS persistence.
- Switched the status/API PPM rate fields to the same stable windowed measurement used by the OLED and debug serial output:
  - `frame_hz`
  - `frame_hz_ema`
  - `ppm_window_hz`
- Added a small PWM-write optimization:
  - servo outputs now skip redundant `ledcWrite()` calls when the target pulse width has not changed
  - stale CRSF no longer rewrites centered `1500us` outputs every loop
- Moved the shared debug monitor sampling into a single helper so OLED, serial, and status reporting stay aligned.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
  - observed expected binary CRSF on USB CDC outside `DEBUG CFG`

### 2026-02-27 - Debug Serial Age Fix + Documentation Pass

- Fixed a debug-monitor regression where serial `age=...ms` could appear pinned to a repeatable loop-phase value instead of reflecting frame-timed sampling.
- Updated the shared monitor window so debug serial sampling advances only on fresh PPM frame boundaries.
- Clarified project documentation to match current firmware behavior:
  - `/dev/ttyACM0` is binary CRSF outside `DEBUG CFG`
  - `DEBUG CFG` is the readable serial console mode
  - status JSON PPM-rate fields use the stable windowed measurement
  - debug `age` describes latest frame age at each monitor sample
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`

### 2026-02-27 - Remove PPM Age From Debug Serial

- Removed the PPM-side `age=...ms` field from the human-readable debug serial line.
- Reason:
  - after the earlier timing fixes it no longer carried a useful signal-health meaning
  - it either locked to a loop-phase artifact or collapsed to near-zero on frame-timed sampling
- Kept the useful fields:
  - measured windowed `hz`
  - `invalid(+delta)`
  - CRSF RX age/status
  - live servo and channel values
- Updated README and hardware notes to stop describing the removed PPM age field.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`

### 2026-02-27 - Cross-Route Fallback Between PPM and CRSF RX

- Added symmetric cross-routing with source-health hysteresis:
  - sources go stale after `500ms` without valid events
  - sources re-acquire health after `3` valid events inside `150ms`
  - switching between live sources is held for at least `250ms`
  - healthy PPM owns CRSF output
  - healthy CRSF RX owns PWM
  - if only one source is healthy, it drives both output sides
- Implemented USB CRSF fallback from incoming CRSF RX:
  - when PPM is no longer healthy and CRSF RX is healthy, CRSF RX is packed and mirrored to USB CDC
  - fallback applies only to USB CDC, not UART1 TX
- Kept UART1 TX PPM-only by design:
  - incoming CRSF RX is not echoed back to UART1 TX
  - this avoids feedback/loop risks on attached CRSF hardware
- Implemented PWM fallback from PPM:
  - when CRSF RX is no longer healthy and PPM is healthy, PWM outputs follow fixed headtracker channels `PAN/ROL/TIL -> S1/S2/S3`
  - if neither source is healthy, PWM outputs return to center
- Added PPM hot-plug hardening:
  - PPM input now uses `INPUT_PULLDOWN`
  - boot serial now prints ESP reset reason for easier brownout vs software-fault diagnosis
- Added route visibility to diagnostics:
  - debug serial now reports `route usb=... pwm=...`
  - status JSON includes `usb_route_label` and `pwm_route_label`
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - serial boot confirmation after flash:
    - observed `Boot: hdzero-headtracker-monitor`
    - observed `Reset reason: UNKNOWN (0)`
    - observed `Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2`
    - observed `Screen -> HDZ>CRSF (boot)`
    - observed `OLED I2C: SDA=4, SCL=5, addr=0x3C`
    - observed `OLED status screen ready`
  - direct runtime cross-route validation with live source drop/recovery is still pending manual confirmation

### 2026-02-28 - Harden DEBUG CFG SoftAP Startup

- Investigated reports that `waybeam-backpack` could appear in scan results but be difficult to join reliably.
- Hardened `DEBUG CFG` AP startup by:
  - forcing a clean `WIFI_OFF -> WIFI_AP` restart before enabling the SoftAP
  - disabling WiFi sleep in AP mode
  - adding short stabilization delays around AP bring-up/teardown
  - adding a small `delay(1)` yield while the debug services are active so WiFi/background tasks are not competing with the tight debug loop
- Follow-up:
  - reverted the explicit `softAP(..., channel, hidden, max_conn)` call and returned to the plain SDK-default `softAP(ssid, password)` start path after the explicit parameterized call still produced cases where the AP reported `START` but was not visible to clients
- Added a more explicit serial line when the AP comes up, including SSID and assigned IP.
- Added WiFi AP event timing logs and matching status JSON timestamps so AP start, client association, and DHCP/IP assignment can be timed separately during debug.
- Follow-up:
  - removed the experimental event-driven AP retry state machine after it triggered an lwIP panic (`tcpip_send_msg_wait_sem`, `Invalid mbox`) during mode changes
  - kept the simpler clean restart path and the passive AP timing instrumentation
- Follow-up:
  - switched `waybeam-backpack` to an open network with no password for simpler client association during debugging
- Follow-up:
  - fixed a Web UI regression in `loadSettings()`: the embedded page was iterating field descriptors as `[name]` tuples instead of objects, which threw `(destructured parameter) is not iterable` and prevented any settings fields from being populated
- Follow-up:
  - checked the local ESP32-C3 SDK limits and confirmed SoftAP beacon interval is already at the minimum/default (`100 TU`)
  - added maximum WiFi TX power request in `DEBUG CFG` as one safe lever to make the AP more visible
- Follow-up:
  - forced the SoftAP to channel `6` instead of the Arduino default channel `1`
  - exposed the configured AP channel in status/logging so channel choice is visible during debug
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`
  - live AP association still needs manual confirmation on-device

### 2026-02-28 - Make CRSF TX12 the Boot Default

- Changed the runtime boot default screen from `HDZ>CRSF` to `CRSF TX12`.
- Updated startup defaults so a clean power-on lands on the 12-channel CRSF view before any persisted settings are applied.
- Fixed `output_mode_default` validation to accept mode `3`, matching the existing `CRSF TX12` screen and Web UI options.
- Follow-up: older saved settings now preserve any explicit user-selected boot screen during the schema migration instead of forcing `CRSF TX12` onto every upgraded board.
- Updated README and hardware notes to reflect the new boot order and short-press cycle.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`
  - observed boot serial `Screen -> CRSF TX12 (boot)`

### 2026-02-27 - Route Hysteresis Follow-Up Fixes

- Fixed the first hysteresis pass so route ownership now behaves as intended:
  - an active source may keep its current route while it is still only `tentative`
  - a source must become fully `healthy` before it can take over a route again after a dropout
  - route failover no longer triggers early on the `150ms` acquire window alone
- Fixed route-switch handling so the `250ms` hold applies to switching between still-live sources instead of being bypassed on common fallback transitions.
- Aligned route staleness with the configured source freshness timeouts:
  - PPM ownership now ages out on `signal_timeout_ms`
  - CRSF RX ownership now ages out on `crsf_rx_timeout_ms`
- Fixed a CRSF output gap where UART1 TX could stop when PPM became `tentative` even though PPM still retained route ownership.
- Reset CRSF parser/source state and monitor state during `applySettings()` so live UART changes do not inherit stale routing state.
- Clarified debug reporting:
  - `route usb=TEXT` now means USB CDC is intentionally reserved for readable debug text in `DEBUG CFG`
  - status JSON uses the same `usb_route_label`
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-27 - Web UI Bench Usability Pass

- Refined the embedded debug/config web page without changing the backend API:
  - added a top summary strip for screen mode, USB route, PWM route, PPM Hz, CRSF RX, and servo outputs
  - kept raw status JSON under an expandable advanced section
  - replaced manual numeric entry with dropdowns for screen mode and servo source channels
  - added inline section notes for GPIO guardrails and routing behavior
  - replaced blocking browser alerts with inline success/error messages
- Kept the page lightweight and self-contained in the existing PROGMEM HTML blob.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-28 - Window CRSF RX Rate Reporting

- Replaced the CRSF RX rate calculation used by the OLED and Web UI.
- Old behavior:
  - computed instantaneous Hz from consecutive packet `millis()` deltas
  - fed that into an EMA
  - produced unrealistic spikes because `1-3ms` packet gaps quantize to very large Hz values
- New behavior:
  - computes CRSF RX rate over the same configurable monitor window used elsewhere in debug reporting
  - reports that windowed rate through the OLED and `crsf_rx_rate_hz`
- This keeps CRSF RX rate reporting consistent with the stable windowed PPM Hz path.
- Follow-up:
  - clamped the CRSF RX rate window to a minimum of `200ms` so lowering `monitor_print_interval_ms` cannot reintroduce unrealistic high-Hz spikes in the OLED/Web UI
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`

### 2026-02-28 - AP Diagnostics Cleanup

- Tightened two debug-only WiFi diagnostics details without changing runtime routing behavior:
  - moved `Debug/config AP start requested...` before the actual SoftAP bring-up work so AP logs now read in chronological order
  - replaced the raw WiFi TX power enum print with a human-readable dBm label
- Kept the existing open AP, fixed channel `6`, and `19.5dBm` TX power request unchanged.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`

### 2026-02-28 - Keep USB CRSF Active In DEBUG CFG

- Removed the special readable-text takeover on USB CDC while `DEBUG CFG` is active.
- New behavior:
  - `DEBUG CFG` still enables the SoftAP, Web UI, and OLED debug/status screen
  - USB CDC now keeps streaming the active CRSF source in `DEBUG CFG`, matching the other runtime screens
  - `usb_route_label` now always reports the actual CRSF owner (`PPM`, `CRSF`, or `NONE`) instead of the old `TEXT` placeholder
- Removed the remaining runtime human-readable serial output that would have corrupted USB CRSF in `DEBUG CFG`:
  - PPM monitor text lines
  - AP start/stop and client event logs
  - the repeating `No PPM frame detected yet...` message
- Kept boot-time serial text intact so reset reason and startup pin/OLED state are still visible before runtime CRSF begins.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`

### 2026-02-28 - Non-Blocking DEBUG CFG AP Bring-Up

- Tightened the `DEBUG CFG` AP control path after review:
  - removed the blocking `delay()`-based SoftAP startup from `setOutputMode()`
  - moved AP startup into a small staged loop service so CRSF output keeps flowing during mode switches
  - changed OLED/status reporting so `AP ON` and `web_ui_active` only reflect a real `WIFI_AP_START` event
- Kept the existing open AP, channel `6`, and `19.5dBm` TX power request unchanged.
- Follow-up:
  - added retry backoff for failed SoftAP startup instead of immediate restart loops
  - exposed AP state explicitly on OLED/status surfaces (`AP WAIT`, `AP RETRY`, `AP ON`)
  - added status JSON fields for retry tracking: `ap_state_label`, `ap_retry_count`, `ap_last_failure_ms`
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`)
  - `pio run -t upload --upload-port /dev/ttyACM0`

### 2026-02-27 - Smoothed CRSF Rate On Debug Screen

- Replaced the raw `CRSF ... XXms` OLED debug field with a smoothed CRSF RC packet-rate estimate.
- Reason:
  - the previous display showed raw packet age sampled at the OLED refresh cadence
  - that made normal packet-timing jitter look like sudden spikes even when CRSF RX was healthy
- Added a lightweight EMA from valid decoded CRSF RC packet intervals and display it as `CRSF ... XXXHz`.
- Kept raw CRSF packet age in status/serial surfaces where it is still useful for deeper debugging.
- Extended status JSON and Web UI summary with `crsf_rx_rate_hz`.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-27 - Web UI Health Labels

- Updated the Web UI summary cards so PPM and CRSF RX now show `health / rate` instead of rate alone.
- Added `ppm_health_label` and `crsf_rx_health_label` to the status JSON so the page reuses the same backend health logic as the OLED and debug serial output.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-27 - Clear Stale CRSF Rate Display

- Tightened the new CRSF rate display so stale/missing CRSF no longer shows a carried-over last-known rate.
- OLED debug page now shows:
  - `CRSF RXOK XXXHz` while CRSF is fresh
  - `CRSF STAL` when packets are stale
  - `CRSF NONE` before any valid CRSF RC packet has been seen
- Status JSON and Web UI summary now expose `crsf_rx_rate_hz` as a live-only value, reported as `0` when CRSF is stale or missing.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-27 - Add CRSF TX12 OLED Screen

- Added a third main OLED runtime screen, `CRSF TX12`, alongside `HDZ>CRSF` and `UART>PWM`.
- Short-press BOOT behavior now cycles across the three main screens:
  - `HDZ>CRSF`
  - `UART>PWM`
  - `CRSF TX12`
- Long-press behavior is unchanged:
  - long press (`>3s`) enters `DEBUG CFG`
  - short press in `DEBUG CFG` returns to the last selected main screen
- Implemented a compact two-column channel view for the first 12 outgoing CRSF channels:
  - reuses the same PPM -> CRSF mapping path as UART1 TX
  - shows slim centered bars so all 12 channels fit on the OLED
  - by default, channels `1..3` move with the headtracker and channels `4..12` stay centered
- Follow-up fix:
  - the screen now follows the active CRSF output route instead of assuming PPM is always the source
  - when USB CRSF output falls back to incoming CRSF RX, `CRSF TX12` shows the decoded CRSF RX channels
- Extended the Web UI mode dropdowns and mode validation to include the new screen.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful

### 2026-02-28 - CRSF Channel Merge Expansion (3 → 8 Channels)

- Expanded the CRSF merge feature from 3 hardcoded channels to a configurable 1–8 range.
- New settings:
  - `crsfMergeCount` (1–8, default 3): how many PPM channels to overlay onto CRSF RX
  - `crsfMergeMap` expanded from 3 to 8 slots (default: 1:1 mapping CH1→CH1 through CH8→CH8)
- NVS keys `mm1`..`mm8` + `mmc`; backward compatible with existing `mm1`/`mm2`/`mm3` keys
- Settings version bumped from 7 to 8 (v7 accepted in migration chain)
- Validation: bitmask-based uniqueness check replaces hardcoded 3-way comparison
- WebUI: merge count dropdown dynamically shows/hides the relevant map fields

### 2026-02-28 - WebUI Redesign

- Replaced single "Show/Hide Advanced" toggle with three view buttons: **Gamepad**, **Advanced**, **Setup** (default)
- Buttons are centered; Setup is the default view on page load
- Moved action buttons (Apply + Save, Reset, Refresh) above the settings sections
- Removed "Apply Live" — all changes now persist on save
- Renamed "Bluetooth Gamepad" section to "Gamepad"
- Gamepad channel invert: replaced 12 separate dropdown rows with inline "Inv" checkboxes next to each source dropdown (halves vertical space)
- Dirty field highlighting: changed fields get an amber left-border and amber input border, reset on successful save
- Save button shows pending change count (e.g. "Apply + Save (3)") and is disabled when clean
- Renamed "Reset to defaults" to "Reset"

### 2026-02-28 - BLE Gamepad Axis Mapping Fix (8BitDo Ultimate 2C)

- Fixed hat switch (D-pad) decoding: the hat is always a single value (`cnt=1`); the old code used the usage-array sub-index to offset into the data, reading past the actual 4-bit hat field when the usage array contained non-data entries (e.g. Application Collection 0x05)
- Fixed Simulation Controls trigger fallback: expanded candidate slots from 4–5 to include 2–3, so both Gas (L-Trigger) and Brake (R-Trigger) get mapped even when Z/Rz are already used by sticks
- Corrected WebUI source labels to match 8BitDo Ultimate 2C BLE HID descriptor:
  - Slot 2: "R-Stick X" (Z axis), Slot 3: "L-Trigger" (Gas via Sim Controls), Slot 4: "R-Trigger" (Brake), Slot 5: "R-Stick Y" (Rz axis)
- Updated default AETR channel mapping to match corrected slots

### 2026-02-28 - Selectable CRSF Output Target

- Added a web UI dropdown to select the CRSF output target:
  - `USB Serial` (default, backward-compatible)
  - `HW UART TX` (silences USB Serial entirely)
  - `Both (USB + HW UART)` (simultaneous output to both)
- Changed USB Serial and HW UART baud to `420000` (standard CRSF rate) for both directions.
- Updated the Web UI `CRSF / UART` section:
  - UART baud label now reads `HW UART baud (RX + TX)` to clarify it applies to both directions on the single UART
  - section note describes the three output targets and that USB Serial always runs at `420000`
- When `HW UART TX` is selected, USB Serial is completely silenced:
  - boot prints, OLED init messages, and runtime CRSF are all suppressed
  - settings load is moved before boot prints in `setup()` so the silence takes effect immediately
- Added NVS persistence for the new setting (`cotr` key, schema version bumped from `3` to `4`).
- Removed dead `sendCrsfRxToUsbFrame()` function after inlining CRSF RX fallback routing into the main loop.
- Updated README, HARDWARE.md, and Web UI to document the new output target options.
- Validation:
  - `pio run` (from `projects/hdzero-headtracker-monitor`) successful
  - `pio run -t upload --upload-port /dev/ttyACM0` successful
  - confirmed USB CRSF output at `420000` baud
  - confirmed HW UART TX output at `420000` baud on remote device
  - confirmed USB Serial silence when `HW UART TX` is selected
  - confirmed `Both` mode sends to both outputs simultaneously
