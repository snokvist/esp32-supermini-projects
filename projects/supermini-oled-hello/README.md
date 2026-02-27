# supermini-oled-hello

ESP32-C3 SuperMini + 0.96" I2C OLED (`SSD1306`) hello-world demo.

## Project Summary

- Name: `supermini-oled-hello`
- Board: `esp32-c3-devkitm-1`
- PlatformIO environment: `esp32c3_supermini`
- Function: initialize a 128x64 OLED over I2C and render a stable `Hello, World!` screen with uptime.
  - includes BOOT-button mode cycling for graph, dead-pixel test, and recovery screens

## Goals

- Provide a known-good wiring and firmware baseline for 0.96" I2C OLED displays.
- Verify I2C bring-up on ESP32-C3 SuperMini using explicit pins.
- Keep output simple and deterministic for first hardware validation.
- Add a 30 FPS-style animated bar graph mode for UI performance testing.
- Add a full-screen pixel flash mode for dead-pixel detection.
- Add a high-speed recovery mode to exercise potentially stuck pixels.

## How To Build and Flash

```bash
pio run
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## How To Interact With This Project

1. Wire a common 4-pin I2C OLED module (`SSD1306`) to the ESP32-C3:
   - `VCC` -> `3V3`
   - `GND` -> `GND`
   - `SDA` -> `GPIO4`
   - `SCL` -> `GPIO5`
2. Flash the firmware.
3. Open serial monitor:

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

4. Confirm serial boot logs show:
   - `Boot: supermini-oled-hello`
   - `I2C pins: SDA=4, SCL=5, addr=0x3C`
   - `Mode button: GPIO9 (active-low)`
   - `Mode -> HELLO (boot)`
   - recurring `Uptime: Ns` once per second
5. Press the onboard `BOOT` button once to switch to graph mode:
   - OLED switches to animated equalizer-style bars
   - serial prints `Mode -> GRAPH (button)` and periodic `Graph FPS: ...`
6. Press `BOOT` again to switch to pixel-test mode:
   - OLED alternates full white and full black every `2s`
   - serial prints `Mode -> PIXEL_TEST (button)` plus `Pixel flash: ON/OFF`
7. Press `BOOT` again to switch to recovery mode:
   - OLED runs fast invert/checker/noise patterns
   - serial prints `Mode -> RECOVERY (button)` and `Recovery FPS: ...`
8. Press `BOOT` again to return to hello mode.
9. Optional: send `t` on serial to cycle mode without pressing the button.
10. On the OLED, confirm:
   - `Hello,` and `World!` are centered.
   - Uptime in seconds updates once per second.
   - graph mode updates smoothly near the 30 FPS target.
   - pixel-test mode flashes full screen every 2 seconds.
   - recovery mode animates rapidly with varied full-screen patterns.

## Expected Behavior

- OLED initializes at I2C address `0x3C` on `GPIO4/5`.
- Hello mode: screen remains stable with centered hello text.
- Uptime overlay updates every `1000ms`.
- Serial monitor prints `Uptime: Ns` every `1000ms`.
- BOOT button (`GPIO9`) cycles through `HELLO -> GRAPH -> PIXEL_TEST -> RECOVERY -> HELLO`.
- Graph mode renders sound-style bars with a frame target of about `30 FPS`.
- Serial monitor prints measured graph refresh (`Graph FPS: X.X`) approximately once per second.
- Pixel-test mode alternates full-screen ON/OFF every `2000ms` for dead-pixel checks.
- Recovery mode cycles fast full-white/full-black/checker/noise patterns and prints `Recovery FPS: X.X`.
- If display init fails, firmware prints:
  - `ERROR: SSD1306 init failed. Check wiring, power, and I2C address.`
  - and stays in a safe idle loop.

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
