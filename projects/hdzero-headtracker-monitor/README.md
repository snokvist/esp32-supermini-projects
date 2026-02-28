# hdzero-headtracker-monitor

Decode HDZero BoxPro+ head-tracker PPM on an ESP32-C3 SuperMini, bridge it to CRSF/UART and PWM outputs, and present four BOOT-cycled OLED screens on a 0.96" I2C display.

## Project Summary

- Name: `hdzero-headtracker-monitor`
- Board: `esp32-c3-devkitm-1`
- PlatformIO environment: `esp32c3_supermini`
- Function: decode PPM head-tracker stream, emit CRSF RC channels, parse incoming CRSF on UART1 for PWM servo outputs, and provide four OLED/UI runtime screens
- OLED output:
  - `SSD1306` `128x64` over I2C
  - fixed wiring: `GPIO4`=`SDA`, `GPIO5`=`SCL`, address `0x3C`
- CRSF outputs:
  - USB CDC (`/dev/ttyACM0`) carries binary CRSF while on `HDZ>CRSF`, `UART>PWM`, or `CRSF TX12`
  - USB CDC normally follows PPM, but falls back to incoming CRSF RX when PPM is no longer healthy
  - USB CDC returns to human-readable debug text in `DEBUG CFG` and route reporting shows this as `usb=TEXT`
  - UART1 on `GPIO21` TX + `GPIO20` RX at `420000` baud for direct FC/receiver wiring
  - UART1 TX remains PPM-driven only; incoming CRSF RX is not echoed back to UART1
- Servo PWM outputs:
  - `GPIO0`, `GPIO1`, `GPIO2` at `100Hz`
  - normally driven from incoming CRSF CH1..CH3, with fallback to headtracker PPM when CRSF RX becomes stale
- Web configuration:
  - AP SSID/password: `waybeam-backpack` / `waybeam-backpack`
  - AP network: `10.100.0.x` (`ESP32 = 10.100.0.1`, built-in DHCP server enabled)
  - Web UI: `http://10.100.0.1/`
  - AP/Web UI is active only while the `DEBUG CFG` screen is selected
  - configurable GPIO guardrail: `0..10, 20, 21` only (USB pins are excluded)
  - `GPIO4/5` are reserved for the OLED and rejected by runtime validation

## Goals

- `HDZ>CRSF` screen: visualize live BoxPro pan/roll/tilt with three centered bar graphs while forwarding CRSF
- `UART>PWM` screen: visualize live servo outputs from incoming CRSF UART with three centered bar graphs
- `CRSF TX12` screen: visualize the first 12 outgoing CRSF channels in two compact columns, following the active CRSF output source
- Boot default: power-on starts on `CRSF TX12`
- `DEBUG CFG` screen: enable AP/web config and keep serial debug text available when needed

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

Use the serial monitor in `DEBUG CFG` if you want readable text. On the three main runtime screens, `/dev/ttyACM0` intentionally carries binary CRSF data.

## How To Interact With This Project

1. Enable head tracker output in BoxPro+ settings.
2. Wire BoxPro 3.5mm HT jack:
   - tip -> ESP32 `GPIO10`
   - sleeve -> ESP32 `GND`
3. Optional CRSF hardware output wiring:
   - ESP32 `GPIO21` (UART1 TX) -> target device CRSF RX
   - target device CRSF TX -> ESP32 `GPIO20` (UART1 RX, parsed for CRSF RC channels)
   - ESP32 `GND` -> target device GND
4. Servo wiring:
   - servo1 signal -> `GPIO0`
   - servo2 signal -> `GPIO1`
   - servo3 signal -> `GPIO2`
   - servo ground tied to ESP32 ground (and external servo power as needed)
5. Optional OLED wiring:
   - `VCC` -> `3V3`
   - `GND` -> `GND`
   - `SDA` -> `GPIO4`
   - `SCL` -> `GPIO5`
6. Power and run firmware. Default screen is **CRSF TX12**.
7. BOOT button interaction:
   - short press: cycle `CRSF TX12` -> `HDZ>CRSF` -> `UART>PWM`
   - long press (`>3s`): enter `DEBUG CFG`
   - short press in `DEBUG CFG`: return to the last graph screen
8. In `DEBUG CFG`, open USB serial monitor for human-readable diagnostics:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

9. Move the headset and observe `HDZ>CRSF`:
   - three centered graphs for `PAN`, `ROL`, `TIL`
   - graph fill moves around center with head motion
   - guide marks show quarter/center positions and the live marker cuts through the fill
   - header shows `OK`, `WAIT`, or `STAL`
10. Feed CRSF RC data into UART1 and observe `UART>PWM`:
   - three centered graphs for `S1`, `S2`, `S3`
   - guide marks and value marker make center offset easier to read
   - header shows `RXOK`, `NONE`, or `STAL`
11. Short press once more and observe `CRSF TX12`:
   - two slim graph columns show channels `1..12` of the active outgoing CRSF payload
   - when PPM owns CRSF output, channels `1..3` move with the headtracker input and channels `4..12` stay centered by default
   - when CRSF RX owns fallback CRSF output, the screen follows the decoded incoming CRSF RX channels instead
   - header shows the active CRSF output source (`PPM`, `CRSF`, or `NONE`)
12. Switch to `DEBUG CFG` and confirm:
   - AP comes up only in this screen
   - OLED shows AP state, simplified health summary, and active pin map
   - PPM line shows the same stable windowed measured Hz used by the serial debug output and web status API
   - CRSF line shows a smoothed CRSF packet rate instead of raw packet-age spikes
   - serial text diagnostics are printed here instead of on the graph screens
   - status/debug route reporting shows which source currently owns USB CRSF output and PWM
13. Connect a phone/laptop to `waybeam-backpack` and open `http://10.100.0.1/` while `DEBUG CFG` is active to:
   - inspect the top status summary (screen, routes, PPM health/rate, CRSF RX health/rate, servo outputs)
   - change all runtime settings live
   - save settings to persistent storage (NVS) for next boot
   - reset all settings to firmware defaults with the `Reset to defaults` button (live + saved)
   - navigate settings in grouped sections with inline guidance (Pins, Modes, CRSF/UART, Servo Outputs, PPM Decode, Timing/Health)
   - use dropdowns for screen modes and servo source channels instead of manual numeric entry
   - see save-state diagnostics (`nvs_ready`), OLED status (`oled_ready`), current screen, and AP activity in status output

## Expected Behavior

- Default mode sends binary CRSF RC frames only when a decoded PPM frame arrives (no idle spam when no PPM frame is present).
- BOOT cycles four runtime screens:
  - boot starts on `CRSF TX12`
  - short press cycles `CRSF TX12` -> `HDZ>CRSF` -> `UART>PWM`
  - long press (`>3s`) enters `DEBUG CFG`
  - short press in `DEBUG CFG` returns to the last graph screen
- CRSF is written to both:
  - UART1 on `GPIO21` TX + `GPIO20` RX at `420000` baud
  - USB CDC `Serial` only while not in `DEBUG CFG` screen
- On this firmware config (`ARDUINO_USB_MODE=1`), `/dev/ttyACM0` is native USB CDC, not an automatic mirror of UART pins.
- `/dev/ttyACM0` should be treated as:
  - binary CRSF output on `HDZ>CRSF`, `UART>PWM`, and `CRSF TX12`
  - human-readable debug console only in `DEBUG CFG`
- Routing ownership uses source health plus route hysteresis:
  - a source keeps its current route until it becomes stale on its configured timeout (`signal_timeout_ms` for PPM, `crsf_rx_timeout_ms` for CRSF RX)
  - a source must re-acquire health with at least `3` valid events inside `150ms` before it can take ownership again
  - route switches between live sources are held for at least `250ms` to reduce flapping
  - healthy PPM owns CRSF output
  - healthy CRSF RX owns PWM
  - if only one source is healthy, it drives both output sides
- Incoming UART1 CRSF RC packets (`0x16`) are parsed and mapped to servo outputs:
  - CH1 -> `GPIO0`
  - CH2 -> `GPIO1`
  - CH3 -> `GPIO2`
  - output rate: `100Hz`
- If CRSF RX is no longer healthy, PWM falls back to fresh PPM headtracker channels (`PAN/ROL/TIL -> S1/S2/S3`). If neither source is healthy, servo outputs return to center (`1500us`).
- If PPM is no longer healthy, USB CRSF output falls back to healthy incoming CRSF RX packets.
- Incoming CRSF RX is mirrored only to USB during fallback. It is intentionally not re-sent on UART1 TX, which avoids output loops on attached CRSF hardware.
- The PPM input uses `INPUT_PULLDOWN` to reduce floating/noisy interrupt bursts when the headtracker cable is unplugged or bouncing.
- Boot serial now prints the ESP reset reason, which helps distinguish software faults from brownouts or hot-plug power glitches.
- OLED status output runs independently of serial/web behavior:
  - if the OLED initializes, it renders one of the four runtime pages on `GPIO4/5`
  - if the OLED is missing or init fails, firmware continues running and reports a warning on serial
- ESP32 runs a local AP (`waybeam-backpack`) and serves a Web UI on `10.100.0.1` only in `DEBUG CFG`.
- DHCP is active on the AP only while `DEBUG CFG` is selected.
- Stored settings are validated on boot; invalid/corrupt persisted settings are auto-replaced with firmware defaults.
- Web pin settings are validated against ESP32-C3-safe configurable pins (`GPIO0..10, GPIO20, GPIO21`), with `GPIO4/5` reserved for the OLED.
- `DEBUG CFG` serial output prints PPM status at roughly `~5Hz`.
- After switching into `DEBUG CFG`, first stats line appears after one monitor interval (~200ms).
- `DEBUG CFG` serial output includes:
  - measured `hz` over the current monitor window
  - `invalid(+delta)` for stale-signal/noise debugging
  - `route usb=TEXT` means USB CDC is intentionally reserved for readable debug output in `DEBUG CFG`
- Web status JSON reports the same stable windowed PPM rate used by `DEBUG CFG`:
  - `frame_hz`
  - `frame_hz_ema`
  - `ppm_window_hz`
- Web status JSON also reports active output ownership:
  - `usb_route_label`
  - `pwm_route_label`
- Web status JSON also includes CRSF packet age and smoothed CRSF packet rate:
  - `ppm_health_label`
  - `crsf_rx_health_label`
  - `crsf_rx_age_ms`
  - `crsf_rx_rate_hz`
- `crsf_rx_rate_hz` is reported as a live value only while CRSF RX is fresh; stale/missing CRSF reports no carried-over rate.
- The Web UI keeps the raw JSON status view under an expandable advanced section for deeper debugging.
- `ch1/ch2/ch3` widths are typically around `1000..2000us`.
- For BoxPro+, source-code mapping indicates:
  - `ch1 = pan`
  - `ch2 = roll`
  - `ch3 = tilt`

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
