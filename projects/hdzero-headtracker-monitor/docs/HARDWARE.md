# Hardware Notes - hdzero-headtracker-monitor

## Target Board

- Board config: `esp32-c3-devkitm-1`
- Environment: `esp32c3_supermini`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- USB serial monitor baud: `115200`
- CRSF UART link: `UART1 TX GPIO21 + RX GPIO20 @ 420000` baud
- Note: with `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1`, `/dev/ttyACM0` is native USB CDC and is not an automatic UART pin mirror.

## WiFi / Web UI

- AP SSID: `waybeam-backpack`
- AP password: `waybeam-backpack`
- AP IP: `10.100.0.1`
- AP subnet: `255.255.255.0` (`10.100.0.x`)
- DHCP: enabled (ESP32 SoftAP built-in DHCP server)
- Web UI: `http://10.100.0.1/`

## Pin Map (Initial)

- LED pin: `GPIO8` (adjust per board variant)
- PPM input pin: `GPIO2` (configurable via `PPM_INPUT_PIN`)
- Mode toggle button pin: `GPIO9` (onboard BOOT button, active low, internal pull-up enabled)
- CRSF UART1 TX pin: `GPIO21` (to FC/receiver CRSF RX)
- CRSF UART1 RX pin: `GPIO20` (from FC/receiver CRSF TX, RC packets parsed)
- Servo PWM outputs (`100Hz`):
  - Servo1 (CRSF CH1): `GPIO3`
  - Servo2 (CRSF CH2): `GPIO4`
  - Servo3 (CRSF CH3): `GPIO5`

BOOT/strapping caution:

- `GPIO9` is a strapping/BOOT-related pin on ESP32-C3 boards.
- Keep external wiring on `GPIO9` minimal and avoid forcing levels at reset/boot time.
- This project only reads the onboard BOOT switch and does not require external button wiring.

## Wiring

HDZero BoxPro+ headtracker jack is a 2-contact 3.5mm TS connection:

- Tip: `HT PPM Output`
- Sleeve: `Ground`

Connect:

- BoxPro tip -> ESP32 `GPIO2`
- BoxPro sleeve -> ESP32 `GND`
- ESP32 `GPIO21` -> target CRSF RX
- target CRSF TX -> ESP32 `GPIO20` (incoming CRSF RC input)
- ESP32 `GND` -> target device GND
- servo1 signal -> ESP32 `GPIO3`
- servo2 signal -> ESP32 `GPIO4`
- servo3 signal -> ESP32 `GPIO5`
- servo GND -> ESP32 GND (use external servo power rail as needed)

Recommended protection while validating unknown signal levels:

- Add `1k` series resistor between tip and GPIO input
- If you later measure >3.3V pulses, add a divider/level shifter

Bring-up checklist:

1. Enable head tracking on BoxPro+ menu.
2. Plug jack and power both devices.
3. Flash this project.
4. Default runtime mode emits CRSF RC channel frames on:
   - USB CDC (`/dev/ttyACM0`, monitor at `115200`)
   - UART1 `GPIO21` TX + `GPIO20` RX at `420000`
   - PWM outputs at `100Hz` on `GPIO3/4/5` from incoming CRSF CH1/CH2/CH3
5. Connect client device to `waybeam-backpack`, browse to `http://10.100.0.1/`, and verify:
   - settings page loads
   - settings are grouped by section for quick navigation (Pins, Modes, CRSF/UART, Servo, PPM, Timing)
   - status JSON updates (CRSF RX counters / servo values)
   - apply/save actions are accepted
   - `Reset to defaults` restores firmware defaults live and persists them
6. Press BOOT to switch to PPM monitor text mode when needed.
7. In monitor mode, open serial monitor at `115200` and confirm:
   - `win=...Hz(ok)` stays near `~50Hz` (warns outside `45..55Hz`)
   - `ch1/ch2/ch3` move around center (~1500us)
   - `invalid(+delta)` remains stable unless signal noise/wiring issues are present

Channel interpretation for BoxPro+ (from HDZero source code, inferred):

- `ch1 = pan`
- `ch2 = roll`
- `ch3 = tilt`
