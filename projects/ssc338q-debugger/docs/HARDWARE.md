# Hardware Notes - ssc338q-debugger

## Target Board

- Board config: `esp32-c3-devkitm-1`
- Environment: `esp32c3_supermini`
- MCU: ESP32-C3 (RISC-V, single core, 160 MHz)

## USB / Serial

- Default upload/monitor port: `/dev/ttyACM0`
- Baud: `115200`
- USB CDC on boot enabled (`ARDUINO_USB_CDC_ON_BOOT=1`)

## Pin Map

| Function | GPIO | Direction | Notes |
|----------|------|-----------|-------|
| Status LED | GPIO8 | Output | Onboard LED, active-low |
| Camera UART TX | GPIO21 | Output | UART1 TX -> Camera RX |
| Camera UART RX | GPIO20 | Input  | UART1 RX <- Camera TX |
| Camera RESET | GPIO4 | Push-pull | OUTPUT HIGH normally; OUTPUT LOW for reset pulse |

## ESP32-C3 SuperMini Pinout Reference

Available GPIOs: 0-10, 20, 21

- GPIO0-GPIO10: General purpose
- GPIO20: Default UART0 RX (remapped to UART1 RX in firmware)
- GPIO21: Default UART0 TX (remapped to UART1 TX in firmware)
- GPIO8: Onboard LED (active-low on most SuperMini boards)

## Camera Connection

### UART0 Debug Port

The SSC338Q exposes a 3.3V UART debug console (typically 115200 8N1). Connect:

- Camera TX -> ESP32 GPIO20 (UART1 RX)
- Camera RX -> ESP32 GPIO21 (UART1 TX)

### Reset Line (2-pin JST Header)

The camera has a reset line accessible via a 2-pin JST connector:

- Pin 1: RESET (active-low, no internal pull-up)
- Pin 2: GND

The ESP32 drives RESET using push-pull (GPIO4):

1. **Normal**: GPIO4 driven `HIGH` — camera runs
2. **Reset**: GPIO4 driven `LOW` for 200ms — camera resets
3. **Release**: GPIO4 driven `HIGH` — camera boots

No external transistor or level shifter is needed (both are 3.3V logic).

**NOTE**: The SSC338Q RST pad is wired to SoC GPIO 10, which is a
general-purpose I/O — not a hardware reset input. A software daemon
(`S99resetd`) must run on the camera to monitor this GPIO and trigger
`reboot` when the ESP32 pulls it LOW. See `camera-scripts/S99resetd`.

## Wiring Diagram

```
ESP32-C3 SuperMini          SSC338Q Camera
==================          ==============

GPIO21 (TX) ───────────────> UART0 RX
GPIO20 (RX) <─────────────── UART0 TX
GPIO4       ───────────────> RESET (JST pin 1)
GND         ───────────────> GND   (JST pin 2 / UART GND)
```
