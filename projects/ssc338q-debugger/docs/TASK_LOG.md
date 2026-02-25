# Task Log - ssc338q-debugger

Use this file as a running implementation log.

## Entries

### 2026-02-25 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `esp32-c3-devkitm-1`
- Env: `esp32c3_supermini`

### 2026-02-25 - UART Bridge Firmware Implemented

- Transparent bidirectional UART bridge: USB CDC <-> UART1 (115200)
- Camera UART on GPIO20 (RX) / GPIO21 (TX)
- Open-drain reset on GPIO3: Ctrl-R triggers 200ms active-low pulse
- Status reporting: Ctrl-T prints uptime, byte counts, reset count
- LED activity indicator on GPIO8 (blink on data, solid during reset)
- Documentation: README, HARDWARE.md with wiring diagram

### 2026-02-25 - UART0 Pin Conflict Fix and Monitor Config

- Fixed garbled camera output: ESP32-C3 bootloader maps UART0 to GPIO20/21
  by default, conflicting with UART1. Added `gpio_reset_pin()` calls to
  detach UART0 before configuring UART1.
- Added `monitor_filters = direct` to platformio.ini so PlatformIO monitor
  passes Ctrl-R/Ctrl-T through to the ESP32 instead of intercepting them.
- Added USB CDC ready wait (up to 3s) so boot banner is visible.
- Added AI agent usage reference to README for Claude Code / Codex integration.
- Verified on hardware: clean camera boot log, login prompt, reset, and
  status commands all working.
