#pragma once
// =============================================================================
// Board configuration for the RadioMaster XR2 Nano (ESP32-C3 + Semtech LR1121)
// Phase 0 bring-up. See docs/hybrid-mesh/09-poc-roadmap.md.
//
// Pin map VERIFIED against the open-source ExpressLRS hardware target for the
// RadioMaster XR2 (the board ships with ELRS, so this is ground truth):
//   ExpressLRS/targets  RX/"Generic C3 LR1121.json"  (base layout)
//   ExpressLRS/targets  targets.json  ->  "xr2" overlay (serial1, led, +13 dBm)
// This completes Phase 0 task #1 ("confirm the ELRS LR1121 pin map/SPI on XR2").
//
// Still bench-verify RF behaviour: the LR1121 RF-switch routing and regulator
// mode (radio_dcdc=true on the XR2) are board defaults here and should be
// confirmed against live TX/RX before trusting range numbers. SPI/control pins
// below are the verified values, so begin() should now succeed.
// =============================================================================

// ---- LR1121 SPI + control pins (ELRS "Generic C3 LR1121" base layout) -------
#ifndef PIN_LORA_SCK
#define PIN_LORA_SCK 6    // radio_sck
#endif
#ifndef PIN_LORA_MISO
#define PIN_LORA_MISO 5   // radio_miso
#endif
#ifndef PIN_LORA_MOSI
#define PIN_LORA_MOSI 4   // radio_mosi
#endif
#ifndef PIN_LORA_NSS
#define PIN_LORA_NSS 7    // radio_nss (chip select)
#endif
#ifndef PIN_LORA_BUSY
#define PIN_LORA_BUSY 3   // radio_busy
#endif
#ifndef PIN_LORA_DIO1
#define PIN_LORA_DIO1 1   // radio_dio1 (IRQ)
#endif
#ifndef PIN_LORA_RST
#define PIN_LORA_RST 2    // radio_rst
#endif

// ---- GNSS UART (XR2 "serial1" overlay = the spare UART; GY-GPS6MV2 here) -----
// XR2 overlay: serial1_rx=18, serial1_tx=19. Using serial1 for GNSS gives up the
// primary CRSF UART (GPIO20/21) — fine for a standalone mesh node. The primary
// UART is also how we reflash/log on the bench (C3 UART0 = the FTDI port).
#ifndef PIN_GNSS_RX
#define PIN_GNSS_RX 18    // C3 RX  <- GNSS TX
#endif
#ifndef PIN_GNSS_TX
#define PIN_GNSS_TX 19    // C3 TX  -> GNSS RX
#endif
#ifndef GNSS_BAUD
#define GNSS_BAUD 9600    // GY-GPS6MV2 (u-blox NEO-6M) NMEA default
#endif

// ---- Status LED (XR2 overlay: led=8, led_rgb=-1 -> simple GPIO LED on 8) -----
#ifndef PIN_LED
#define PIN_LED 8
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0  // simple LED; polarity unconfirmed, flip to 1 if dark
#endif

// ---- LR1121 2.4 GHz LoRa radio configuration --------------------------------
// 2.4 GHz only on the XR2 (no sub-GHz RF path). Tune SF/BW for the
// range/airtime/sensitivity tradeoff (see docs/hybrid-mesh/05-protocol.md).
#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ 2450.0f   // 2.400-2.479 GHz band
#endif
#ifndef LORA_BW_KHZ
#define LORA_BW_KHZ 812.5f      // 2.4 GHz LoRa BW options: 203.125/406.25/812.5/1625
#endif
#ifndef LORA_SF
#define LORA_SF 9               // SF5..SF12
#endif
#ifndef LORA_CR
#define LORA_CR 5               // coding rate 4/5..4/8 -> 5..8
#endif
#ifndef LORA_POWER_DBM
#define LORA_POWER_DBM 10       // XR2 stock telemetry power; overlay allows +13
#endif
#ifndef LORA_PREAMBLE
#define LORA_PREAMBLE 8
#endif
// XR2 base layout declares no TCXO control pin -> the LR1121 runs off the board
// XTAL, so no radio-driven TCXO voltage is needed (confirmed, not a guess).
// radio_dcdc=true: once bring-up is stable, enable the LR1121 DCDC regulator via
// RadioLib setRegulatorDCDC() to match the power model in 07-power-and-runtime.
#ifndef LORA_TCXO_V
#define LORA_TCXO_V 0.0f        // no DIO-controlled TCXO on the XR2
#endif

// ---- Phase 0 behavior -------------------------------------------------------
#ifndef BEACON_PERIOD_MS
#define BEACON_PERIOD_MS 2000   // how often this node transmits a loopback beacon
#endif
#ifndef STATUS_PERIOD_MS
#define STATUS_PERIOD_MS 5000   // how often to print a rolling status/PDR line
#endif
