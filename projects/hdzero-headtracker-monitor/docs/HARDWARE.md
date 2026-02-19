# Hardware Notes - hdzero-headtracker-monitor

## Target Board

- Board config: `esp32-c3-devkitm-1`
- Environment: `esp32c3_supermini`

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- Baud: `115200`

## Pin Map (Initial)

- LED pin: `GPIO8` (adjust per board variant)
- PPM input pin: `GPIO2` (configurable via `PPM_INPUT_PIN`)
- Mode toggle button pin: `GPIO9` (onboard BOOT button, active low, internal pull-up enabled)

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

Recommended protection while validating unknown signal levels:

- Add `1k` series resistor between tip and GPIO input
- If you later measure >3.3V pulses, add a divider/level shifter

Bring-up checklist:

1. Enable head tracking on BoxPro+ menu.
2. Plug jack and power both devices.
3. Flash this project.
4. Default runtime mode emits CRSF RC channel frames on serial (`115200`).
5. Press BOOT to switch to PPM monitor text mode when needed.
6. In monitor mode, open serial monitor at `115200` and confirm:
   - `win=...Hz(ok)` stays near `~50Hz` (warns outside `45..55Hz`)
   - `ch1/ch2/ch3` move around center (~1500us)
   - `invalid(+delta)` remains stable unless signal noise/wiring issues are present

Channel interpretation for BoxPro+ (from HDZero source code, inferred):

- `ch1 = pan`
- `ch2 = roll`
- `ch3 = tilt`
