# Hardware Notes - hdzero-headtracker-monitor

## Target Board

- Board config: `esp32-c3-devkitm-1`
- Environment: `esp32c3_supermini`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- USB serial monitor baud: `115200`
- CRSF UART link: `UART1 TX GPIO21 + RX GPIO20 @ 420000` baud
- Note: with `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1`, `/dev/ttyACM0` is native USB CDC and is not an automatic UART pin mirror.
- Runtime USB CDC behavior:
  - `HDZ>CRSF` and `UART>PWM`: `/dev/ttyACM0` carries binary CRSF frames
  - healthy PPM owns USB CRSF output, with fallback to healthy CRSF RX when PPM drops stale
  - `DEBUG CFG`: `/dev/ttyACM0` carries human-readable debug text, reported as `usb=TEXT`
- UART1 TX note:
  - CRSF RX fallback is mirrored to USB only
  - incoming CRSF RX is not echoed back to UART1 TX, which avoids routing loops on attached CRSF hardware

## OLED Module

- Type: 0.96" I2C OLED (`SSD1306`, `128x64`)
- Default address: `0x3C`
- Logic level: `3.3V`
- Fixed OLED I2C pins:
  - `GPIO4` = `SDA`
  - `GPIO5` = `SCL`
- Design choice:
  - `GPIO4/5` are reserved for the OLED and are blocked from runtime reassignment in the web settings validator

## WiFi / Web UI

- AP SSID: `waybeam-backpack`
- AP password: `waybeam-backpack`
- AP IP: `10.100.0.1`
- AP subnet: `255.255.255.0` (`10.100.0.x`)
- DHCP: enabled only while `DEBUG CFG` screen is active
- Web UI: `http://10.100.0.1/` while `DEBUG CFG` is active
- Status JSON includes `nvs_ready` (1 when preferences persistence is available).

## Configurable GPIO Guardrails

- Runtime pin reassignment via Web UI is restricted to:
  - `GPIO0..GPIO10`
  - `GPIO20`, `GPIO21`
- This intentionally excludes USB D-/D+ pins and unsupported/unrouted GPIOs to reduce brick-risk while using native USB CDC.
- Additional OLED reservation:
  - `GPIO4`, `GPIO5` are treated as reserved and cannot be assigned to LED, PPM, button, UART, or servo roles

## Pin Map (Initial)

- LED pin: `GPIO8` (adjust per board variant)
- PPM input pin: `GPIO10` (configurable via `PPM_INPUT_PIN`)
- Mode toggle button pin: `GPIO9` (onboard BOOT button, active low, internal pull-up enabled)
- OLED I2C:
  - `GPIO4` -> `SDA`
  - `GPIO5` -> `SCL`
- CRSF UART1 TX pin: `GPIO21` (to FC/receiver CRSF RX)
- CRSF UART1 RX pin: `GPIO20` (from FC/receiver CRSF TX, RC packets parsed)
- Servo PWM outputs (`100Hz`):
  - Servo1 (CRSF CH1): `GPIO0`
  - Servo2 (CRSF CH2): `GPIO1`
  - Servo3 (CRSF CH3): `GPIO2`

BOOT/strapping caution:

- `GPIO9` is a strapping/BOOT-related pin on ESP32-C3 boards.
- Keep external wiring on `GPIO9` minimal and avoid forcing levels at reset/boot time.
- This project only reads the onboard BOOT switch and does not require external button wiring.

## Wiring

HDZero BoxPro+ headtracker jack is a 2-contact 3.5mm TS connection:

- Tip: `HT PPM Output`
- Sleeve: `Ground`

Connect:

- BoxPro tip -> ESP32 `GPIO10`
- BoxPro sleeve -> ESP32 `GND`
- OLED `VCC` -> ESP32 `3V3`
- OLED `GND` -> ESP32 `GND`
- OLED `SDA` -> ESP32 `GPIO4`
- OLED `SCL` -> ESP32 `GPIO5`
- ESP32 `GPIO21` -> target CRSF RX
- target CRSF TX -> ESP32 `GPIO20` (incoming CRSF RC input)
- ESP32 `GND` -> target device GND
- servo1 signal -> ESP32 `GPIO0`
- servo2 signal -> ESP32 `GPIO1`
- servo3 signal -> ESP32 `GPIO2`
- servo GND -> ESP32 GND (use external servo power rail as needed)

Recommended protection while validating unknown signal levels:

- Add `1k` series resistor between tip and GPIO input
- Internal `INPUT_PULLDOWN` is enabled on the PPM pin in firmware:
  - this weakly biases the input low when the cable is unplugged
  - it reduces floating/noise-triggered interrupts during hot-plug or poor ground conditions
- If you later measure >3.3V pulses, add a divider/level shifter

Bring-up checklist:

1. Enable head tracking on BoxPro+ menu.
2. Plug jack and power both devices.
3. Flash this project.
4. Confirm default runtime behavior:
   - default screen is `HDZ>CRSF`
   - BOOT short press toggles `HDZ>CRSF` <-> `UART>PWM`
   - BOOT long press (`>3s`) enters `DEBUG CFG`
   - short press in `DEBUG CFG` returns to the last graph screen
   - fresh/healthy PPM is emitted on UART1 `GPIO21` TX and on USB CDC while not in `DEBUG CFG`
   - if PPM loses health, USB CDC falls back to healthy CRSF RX
   - PWM outputs run at `100Hz` on `GPIO0/1/2`, normally from incoming CRSF CH1/CH2/CH3
   - if CRSF RX loses health, PWM falls back to the PPM headtracker channels
   - OLED output is active on `GPIO4/5`
   - route hysteresis uses:
     - source-specific stale timeouts (`signal_timeout_ms` for PPM, `crsf_rx_timeout_ms` for CRSF RX)
     - `3` valid events within `150ms` to re-acquire source health before a source can take ownership again
     - `250ms` minimum hold when switching between still-live sources
5. In `HDZ>CRSF`, verify:
   - header shows `OK`, `WAIT`, or `STAL`
   - three centered graphs track `PAN`, `ROL`, `TIL`
   - graph rows show edge/quarter/center guide marks and a live value marker
6. In `UART>PWM`, verify:
   - header shows `RXOK`, `NONE`, or `STAL`
   - three centered graphs track `S1`, `S2`, `S3`
   - graph rows show edge/quarter/center guide marks and a live value marker
7. In `DEBUG CFG`, verify:
   - AP starts only in this screen
   - OLED shows AP state, health summary, and pin map
   - PPM line shows the same stable windowed measured Hz used by the serial debug output and status API
   - CRSF line shows a smoothed packet-rate estimate while CRSF is fresh, and no stale carried-over rate once CRSF drops out
   - serial diagnostics are active on `/dev/ttyACM0`
8. Connect client device to `waybeam-backpack`, browse to `http://10.100.0.1/`, and verify:
   - settings page loads
   - top summary cards show current screen, route ownership, PPM health/rate, CRSF RX health/rate, and servo outputs
   - settings are grouped by section for quick navigation (Pins, Modes, CRSF/UART, Servo, PPM, Timing)
   - mode and servo source fields use dropdowns instead of raw numeric entry
   - status JSON updates (`web_ui_active`, `output_mode_label`, CRSF RX counters/rate, servo values, `oled_ready`)
   - route labels update correctly (`usb_route_label`, `pwm_route_label`)
   - status JSON includes `nvs_ready: 1` during normal operation
   - apply/save actions are accepted
   - `Reset to defaults` restores firmware defaults live and persists them
   - invalid pin selections are rejected by API validation, including any attempt to use `GPIO4/5`
9. In `DEBUG CFG`, open serial monitor at `115200` and confirm:
   - `hz=...Hz` reports the measured PPM frame rate directly
   - `ch1/ch2/ch3` move around center (~1500us)
   - `invalid(+delta)` remains stable unless signal noise/wiring issues are present
   - `route usb=TEXT pwm=...` indicates USB CDC is reserved for readable debug text in this screen
   - outside `DEBUG CFG`, `route usb=... pwm=...` reflects current output ownership
   - boot output includes `Reset reason: ...`, useful when diagnosing hot-plug resets

Channel interpretation for BoxPro+ (from HDZero source code, inferred):

- `ch1 = pan`
- `ch2 = roll`
- `ch3 = tilt`
