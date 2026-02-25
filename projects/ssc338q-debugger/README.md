# ssc338q-debugger

USB-to-UART debug bridge for SSC338Q IP cameras, with remote reset control.

## Project Summary

- Name: `ssc338q-debugger`
- Board: `esp32-c3-devkitm-1` (ESP32-C3 SuperMini)
- PlatformIO environment: `esp32c3_supermini`

## Overview

The ESP32-C3 SuperMini acts as a transparent UART bridge between a host PC (USB CDC) and the SSC338Q camera's debug UART0. It also provides a remote reset trigger via the camera's active-low reset line.

```
Host PC  <-- USB CDC -->  ESP32-C3  <-- UART1 (115200) -->  Camera UART0
                              |
                         GPIO4 (push-pull) --> Camera RESET (active-low)
```

## Wiring (4 wires)

| ESP32-C3 Pin | Camera Pin | Wire Color (suggested) |
|--------------|------------|----------------------|
| GPIO21 (TX)  | UART0 RX   | Yellow |
| GPIO20 (RX)  | UART0 TX   | Green  |
| GPIO4        | RESET (JST)| White  |
| GND          | GND        | Black  |

See `docs/HARDWARE.md` for detailed pinout and wiring notes.

## How To Build and Flash

### Prerequisites

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html) installed
- ESP32-C3 SuperMini connected via USB

### Build

From the project directory (`projects/ssc338q-debugger`):

```bash
pio run
```

Expected output ends with:

```
RAM:   [          ]   4.3%
Flash: [==        ]  20.3%
========================= [SUCCESS] ...
```

### Flash

1. Connect the ESP32-C3 SuperMini to USB. It should appear as `/dev/ttyACM0` (Linux) or similar.
2. Flash:

```bash
pio run -t upload --upload-port /dev/ttyACM0
```

If upload fails with a permissions error, ensure your user is in the `dialout` group:

```bash
sudo usermod -aG dialout $USER
# Log out and back in for the change to take effect
```

If the board doesn't enter flash mode automatically, hold the BOOT button while pressing RESET, then retry the upload command.

### Connect to the Serial Monitor

The `platformio.ini` is configured with `monitor_filters = direct`, which passes all bytes through raw — required for Ctrl-R (reset) and Ctrl-T (status) to reach the ESP32.

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

**Note:** In `direct` mode, Ctrl-C does not quit the monitor. To exit, kill it from another terminal (`pkill -f "pio device monitor"`) or unplug the USB cable.

Alternatively, use any raw serial terminal (minicom, screen, picocom):

```bash
picocom -b 115200 /dev/ttyACM0
```

## Verifying Functionality

### Step 1: Confirm Boot (no camera needed)

1. Flash the ESP32 and open a serial terminal.
2. You should see:

```
[debugger] SSC338Q UART Debugger ready
[debugger] Ctrl-R = reset camera | Ctrl-T = status
```

3. Press **Ctrl-T**. You should see a status line:

```
[debugger] Uptime: 0h00m05s | To camera: 0 bytes | To host: 0 bytes | Resets: 0
```

This confirms the firmware is running and the USB CDC link works.

### Step 2: Verify Camera UART Bridge

1. Wire the ESP32 to the camera (see Wiring table above).
2. Power the camera.
3. Camera boot logs should appear in the serial terminal immediately.
4. Type a command (e.g., press Enter) — the camera should echo or respond, confirming bidirectional data flow.
5. Press **Ctrl-T** — byte counters should be non-zero.

### Step 3: Verify Reset Control

1. With the camera running, press **Ctrl-R**.
2. You should see:

```
[debugger] Reset pulse started (200 ms) [count=1]
[debugger] Reset released, camera booting
```

3. The camera boot log should appear again as it reboots.
4. The onboard LED should turn solid during the 200ms reset pulse, then return to idle.

### Troubleshooting

| Symptom | Check |
|---------|-------|
| No boot message | Verify USB cable is data-capable (not charge-only). Try a different port. |
| Boot message OK but no camera data | Check TX/RX wiring — they must be crossed (ESP TX→Camera RX, ESP RX→Camera TX). Verify camera is powered. |
| Ctrl-R has no effect | Verify GPIO4 is wired to camera RST pad. Ensure `S99resetd` is running on the camera (see below). |
| Garbled camera output | Confirm camera UART is 115200 baud, 8N1. |
| `/dev/ttyACM0` not found | Board may enumerate as a different device — check `ls /dev/ttyACM*` or `dmesg | tail`. |

## Escape Commands

| Key | Code | Action |
|-----|------|--------|
| Ctrl-R | `0x12` | Trigger 200ms camera reset pulse |
| Ctrl-T | `0x14` | Print status line (uptime, byte counts, reset count) |

All other bytes are forwarded to the camera as-is.

## LED Behavior

- **Blink**: Data activity (either direction)
- **Solid ON**: Reset pulse in progress
- **Off**: Idle

## Camera-Side Reset Setup

The SSC338Q RST JST pad is connected to the SoC's GPIO 10, which is a general-purpose pin — not a hardware reset input. A daemon on the camera must monitor this GPIO and trigger a software reboot when the ESP32 pulls it LOW.

### Install the reset daemon

```bash
scp -O camera-scripts/S99resetd root@192.168.2.10:/etc/init.d/
ssh root@192.168.2.10 "chmod +x /etc/init.d/S99resetd"
```

The daemon starts automatically on boot. To start it immediately without rebooting:

```bash
ssh root@192.168.2.10 "/etc/init.d/S99resetd start"
```

### How it works

1. ESP32 receives Ctrl-R from host
2. ESP32 drives GPIO4 LOW for 200ms
3. Camera's GPIO 10 goes LOW (wired to ESP32 GPIO4)
4. `S99resetd` daemon detects the LOW signal (~0.5s threshold)
5. Camera executes `reboot`

The daemon uses 1% CPU (polls every 250ms via sysfs).

## AI Agent Usage (Claude Code / Codex)

The following is a reference for AI coding agents that need to interact with the SSC338Q camera through this debug bridge. Paste it into your agent context or AGENTS.md.

---

### SSC338Q Debug Bridge — Agent Reference

An ESP32-C3 SuperMini is connected via USB at `/dev/ttyACM0` and bridges to the SSC338Q camera's UART console at 115200 baud. The bridge is transparent and bidirectional.

**Reading camera output** (non-interactive, best for agents):

```bash
# Configure port and read for N seconds
stty -F /dev/ttyACM0 115200 raw -echo
timeout 10 cat /dev/ttyACM0
```

**Sending a command to the camera:**

```bash
# Send a single command (echo adds a newline)
echo "cat /etc/os-release" > /dev/ttyACM0
# Then read the response
timeout 5 cat /dev/ttyACM0
```

**Send and capture in one shot:**

```bash
stty -F /dev/ttyACM0 115200 raw -echo
# Start reading in background, send command, wait for output
timeout 5 cat /dev/ttyACM0 &
sleep 0.2
echo "uname -a" > /dev/ttyACM0
wait
```

**Trigger a camera reset** (200ms active-low pulse via GPIO4):

```bash
# Send Ctrl-R (0x12) to the bridge
printf '\x12' > /dev/ttyACM0
# Then read boot log
timeout 30 cat /dev/ttyACM0
```

**Query bridge status** (uptime, byte counters, reset count):

```bash
printf '\x14' > /dev/ttyACM0
timeout 2 cat /dev/ttyACM0
```

**Important notes for agents:**

- The serial port is `/dev/ttyACM0` (may vary — check `ls /dev/ttyACM*`).
- Baud rate is 115200, 8N1. Always run `stty -F /dev/ttyACM0 115200 raw -echo` before first use in a session.
- The camera runs OpenIPC Linux. After boot, a login prompt appears. Default login is `root` with password from the OpenIPC build.
- `0x12` (Ctrl-R) and `0x14` (Ctrl-T) are intercepted by the bridge. All other bytes pass through to the camera.
- Camera boot takes ~15-20 seconds after reset. Wait accordingly before sending commands.
- If the camera is at a login prompt, send credentials before running commands:
  ```bash
  echo "root" > /dev/ttyACM0
  sleep 0.5
  echo "your_password" > /dev/ttyACM0
  sleep 0.5
  ```
- Multiple processes must not open the serial port simultaneously. Close any running `cat` or monitor before sending commands.
- Output may contain ANSI color codes (`\e[1;31m` etc.). Strip them if parsing:
  ```bash
  timeout 5 cat /dev/ttyACM0 | sed 's/\x1b\[[0-9;]*m//g'
  ```

---

## Project Layout

- `src/`: firmware source
- `include/`: headers
- `lib/`: private libraries
- `test/`: tests
- `docs/HARDWARE.md`: wiring, pins, modules
- `docs/TASK_LOG.md`: implementation log
