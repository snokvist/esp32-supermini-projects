# hdzero-headtracker-monitor

Decode HDZero BoxPro+ head-tracker PPM on an ESP32-C3 SuperMini, bridge it to CRSF/UART and PWM outputs, and present three BOOT-cycled OLED screens on a 0.96" I2C display.

## Project Summary

- Name: `hdzero-headtracker-monitor`
- Board: `esp32-c3-devkitm-1`
- PlatformIO environment: `esp32c3_supermini`
- Function: decode PPM head-tracker stream, emit CRSF RC channels, parse incoming CRSF on UART1 for PWM servo outputs, and provide three OLED/UI runtime screens
- OLED output:
  - `SSD1306` `128x64` over I2C
  - fixed wiring: `GPIO4`=`SDA`, `GPIO5`=`SCL`, address `0x3C`
- CRSF outputs:
  - USB CDC (`/dev/ttyACM0`) carries binary CRSF while on `HDZ>CRSF` or `UART>PWM`
  - USB CDC returns to human-readable debug text in `DEBUG CFG`
  - UART1 on `GPIO21` TX + `GPIO20` RX at `420000` baud for direct FC/receiver wiring
- Servo PWM outputs:
  - `GPIO0`, `GPIO1`, `GPIO2` at `100Hz` (driven from incoming CRSF CH1..CH3)
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
- `DEBUG CFG` screen: enable AP/web config and keep serial debug text available when needed

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

Use the serial monitor in `DEBUG CFG` if you want readable text. On the two graph screens, `/dev/ttyACM0` intentionally carries binary CRSF data.

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
6. Power and run firmware. Default screen is **HDZ>CRSF**.
7. BOOT button interaction:
   - short press: toggle between `HDZ>CRSF` and `UART>PWM`
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
11. Switch to `DEBUG CFG` and confirm:
   - AP comes up only in this screen
   - OLED shows AP state, simplified health summary, and active pin map
   - PPM line shows the same stable windowed measured Hz used by the serial debug output and web status API
   - serial text diagnostics are printed here instead of on the graph screens
12. Connect a phone/laptop to `waybeam-backpack` and open `http://10.100.0.1/` while `DEBUG CFG` is active to:
   - inspect runtime status (frame rates, CRSF RX health, servo outputs)
   - change all runtime settings live
   - save settings to persistent storage (NVS) for next boot
   - reset all settings to firmware defaults with the `Reset to defaults` button (live + saved)
   - navigate settings in grouped sections (Pins, Modes, CRSF/UART, Servo Outputs, PPM Decode, Timing/Health)
   - see save-state diagnostics (`nvs_ready`), OLED status (`oled_ready`), current screen, and AP activity in status output

## Expected Behavior

- Default mode sends binary CRSF RC frames only when a decoded PPM frame arrives (no idle spam when no PPM frame is present).
- BOOT cycles three runtime screens:
  - short press toggles `HDZ>CRSF` <-> `UART>PWM`
  - long press (`>3s`) enters `DEBUG CFG`
  - short press in `DEBUG CFG` returns to the last graph screen
- CRSF is written to both:
  - UART1 on `GPIO21` TX + `GPIO20` RX at `420000` baud
  - USB CDC `Serial` only while not in `DEBUG CFG` screen
- On this firmware config (`ARDUINO_USB_MODE=1`), `/dev/ttyACM0` is native USB CDC, not an automatic mirror of UART pins.
- `/dev/ttyACM0` should be treated as:
  - binary CRSF output on `HDZ>CRSF` and `UART>PWM`
  - human-readable debug console only in `DEBUG CFG`
- Incoming UART1 CRSF RC packets (`0x16`) are parsed and mapped to servo outputs:
  - CH1 -> `GPIO0`
  - CH2 -> `GPIO1`
  - CH3 -> `GPIO2`
  - output rate: `100Hz`
- If CRSF RX packets go stale for >`500ms`, servo outputs return to center (`1500us`).
- OLED status output runs independently of serial/web behavior:
  - if the OLED initializes, it renders one of the three runtime pages on `GPIO4/5`
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
- Web status JSON reports the same stable windowed PPM rate used by `DEBUG CFG`:
  - `frame_hz`
  - `frame_hz_ema`
  - `ppm_window_hz`
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
