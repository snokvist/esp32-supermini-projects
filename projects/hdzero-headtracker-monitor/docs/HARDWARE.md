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
3. Flash this project and open serial monitor at `115200`.
4. Move headset and confirm `ch1/ch2/ch3` values move around center (~1500us).

Channel interpretation for BoxPro+ (from HDZero source code, inferred):

- `ch1 = pan`
- `ch2 = roll`
- `ch3 = tilt`
