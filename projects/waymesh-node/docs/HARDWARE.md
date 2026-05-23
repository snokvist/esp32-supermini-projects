# Hardware Notes - waymesh-node

## Target Board: RadioMaster XR2 Nano

- MCU: **ESP32-C3** (board config `esp32-c3-devkitm-1`, env `esp32c3_supermini`)
- Radio: **Semtech LR1121**, **2.4 GHz only** on this board (integrated 2.4 GHz
  tower antenna, no sub-GHz RF path)
- Size/weight: 16 × 12 × 6 mm, 0.8 g
- Working voltage: **5 V** (onboard regulator to 3.3 V)
- Exposed IO: CRSF (TX/RX) + a secondary UART; WiFi for flashing
- Ships with ExpressLRS v3.5.1 — **open source, drives the LR1121 on this exact
  board → the authoritative pin-map reference**

## USB / Serial

- Monitor over USB-CDC at **115200** (the C3 provides USB)
- Upload port (when bench-connected): `/dev/ttyACM0`

## ⚠️ Pin map — PLACEHOLDERS, verify before flashing

The values in `src/board_config.h` are **not** confirmed XR2 pins. Phase 0,
task #1 is to confirm them. Get them from the ExpressLRS RadioMaster XR2 RX
hardware target (<https://github.com/ExpressLRS/targets>) and/or the XR2
schematic, then update `board_config.h`. Mapping:

| ELRS target field | board_config.h define | Purpose |
|-------------------|----------------------|---------|
| `radio_nss`  | `PIN_LORA_NSS`  | SPI chip select |
| `radio_sck`  | `PIN_LORA_SCK`  | SPI clock |
| `radio_miso` | `PIN_LORA_MISO` | SPI MISO |
| `radio_mosi` | `PIN_LORA_MOSI` | SPI MOSI |
| `radio_busy` | `PIN_LORA_BUSY` | LR1121 BUSY handshake |
| `radio_dio1` | `PIN_LORA_DIO1` | IRQ |
| `radio_rst`  | `PIN_LORA_RST`  | reset |
| (RF switch DIOs) | RadioLib `setRfSwitchTable` | TX/RX routing — **needed** |
| (TCXO)       | `LORA_TCXO_V`   | TCXO control voltage |

Also confirm the **LED GPIO** (`PIN_LED`, and `LED_ACTIVE_LOW`) and the **spare
UART pads** used for GNSS (`PIN_GNSS_RX`, `PIN_GNSS_TX`).

## Wiring

- **GNSS module → XR2 spare UART.** GNSS TX → `PIN_GNSS_RX` (C3 RX), GNSS RX →
  `PIN_GNSS_TX` (C3 TX), common ground, GNSS VCC from the 3.3 V rail (ideally via
  a GPIO load switch so a sleeping node can cut it). Using the UART for GNSS
  typically **gives up the CRSF interface** — fine for a standalone mesh node.
- **LR1121** is on-board (SPI); no external wiring.
- **Antenna** is the XR2's integrated 2.4 GHz tower antenna (fixed).

## Powering as a standalone node

The XR2 expects ~5 V. Options:
1. 1S LiPo (3.7–4.2 V) → 5 V boost → XR2 5 V input (simple, small boost loss).
2. Tap the 3.3 V rail directly via an LDO/buck (fewer losses; bypasses onboard
   reg — verify the rail tolerates it).

Watch for **brownout** on tiny cells during the ~240 mA WiFi TX peak or a
+13 dBm LoRa burst (Phase 0 measures this). Add decoupling as needed.

## Bring-up checklist (Phase 0)

1. Reflash the C3 with this firmware (ELRS WiFi bootloader or UART).
2. Open serial @115200; confirm the banner prints.
3. If `begin_err=<code>` appears → SPI pins/wiring wrong → fix `board_config.h`.
4. With `radio_ok`: power a **second** node; confirm mutual `rx` lines + a
   sensible `pdr` in `status`.
5. Set the RF-switch table / TCXO for the XR2 if RX/TX is one-directional or
   weak.
6. Attach GNSS; confirm `gps_fix` lines and lat/lon populate.
7. Measure per-state current (sleep / LoRa-RX / LoRa-TX / GNSS) with a meter and
   record vs the estimates in `docs/hybrid-mesh/07-power-and-runtime.md`.

## Reference

- Design docs: [`docs/hybrid-mesh/`](../../../docs/hybrid-mesh/README.md)
- LR1121 driver reference: ExpressLRS firmware (open source) for this board
- RadioLib LR1121 API: `jgromes/RadioLib`
