# HDZero Headtracker Monitor — User Guide

*ESP32-C3 SuperMini firmware for HDZero BoxPro+ head-tracker bridging,
CRSF output, servo PWM control, BLE gamepad input, and OLED status display.*

---

## Table of Contents

1. [What This Project Does](#1-what-this-project-does)
2. [Hardware Overview](#2-hardware-overview)
3. [Wiring Guide](#3-wiring-guide)
4. [Flashing the Firmware](#4-flashing-the-firmware)
5. [OLED Screens Reference](#5-oled-screens-reference)
6. [Button Controls](#6-button-controls)
7. [LED Status Patterns](#7-led-status-patterns)
8. [Web Configuration UI](#8-web-configuration-ui)
9. [Usage Scenarios](#9-usage-scenarios)
10. [Signal Routing and Failover](#10-signal-routing-and-failover)
11. [BLE Gamepad Mode](#11-ble-gamepad-mode)
12. [Configuration Reference](#12-configuration-reference)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. What This Project Does

This firmware turns an ESP32-C3 SuperMini into a versatile head-tracker
bridge for FPV systems. It decodes the PPM signal from an HDZero BoxPro+
headset and converts it into CRSF RC channel data that flight controllers,
receivers, and other FPV gear can understand.

**Key capabilities:**

- Decode HDZero BoxPro+ PPM head-tracker output (pan, roll, tilt)
- Output CRSF RC frames over USB or hardware UART (or both)
- Parse incoming CRSF on UART and drive 3 servo PWM outputs
- Merge PPM head-tracker channels into an existing CRSF stream
- Connect a BLE gamepad (e.g. 8BitDo Ultimate 2C) as an input source
- Display live signal status on a 0.96" OLED with 4+ screens
- Configure everything via a built-in WiFi web interface

---

## 2. Hardware Overview

### Parts List

| Component | Specification | Notes |
|---|---|---|
| ESP32-C3 SuperMini | ESP32-C3 dev board with USB-C | Any SuperMini-compatible board |
| OLED display | 0.96" SSD1306, 128x64, I2C | White mono or yellow/blue dual-color |
| HDZero BoxPro+ | FPV headset with head-tracker | 3.5mm TS jack for PPM output |
| 3.5mm TS cable | Tip-Sleeve audio cable | Mono cable or stereo with ring unused |
| Servo motors (optional) | Standard hobby servos | For gimbal/pan-tilt control |
| BLE gamepad (optional) | 8BitDo Ultimate 2C or similar | BLE HID gamepad |

### ESP32-C3 SuperMini Pinout

The board has a USB-C connector at the top and pin headers on both sides.
This diagram shows the board from the **top** (component side facing you,
USB-C pointing up):

```
                    ┌──────────┐
                    │  USB-C   │
         ┌──────────┤          ├──────────┐
         │  GPIO5 ○─┤          ├─○ GPIO6  │
         │  GPIO4 ○─┤          ├─○ GPIO7  │
         │  GPIO3 ○─┤          ├─○ GPIO8  │  ← Onboard LED
         │  GPIO2 ○─┤  ESP32   ├─○ GPIO9  │  ← BOOT button
         │  GPIO1 ○─┤   -C3    ├─○ GPIO10 │
         │  GPIO0 ○─┤          ├─○ GPIO20 │
         │    3V3 ○─┤          ├─○ GPIO21 │
         │    GND ○─┤          ├─○    VCC │  (5V via USB)
         │     5V ○─┤          ├─○    GND │
         └──────────┴──────────┴──────────┘
```

### Pin Assignments (Default)

```
         ┌──────────┐
         │  USB-C   │  ← CRSF output (420000 baud) + power
┌────────┤          ├────────┐
│ GPIO5 ─┤  OLED    ├─ GPIO6 │
│ (SCL)  │  SERVO   │        │
│ GPIO4 ─┤  PPM     ├─ GPIO7 │
│ (SDA)  │  CRSF    │        │
│ GPIO3 ─┤          ├─ GPIO8 │  ← Status LED
│        │  ESP32   │  (LED) │
│ GPIO2 ─┤   -C3    ├─ GPIO9 │  ← BOOT button (mode cycle)
│(SERVO3)│          │ (BOOT) │
│ GPIO1 ─┤          ├─ GPIO10│  ← PPM input (from BoxPro+)
│(SERVO2)│          │ (PPM)  │
│ GPIO0 ─┤          ├─ GPIO20│  ← CRSF UART RX (from FC)
│(SERVO1)│          │(UART RX│
│    3V3 ─┤         ├─ GPIO21│  ← CRSF UART TX (to FC)
│    GND ─┤         ├─   VCC │   (UART TX)
│     5V ─┤         ├─   GND │
└─────────┴─────────┴────────┘

  ╔═══════════════════════════════════════════╗
  ║  Function       Pin       Direction       ║
  ╠═══════════════════════════════════════════╣
  ║  OLED SDA       GPIO4     Output (I2C)    ║
  ║  OLED SCL       GPIO5     Output (I2C)    ║
  ║  Servo 1        GPIO0     PWM output      ║
  ║  Servo 2        GPIO1     PWM output      ║
  ║  Servo 3        GPIO2     PWM output      ║
  ║  PPM Input      GPIO10    Digital input    ║
  ║  CRSF UART TX   GPIO21    Serial output   ║
  ║  CRSF UART RX   GPIO20    Serial input    ║
  ║  Status LED     GPIO8     Digital output   ║
  ║  BOOT button    GPIO9     Digital input    ║
  ╚═══════════════════════════════════════════╝
```

> **Note:** GPIO4 and GPIO5 are permanently reserved for the OLED and
> cannot be reassigned. All other pins are configurable via the web UI.

### OLED Display

The firmware supports the common 0.96" I2C OLED modules based on the
SSD1306 driver (128x64 pixels). Both mono (white) and dual-color
(yellow header / blue body) variants are supported — select the type
in the web UI for optimal layout spacing.

```
  ┌─────────────────────────┐
  │  ┌───────────────────┐  │
  │  │                   │  │  128 x 64 pixels
  │  │   OLED SCREEN     │  │  SSD1306 driver
  │  │   0.96 inch       │  │  I2C interface
  │  │                   │  │
  │  └───────────────────┘  │
  │  GND  VCC  SCL  SDA    │
  │   │    │    │    │      │
  └───┼────┼────┼────┼──────┘
      │    │    │    │
     GND  3V3  GP5  GP4
      └────┴────┴────┘
       ESP32-C3 SuperMini
```

---

## 3. Wiring Guide

### Minimal Setup: BoxPro+ Head Tracker Only

Connect just the HDZero headset for CRSF output over USB:

```
  HDZero BoxPro+                    ESP32-C3 SuperMini
  3.5mm HT jack                    ┌──────────┐
  ┌──────┐                         │  USB-C   │──→ To PC / receiver
  │ TIP ─┼──── (1kΩ resistor) ────→│ GPIO10   │
  │SLEEVE┼─────────────────────────→│ GND      │
  └──────┘                         └──────────┘
```

> **Tip:** The 1kΩ series resistor is recommended as protection while
> validating unknown signal levels. The firmware enables an internal
> pull-down on the PPM pin to reduce noise when the cable is unplugged.

### Full Setup: Head Tracker + OLED + CRSF UART + Servos

```
  HDZero BoxPro+            ESP32-C3 SuperMini               Peripherals
  ┌──────────┐         ┌──────────────────────────┐
  │          │         │                          │
  │  TIP  ───┼────────→│ GPIO10 (PPM in)          │
  │ SLEEVE ──┼────────→│ GND                      │
  └──────────┘         │                          │
                       │ GPIO21 (UART TX) ────────┼──→ Flight controller RX
  ┌──────────┐         │ GPIO20 (UART RX) ←───────┼─── Flight controller TX
  │   OLED   │         │                          │
  │  SDA  ───┼────────→│ GPIO4                    │    ┌────────────────┐
  │  SCL  ───┼────────→│ GPIO5                    │    │   Servo 1      │
  │  VCC  ───┼────────→│ 3V3                      │    │  signal ←──────┼── GPIO0
  │  GND  ───┼────────→│ GND                      │    │  GND ──────────┼── GND
  └──────────┘         │                          │    └────────────────┘
                       │ GPIO0 (Servo 1 PWM) ─────┼─→  ┌────────────────┐
                       │ GPIO1 (Servo 2 PWM) ─────┼─→  │   Servo 2      │
                       │ GPIO2 (Servo 3 PWM) ─────┼─→  │   Servo 3      │
                       │                          │    └────────────────┘
                       │  USB-C ──────────────────┼──→ CRSF output / power
                       └──────────────────────────┘

  Servo power: Use an external 5V–6V supply for servo VCC.
  Connect servo GND to ESP32 GND (common ground).
```

### HDZero BoxPro+ 3.5mm Jack Wiring

The head-tracker output uses a 2-contact TS (Tip-Sleeve) connection:

```
  3.5mm plug cross-section:

  ════╤════╤════
      │    │
     TIP  SLEEVE
      │    │
    PPM   GND
   signal
```

- **Tip** = PPM head-tracker output (3 channels: pan, roll, tilt)
- **Sleeve** = Ground
- Ring (if present on a TRS cable) is unused

---

## 4. Flashing the Firmware

### Prerequisites

- [PlatformIO](https://platformio.org/) installed (CLI or IDE plugin)
- USB-C cable connected to the ESP32-C3 SuperMini

### Build and Flash

```bash
# Navigate to the project directory
cd esp32-supermini-projects/projects/hdzero-headtracker-monitor

# Build
pio run

# Flash (adjust port if needed)
pio run -t upload --upload-port /dev/ttyACM0
```

### Verify Boot

After flashing, the USB port carries binary CRSF data at 420000 baud.
To see the boot text (before CRSF starts), connect at 420000 baud
immediately after reset:

```bash
pio device monitor -p /dev/ttyACM0 -b 420000
```

You should see:

```
Boot: hdzero-headtracker-monitor
Reset reason: POWERON (1)
Pins: PPM=10 LED=8 BTN=9 UART_RX=20 UART_TX=21 SERVO=0,1,2
OLED I2C: SDA=4, SCL=5, addr=0x3C
OLED status screen ready
Screen -> CRSF TX12 (boot)
```

> **Note:** Boot text is only visible when the CRSF output target is set
> to "USB Serial" (default). If set to "HW UART TX", USB is silent.

---

## 5. OLED Screens Reference

The firmware has four main screens cycled with the BOOT button, plus
additional debug sub-pages. The header bar shows the screen name on the
left and a status indicator on the right.

### Screen 1: CRSF TX12 (Boot Default)

Shows the first 12 outgoing CRSF channels in a compact two-column layout.
This is the default screen after power-on.

```
  ┌────────────────────────────────┐
  │▌CRSF TX12           PPM      ▐│  ← Header (white on black)
  ├────────────────────────────────┤
  │ 01 ╞══════╪══════╡ 07 ╞══╪═══╡│  ← Ch 1, 7
  │ 02 ╞══════╪══════╡ 08 ╞══╪═══╡│  ← Ch 2, 8
  │ 03 ╞════╪═╪══════╡ 09 ╞══╪═══╡│  ← Ch 3, 9
  │ 04 ╞══════╪══════╡ 10 ╞══╪═══╡│  ← Ch 4, 10
  │ 05 ╞══════╪══════╡ 11 ╞══╪═══╡│  ← Ch 5, 11
  │ 06 ╞══════╪══════╡ 12 ╞══╪═══╡│  ← Ch 6, 12
  └────────────────────────────────┘
         ╪ = center line
         ╞ ╡ = bar endpoints
```

**Header status:** Shows the active CRSF output source:
- **PPM** — head-tracker PPM is driving CRSF output
- **CRSF** — incoming CRSF RX is driving output (fallback)
- **NONE** — no active input source

**Bar behavior:**
- Each bar is centered at 992 CRSF ticks (1500 us)
- Bars fill left or right from center as channels deviate
- Channels mapped from PPM (default: 1–3) move with head motion
- Unmapped channels remain centered
- If a channel drives a servo, its label is highlighted (inverse video)

When BLE gamepad mode is active, the header changes to **BLUETOOTH**
with a BLE connection status indicator.

### Screen 2: HDZ>CRSF

Shows three large centered bar graphs for the head-tracker PPM channels
(pan, roll, tilt). Best for visually confirming head tracking response.

```
  ┌────────────────────────────────┐
  │▌HDZ>CRSF              OK     ▐│  ← Header
  ├────────────────────────────────┤
  │                                │
  │ PAN ╞════▓▓▓▓╪▒▒════════╡ 1102│  ← Pan channel
  │                                │
  │ ROL ╞════════╪═══▒▓▓▓▓══╡  887│  ← Roll channel
  │                                │
  │ TIL ╞════════╪▒══════════╡ 1005│  ← Tilt channel
  │                                │
  └────────────────────────────────┘
         ▓▓▓ = filled bar (deviation from center)
         ▒ = live position marker
         ╪ = center line
         │╡ = guide marks at 25%/50%/75%
```

**Header status:**
- **OK** — PPM signal is healthy and active
- **WAIT** — no PPM signal received yet since boot
- **STAL** — PPM signal was received but has gone stale (timed out)

**Bar details:**
- Bar width: 76 pixels with 12-pixel height
- Guide marks at 0%, 25%, 50% (center), 75%, 100%
- Fill extends bidirectionally from center
- A vertical marker with small wings shows the exact current position
- Numeric value displayed on the right (CRSF ticks, center = 992)

**Channel mapping** (from HDZero BoxPro+ source code):
- **PAN** = Channel 1 (horizontal head rotation)
- **ROL** = Channel 2 (head tilt side-to-side)
- **TIL** = Channel 3 (head tilt forward/back)

### Screen 3: UART>PWM

Shows three large centered bar graphs for the servo PWM outputs, driven
by incoming CRSF data on the UART RX pin. Same layout as HDZ>CRSF but
tracks servo positions instead of PPM input.

```
  ┌────────────────────────────────┐
  │▌UART>PWM             RXOK    ▐│  ← Header
  ├────────────────────────────────┤
  │                                │
  │  S1 ╞════════╪▒══════════╡ 1500│  ← Servo 1 (GPIO0)
  │                                │
  │  S2 ╞════════╪▒══════════╡ 1500│  ← Servo 2 (GPIO1)
  │                                │
  │  S3 ╞════════╪▒══════════╡ 1500│  ← Servo 3 (GPIO2)
  │                                │
  └────────────────────────────────┘
```

**Header status:**
- **RXOK** — CRSF data is being received on UART RX
- **NONE** — no CRSF data received yet since boot
- **STAL** — CRSF RX has gone stale (timed out)

**Values:** Servo pulse width in microseconds (1000–2000, center 1500).

### Screen 4: DEBUG CFG (Debug / Configuration)

A text-based status screen that also activates the WiFi access point
and web configuration UI. The debug screen has multiple sub-pages
cycled with short presses.

**Debug Page 0 — Status Overview:**

```
  ┌────────────────────────────────┐
  │▌DBG STATUS          AP ON    ▐│  ← Header
  ├────────────────────────────────┤
  │ PPM OK 50.0Hz win             │  ← PPM health + rate
  │ CRSF RXOK 100Hz              │  ← CRSF RX health + rate
  │ AP 10.100.0.1                 │  ← Access point IP
  │ PPM=10 S=0,1,2               │  ← Pin assignments
  │ short >pg  long exit          │  ← Button hint
  └────────────────────────────────┘
```

**Debug Page 1 — Signal Routes:**

```
  ┌────────────────────────────────┐
  │▌DBG ROUTES         MRG ON   ▐│  ← Header
  ├────────────────────────────────┤
  │ USB:PPM src PPM:OK            │  ← USB output route
  │ PWM:CRSF src CRSF:OK         │  ← PWM output route
  │ Mrg 3ch 1/2/3                │  ← Merge config
  │ Map 1>S1 2>S2 3>S3           │  ← Servo mapping
  │ Out:USB short>pg              │  ← Output target
  └────────────────────────────────┘
```

**Debug Page 2 — Ranges and Timing:**

```
  ┌────────────────────────────────┐
  │▌DBG RANGES           50Hz   ▐│  ← Header
  ├────────────────────────────────┤
  │ PPM 900-2100us                │  ← PPM pulse range
  │ CRSF map 700-2300us          │  ← CRSF mapping range
  │ Srv 1000/1500/2000us         │  ← Servo min/center/max
  │ T/O PPM:500ms CRSF:200ms    │  ← Timeout thresholds
  │ UART 420000  WiFi ch6        │  ← UART baud + WiFi channel
  └────────────────────────────────┘
```

**Header AP status:**
- **AP OFF** — WiFi access point is not running (non-debug screens)
- **AP WAIT** — AP startup has been requested, waiting for WiFi stack
- **AP RETRY** — AP startup failed, retrying with backoff
- **AP ON** — AP is running and ready for connections

---

## 6. Button Controls

The onboard **BOOT** button (GPIO9) controls screen navigation and
BLE toggle:

```
  ╔══════════════════════════════════════════════════╗
  ║  Gesture              Action                     ║
  ╠══════════════════════════════════════════════════╣
  ║  Short press          Cycle to next screen       ║
  ║  Double press (<0.4s) Toggle BLE on/off + save   ║
  ║  Long press (>3s)     Enter / exit DEBUG CFG     ║
  ╚══════════════════════════════════════════════════╝
```

**Double-press** rapidly toggles Bluetooth gamepad mode on or off.
The change is applied live and saved to flash immediately — no reboot
required. The LED pattern will switch to reflect the new state
(e.g. BLE scanning triple-pulse, or back to idle/PPM heartbeat).

```
  Screen cycle order:

    ┌──────────┐  short   ┌──────────┐  short   ┌──────────┐
    │ CRSF TX12├─────────→│ HDZ>CRSF ├─────────→│ UART>PWM │
    │  (boot)  │←─────────┤          │←─────────┤          │
    └────┬─────┘  short   └────┬─────┘          └────┬─────┘
         │                     │                     │
         │ long press          │ long press           │ long press
         ▼                     ▼                     ▼
    ┌──────────────────────────────────────────────────┐
    │                  DEBUG CFG                        │
    │  Page 0: Status ─→ Page 1: Routes ─→ Page 2: ... │
    │         short press cycles pages                  │
    │         long press exits to last normal screen    │
    └──────────────────────────────────────────────────┘

         double-press on ANY screen → toggle BLE on/off
```

> **Note:** The short press fires after a brief delay (~400ms) to
> distinguish it from a double-press. This is the only noticeable
> difference from previous firmware behavior.

---

## 7. LED Status Patterns

The onboard blue LED (GPIO8) blinks in distinct patterns to indicate the
current operating state. This is especially useful when no OLED is attached
— you can diagnose the system state from the LED alone.

### Pattern Reference

```
  IDLE (no input):
  ▓░░░░░░░░░░░░░░░░░░░░░░░░░▓░░░░░░░░░░░░░░░░░░░░░░░░░
  80ms ON ──────────── 2.5s OFF ──────────── repeat
  Meaning: Powered on, waiting for signal


  PPM ACTIVE (head tracker connected):
  ▓▓░░░░░░░░░░░░░░░░░░░░▓▓░░░░░░░░░░░░░░░░░░░░
  150ms ON ──────── 2s OFF ──────── repeat
  Meaning: PPM signal healthy, CRSF output active


  CRSF RX ACTIVE (receiving CRSF on UART):
  ▓░▓░░░░░░░░░░░░░░░░░░░░░░▓░▓░░░░░░░░░░░░░░░
  80ms ON, 120ms OFF, 80ms ON ── 2s OFF ── repeat
  Meaning: CRSF data on UART RX, servos active


  BOTH ACTIVE (PPM + CRSF RX):
  ▓▓▓░░░░░░░░░░░░░░░░▓▓▓░░░░░░░░░░░░░░░░
  200ms ON ──── 1.5s OFF ──── repeat
  Meaning: Normal operation, both inputs healthy


  DEBUG CONFIG MODE:
  ▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░▓░
  40ms ON, 40ms OFF ── continuous rapid blink
  Meaning: WiFi AP active, web UI available


  BLE SCANNING:
  ▓░▓░▓░░░░░░░░░░░░░░░░░░░░▓░▓░▓░░░░░░░
  3x (60ms ON, 100ms OFF) ── 2s OFF ── repeat
  Meaning: Searching for Bluetooth gamepad


  BLE CONNECTED:
  ▓▓░▓░░░░░░░░░░░░░░░░░░░░▓▓░▓░░░░░░░░░
  150ms ON, 120ms OFF, 60ms ON ── 2s OFF ── repeat
  Meaning: BLE gamepad paired and sending data


  BLE LOST:
  ▓░▓░▓░▓░░░░░░░░░░░░▓░▓░▓░▓░░░░░░░░░░
  4x (60ms ON, 60ms OFF) ── 1.2s OFF ── repeat
  Meaning: BLE connection lost, attempting reconnect
```

### Quick Identification Guide

| What you see | State | Action needed |
|---|---|---|
| Very slow single pulse | No input | Connect PPM or enable BLE |
| Single heartbeat pulse | PPM only | Working — head tracker active |
| Double quick pulse | CRSF RX only | UART receiving, no PPM |
| Long single heartbeat | Both inputs | Normal full operation |
| Rapid continuous blink | Debug mode | WiFi AP is up, connect to configure |
| Triple quick pulse | BLE scanning | Waiting for gamepad — put it in pairing mode |
| Long-short pulse | BLE connected | Gamepad paired and active |
| Fast quad pulse | BLE lost | Connection dropped — will auto-reconnect |

### No OLED? No Problem

The LED patterns are designed so you can fully diagnose the system without
a display. Combined with the web UI (enter debug mode by long-pressing
BOOT — you'll see the rapid blink), you can configure and monitor
everything wirelessly.

The firmware handles a missing OLED gracefully:
- If the OLED is not detected on the I2C bus, a warning is printed at boot
- All other functions (CRSF, PPM, servos, WiFi, BLE) continue normally
- The web UI status reports `oled_ready: 0` so you can confirm remotely
- LED patterns work identically with or without an OLED

---

## 8. Web Configuration UI

The web UI is available **only while the DEBUG CFG screen is active**.
This keeps WiFi off during normal operation to avoid interference.

### Connecting

1. Navigate to the **DEBUG CFG** screen (long-press BOOT)
2. Wait for the OLED to show **AP ON**
3. Connect your phone/laptop to WiFi network: **`waybeam-backpack`** (open, no password)
4. Open a browser to **`http://10.100.0.1/`**

### Layout Overview

The web UI has three tabs and a status summary at the top:

```
  ┌──────────────────────────────────────────┐
  │         waybeam-backpack Config           │
  ├──────────────────────────────────────────┤
  │                                          │
  │  ┌─────────┐ ┌──────┐ ┌──────────────┐  │
  │  │ Screen  │ │ USB  │ │ PPM          │  │  ← Status cards
  │  │CRSF TX12│ │Route │ │ OK / 50.0Hz  │  │
  │  │OLED ok  │ │ PPM  │ │ T/O 500ms    │  │
  │  └─────────┘ └──────┘ └──────────────┘  │
  │  ┌─────────┐ ┌──────┐ ┌──────────────┐  │
  │  │  PWM    │ │CRSF  │ │ Servos       │  │
  │  │ Route   │ │ RX   │ │1500/1500/1500│  │
  │  │  CRSF   │ │RXOK  │ │ Merge: 3ch   │  │
  │  └─────────┘ └──────┘ └──────────────┘  │
  │                                          │
  │  [ Apply + Save ]  [ Reset ]  [Refresh]  │  ← Action buttons
  │                                          │
  │  ┌────────┐ ┌──────────┐ ┌─────────┐    │
  │  │ Setup  │ │ Advanced │ │ Gamepad │    │  ← Tab buttons
  │  │(active)│ │          │ │         │    │
  │  └────────┘ └──────────┘ └─────────┘    │
  │                                          │
  │  ┌── Modes ─────────────────────────┐    │
  │  │ Live screen    [CRSF TX12  ▼]   │    │
  │  │ Boot default   [CRSF TX12  ▼]   │    │
  │  │ CRSF output    [USB Serial ▼]   │    │
  │  │ OLED type      [Mono       ▼]   │    │
  │  └─────────────────────────────────┘    │
  │                                          │
  │  ┌── Servo Mapping ────────────────┐    │
  │  │ Servo 1 source  [CH1  ▼]       │    │
  │  │ Servo 2 source  [CH2  ▼]       │    │
  │  │ Servo 3 source  [CH3  ▼]       │    │
  │  │ PWM frequency   [100  ▼] Hz    │    │
  │  └─────────────────────────────────┘    │
  │  ...                                     │
  └──────────────────────────────────────────┘
```

### Tab Contents

**Setup** (default tab):
- **Modes** — live screen, boot default, CRSF output target, OLED type
- **Servo Mapping** — which CRSF channel drives each servo, PWM frequency
- **Servo Fallback** — position for each servo when signal is lost
- **CRSF Channel Merge** — overlay PPM channels onto CRSF stream

**Advanced** tab:
- **Pins** — reassign GPIO pins for LED, PPM, button, UART, servos
- **CRSF / UART** — baud rate, PPM-to-CRSF mapping range, timeouts
- **Servo Pulse Range** — min/center/max microseconds
- **PPM Decode** — pulse width bounds, sync gap threshold
- **Timing / Health** — signal timeout, debounce, monitor interval
- **WiFi AP** — channel selection, TX power

**Gamepad** tab:
- **BLE enable** — toggle Bluetooth gamepad mode on/off
- **Channel mapping** — 12 channels with source dropdown and invert checkbox

### Visual Feedback

- **Amber highlight** on changed fields (unsaved modifications)
- **Save button** shows pending change count: "Apply + Save (3)"
- **Save button disabled** when no changes are pending
- **Reset** restores all firmware defaults (live + saved to flash)

---

## 9. Usage Scenarios

### Scenario A: Basic Head Tracker to USB CRSF

*Use case: Feed head-tracker data to a PC application or USB receiver.*

**Wiring:** BoxPro+ 3.5mm jack → GPIO10, USB-C → PC

**What happens:**
1. Power on — OLED shows **CRSF TX12** with centered bars
2. Move your head — channels 1–3 respond on the bar graphs
3. PC receives binary CRSF RC frames at 420000 baud on `/dev/ttyACM0`
4. All 16 CRSF channels are present; channels 4–16 stay centered

```
  HDZero BoxPro+          ESP32-C3            PC / Receiver
  ┌────────────┐       ┌──────────┐       ┌──────────────┐
  │ Head motion│──PPM─→│ Decode + │─CRSF─→│ /dev/ttyACM0 │
  │ Pan/Roll/  │       │ Pack to  │  USB  │ 420000 baud  │
  │ Tilt       │       │ CRSF     │       │ 16 channels  │
  └────────────┘       └──────────┘       └──────────────┘
```

### Scenario B: Head Tracker to Flight Controller via UART

*Use case: Wire the ESP32 directly to a flight controller or ELRS receiver
CRSF input for head-tracker-as-RC.*

**Wiring:** BoxPro+ → GPIO10, GPIO21 (TX) → FC CRSF RX, common GND

**Configuration:**
1. Enter DEBUG CFG (long-press BOOT)
2. Connect to `waybeam-backpack` WiFi, open `http://10.100.0.1/`
3. Set **CRSF output target** to **HW UART TX** (or **Both**)
4. Save settings
5. Return to normal screen (long-press BOOT)

```
  HDZero BoxPro+      ESP32-C3          Flight Controller
  ┌────────────┐   ┌──────────┐      ┌──────────────────┐
  │ PPM output │──→│ GPIO10   │      │                  │
  └────────────┘   │          │      │ CRSF RX ←────────┼── GPIO21
                   │          │      │ CRSF TX ─────────┼─→ GPIO20
                   └──────────┘      └──────────────────┘
```

### Scenario C: Head Tracker + Servo Gimbal Control

*Use case: BoxPro+ drives CRSF output while incoming CRSF from a flight
controller drives servo-based camera gimbal.*

**Wiring:** Full setup — BoxPro+, OLED, UART to FC, 3 servos

**Signal flow:**
1. PPM from BoxPro+ → packed into CRSF → sent out via USB/UART
2. CRSF from flight controller → received on UART RX → decoded
3. CRSF channels 1–3 → mapped to servo PWM on GPIO0, GPIO1, GPIO2

```
  BoxPro+ ──PPM──→ ESP32 ──CRSF──→ Flight Controller
                     │
                     │ ←──CRSF──── Flight Controller
                     │
                     ├──PWM──→ Servo 1 (Pan)
                     ├──PWM──→ Servo 2 (Roll)
                     └──PWM──→ Servo 3 (Tilt)
```

**Failover behavior:**
- If CRSF RX drops out, servos automatically fall back to PPM head-tracker
- If PPM drops out, CRSF output falls back to forwarding incoming CRSF RX
- If both drop out, servos center at 1500 us

### Scenario D: CRSF Channel Merge (Head Tracker + RC)

*Use case: Overlay head-tracker channels onto an existing CRSF RC stream,
so a single output contains both stick inputs and head tracking.*

**Setup:**
1. CRSF from RC receiver → ESP32 UART RX (GPIO20)
2. PPM from BoxPro+ → ESP32 GPIO10
3. Merged CRSF → ESP32 USB or UART TX

**Configuration:**
1. In web UI, enable **CRSF Channel Merge**
2. Set **Merge count** (1–8 PPM channels to overlay)
3. Configure **Merge map** (which PPM channel → which CRSF slot)
4. Default: PPM ch1 → CRSF ch1, ch2 → ch2, ch3 → ch3

```
  RC Receiver              ESP32-C3              Output
  ┌──────────┐          ┌──────────┐
  │ 16ch CRSF├──UART───→│          │
  └──────────┘          │  MERGE   ├──CRSF──→ 16 channels
                        │          │          (3 from PPM,
  BoxPro+ ──PPM────────→│ 3ch PPM  │           13 from RC)
                        └──────────┘
```

### Scenario E: BLE Gamepad as RC Input

*Use case: Use a Bluetooth gamepad (e.g. 8BitDo Ultimate 2C) as an RC
controller, outputting CRSF over USB or UART.*

**Enable BLE:**
- **Quick way:** Double-press the BOOT button on any screen
- **Web UI way:** Open the Gamepad tab and enable Bluetooth

**Configure channels (web UI):**
1. Map gamepad axes/buttons to CRSF channels (12 configurable)
2. Use invert checkboxes to reverse axis directions
3. Save and return to normal screen

**OLED behavior:**
- **CRSF TX12** header shows **BLUETOOTH** with connection status
- Status: **SCAN** (searching), **LINK** (connecting), **OK** (paired)
- Debug pages show RSSI, error counts, and BLE-specific mapping

```
  8BitDo Controller         ESP32-C3            Output
  ┌───────────────┐      ┌──────────┐
  │  Sticks       │      │          │
  │  Triggers  ───┼─BLE─→│  Map to  ├──CRSF──→ Receiver/FC
  │  D-Pad        │      │  12 ch   │
  │  Buttons      │      │          │
  └───────────────┘      └──────────┘
```

---

## 10. Signal Routing and Failover

The firmware continuously monitors both input sources (PPM and CRSF RX)
and automatically routes them to outputs based on health status.

### Routing Rules

```
  ┌─────────────────────────────────────────────────────────┐
  │                   ROUTING TABLE                         │
  ├──────────┬──────────┬───────────────────────────────────┤
  │ PPM      │ CRSF RX  │ Result                            │
  ├──────────┼──────────┼───────────────────────────────────┤
  │ Healthy  │ Healthy  │ PPM → CRSF out, CRSF RX → Servos │
  │ Healthy  │ Stale    │ PPM → CRSF out + Servos (fallback)│
  │ Stale    │ Healthy  │ CRSF RX → CRSF out + Servos      │
  │ Stale    │ Stale    │ No CRSF out, Servos → center      │
  └──────────┴──────────┴───────────────────────────────────┘
```

### Health State Machine

Each source transitions through these states:

```
                    3 valid events
       ┌─────────  within 150ms    ─────────┐
       │                                     ▼
  ┌─────────┐   first event   ┌──────────┐     ┌─────────┐
  │  STALE  │────────────────→│ TENTATIVE│────→│ HEALTHY │
  │(timed   │                 │(acquiring│     │(active  │
  │  out)   │←────────────────│  health) │←────│ owner)  │
  └─────────┘   timeout       └──────────┘     └─────────┘
                                                    │
                                         timeout    │
                                    (configurable)  │
                                         ┌──────────┘
                                         ▼
                                    ┌─────────┐
                                    │  STALE  │
                                    └─────────┘
```

### Hysteresis Protection

Route switching is protected against flapping:

- **Source timeout:** PPM uses `signal_timeout_ms` (default 500ms),
  CRSF RX uses `crsf_rx_timeout_ms` (default 200ms)
- **Re-acquire threshold:** 3 valid events within 150ms before a
  previously-stale source can take ownership again
- **Switch hold:** 250ms minimum between route changes when both
  sources are live, preventing rapid oscillation

---

## 11. BLE Gamepad Mode

When Bluetooth is enabled, the ESP32 scans for and connects to a BLE
HID gamepad. Tested with the **8BitDo Ultimate 2C**.

BLE can be toggled on/off by **double-pressing the BOOT button** on any
screen. The setting is saved to flash immediately. You can also toggle
it from the web UI Gamepad tab.

### Channel Mapping (Default for 8BitDo Ultimate 2C)

| CRSF Channel | Default Source | Description |
|---|---|---|
| CH1 | L-Stick X | Aileron |
| CH2 | L-Stick Y | Elevator |
| CH3 | R-Stick Y | Throttle |
| CH4 | R-Stick X | Rudder |
| CH5 | L-Trigger | Aux 1 |
| CH6 | R-Trigger | Aux 2 |
| CH7 | D-Pad X | Aux 3 |
| CH8 | D-Pad Y | Aux 4 |
| CH9–12 | Buttons 1–4 | Aux 5–8 |

Each channel has a configurable source dropdown and an **Inv** (invert)
checkbox in the web UI Gamepad tab.

### BLE Connection States

| OLED Label | Meaning |
|---|---|
| **IDLE** | Bluetooth off or waiting to start |
| **SCAN** | Scanning for BLE gamepad devices |
| **LINK** | Connecting to a discovered device |
| **OK** | Connected and receiving HID reports |
| **LOST** | Connection was lost, will auto-reconnect |

---

## 12. Configuration Reference

All settings are persisted in flash (NVS) and survive power cycles.
Invalid or corrupt saved settings are auto-replaced with firmware
defaults on boot.

### Settings Overview

| Setting | Default | Range | Tab |
|---|---|---|---|
| Live screen | CRSF TX12 | CRSF TX12 / HDZ>CRSF / UART>PWM / DEBUG CFG | Setup |
| Boot default screen | CRSF TX12 | same as above | Setup |
| CRSF output target | USB Serial | USB Serial / HW UART TX / Both | Setup |
| OLED type | Mono | Mono / Dual-color | Setup |
| Servo 1/2/3 source | CH1/CH2/CH3 | CH1–CH16 | Setup |
| PWM frequency | 100 Hz | 50–400 Hz | Setup |
| Servo fallback | 1500 us | 500–2500 us | Setup |
| Merge enable | Off | On / Off | Setup |
| Merge count | 3 | 1–8 | Setup |
| HW UART baud | 420000 | Standard baud rates | Advanced |
| PPM CRSF map min | 700 us | — | Advanced |
| PPM CRSF map max | 2300 us | — | Advanced |
| CRSF RX timeout | 200 ms | — | Advanced |
| Servo pulse min | 1000 us | — | Advanced |
| Servo pulse center | 1500 us | — | Advanced |
| Servo pulse max | 2000 us | — | Advanced |
| PPM min channel | 900 us | — | Advanced |
| PPM max channel | 2100 us | — | Advanced |
| PPM sync gap | 3000 us | — | Advanced |
| Signal timeout | 500 ms | — | Advanced |
| WiFi AP channel | 6 | 1–11 | Advanced |
| WiFi TX power | 78 (19.5dBm) | 8–78 | Advanced |
| BLE enable | Off | On / Off | Gamepad |
| BLE channel 1–12 source | Various | Axis/button sources | Gamepad |
| BLE channel 1–12 invert | Off | On / Off | Gamepad |

### Pin Reassignment Rules

Configurable pins: GPIO 0–10, 20, 21.

**Reserved pins (cannot be reassigned):**
- GPIO4 — OLED SDA (always)
- GPIO5 — OLED SCL (always)
- GPIO9 — BOOT button (default, but technically reassignable)

**Excluded pins:**
- GPIO11–19 — not routed or USB-reserved on SuperMini boards
- USB D+/D- pins — excluded to prevent bricking USB connectivity

---

## 13. Troubleshooting

### OLED not working

| Symptom | Check |
|---|---|
| Blank screen | Verify wiring: SDA→GPIO4, SCL→GPIO5, VCC→3V3, GND→GND |
| Garbled display | Confirm I2C address is 0x3C (most common for 0.96" SSD1306) |
| Yellow/blue layout looks off | Change OLED type in web UI (Setup → Mono or Dual-color) |
| Boot log says "OLED missing" | OLED not detected on I2C bus — check solder joints |

> The firmware runs fine without an OLED. All other functions continue normally.

### WiFi AP not appearing

| Symptom | Check |
|---|---|
| No `waybeam-backpack` in WiFi list | Must be on DEBUG CFG screen (long-press BOOT) |
| OLED shows "AP WAIT" | AP is still starting up — wait a few seconds |
| OLED shows "AP RETRY" | AP failed to start — wait for retry (backoff) |
| Can see AP but can't connect | Move closer; AP uses channel 6, open network |

### No CRSF output on USB

| Symptom | Check |
|---|---|
| No data on `/dev/ttyACM0` | Confirm CRSF output target is "USB Serial" or "Both" |
| Garbled data | Must use 420000 baud (not 115200) |
| Output target is "HW UART TX" | USB is intentionally silent in this mode |
| Data present but channels centered | Connect BoxPro+ PPM cable and enable head tracker |

### Servos not moving

| Symptom | Check |
|---|---|
| All servos centered | No CRSF data on UART RX, or both sources stale |
| Servos jittery | Check servo power supply (not just ESP32 3V3) |
| Wrong servo responds | Check servo source channel mapping in web UI |
| OLED shows "NONE" on UART>PWM | No CRSF data received yet on GPIO20 |

### PPM signal issues

| Symptom | Check |
|---|---|
| OLED shows "WAIT" forever | Enable head tracker in BoxPro+ settings menu |
| OLED shows "STAL" intermittently | Check 3.5mm cable connection; try adding 1kΩ resistor |
| Channels don't match head motion | Verify ch1=pan, ch2=roll, ch3=tilt (BoxPro+ default) |
| Hz shows ~25 instead of ~50 | Normal for some HDZero firmware versions |

### BLE gamepad issues

| Symptom | Check |
|---|---|
| Stuck on "SCAN" | Ensure gamepad is in pairing mode; power cycle both devices |
| "LINK" then "LOST" repeating | Gamepad may not be BLE HID compatible; try closer range |
| Axes not responding | Check channel source mapping in Gamepad tab |
| Axis direction wrong | Toggle the Inv checkbox for that channel |

---

*This guide covers firmware as of the latest development build. Pin
assignments, screen layouts, and web UI fields are all configurable
and may differ from defaults if settings have been changed.*

---

**Project:** `esp32-supermini-projects/projects/hdzero-headtracker-monitor`
**License:** Autod Personal Use License
**Repository:** [waybeam-coordination](https://github.com/snokvist/waybeam-coordination)
