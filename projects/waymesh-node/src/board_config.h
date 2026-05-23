#pragma once
// =============================================================================
// Board configuration for the RadioMaster XR2 Nano (ESP32-C3 + Semtech LR1121)
// Phase 0 bring-up. See docs/hybrid-mesh/09-poc-roadmap.md.
//
// !!! EVERY VALUE BELOW IS A PLACEHOLDER UNTIL VERIFIED ON HARDWARE. !!!
// The pin map and RF-switch/TCXO config are board-specific and are NOT yet
// confirmed for the XR2. Phase 0, task #1 is literally "confirm the ELRS LR1121
// pin map/SPI on the XR2". Extract the real values from the open-source
// ExpressLRS hardware target for the RadioMaster XR2 before flashing:
//   https://github.com/ExpressLRS/targets   (RX layout JSON for the XR2)
// Map the ELRS layout fields to the defines here:
//   radio_nss   -> PIN_LORA_NSS      radio_busy -> PIN_LORA_BUSY
//   radio_dio1  -> PIN_LORA_DIO1     radio_rst  -> PIN_LORA_RST
//   radio_sck   -> PIN_LORA_SCK      radio_miso -> PIN_LORA_MISO
//   radio_mosi  -> PIN_LORA_MOSI
// Wrong pins = the radio simply will not respond (begin() returns an error).
// =============================================================================

// ---- LR1121 SPI + control pins (VERIFY against ELRS XR2 target) -------------
#ifndef PIN_LORA_SCK
#define PIN_LORA_SCK 4    // PLACEHOLDER
#endif
#ifndef PIN_LORA_MISO
#define PIN_LORA_MISO 5   // PLACEHOLDER
#endif
#ifndef PIN_LORA_MOSI
#define PIN_LORA_MOSI 6   // PLACEHOLDER
#endif
#ifndef PIN_LORA_NSS
#define PIN_LORA_NSS 7    // PLACEHOLDER (chip select)
#endif
#ifndef PIN_LORA_BUSY
#define PIN_LORA_BUSY 2   // PLACEHOLDER
#endif
#ifndef PIN_LORA_DIO1
#define PIN_LORA_DIO1 3   // PLACEHOLDER (IRQ)
#endif
#ifndef PIN_LORA_RST
#define PIN_LORA_RST 10   // PLACEHOLDER
#endif

// ---- GNSS UART (kept in early phases, on the XR2 spare UART) -----------------
// Using the spare UART for GNSS typically means giving up the CRSF interface;
// acceptable for a standalone mesh node. VERIFY pins against the XR2 pads.
#ifndef PIN_GNSS_RX
#define PIN_GNSS_RX 20    // PLACEHOLDER (C3 RX  <- GNSS TX)
#endif
#ifndef PIN_GNSS_TX
#define PIN_GNSS_TX 21    // PLACEHOLDER (C3 TX  -> GNSS RX)
#endif
#ifndef GNSS_BAUD
#define GNSS_BAUD 9600    // u-blox MAX-M10S default; adjust if reconfigured
#endif

// ---- Status LED (VERIFY; XR2 has an LED, GPIO unknown here) ------------------
#ifndef PIN_LED
#define PIN_LED 8         // PLACEHOLDER
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0  // set to 1 if the XR2 LED is active-low
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
#define LORA_POWER_DBM 10       // XR2 ships ~10 dBm; LR1121 2.4 GHz max +13 dBm
#endif
#ifndef LORA_PREAMBLE
#define LORA_PREAMBLE 8
#endif
// LR1121 needs a board-correct RF-switch (DIO) table and TCXO voltage to route
// TX/RX. These are board-specific and MUST come from the XR2 design. Left at
// RadioLib defaults here; the radio may TX/RX incorrectly until set on the bench.
#ifndef LORA_TCXO_V
#define LORA_TCXO_V 0.0f        // 0 = no TCXO control via radio (VERIFY for XR2)
#endif

// ---- Phase 0 behavior -------------------------------------------------------
#ifndef BEACON_PERIOD_MS
#define BEACON_PERIOD_MS 2000   // how often this node transmits a loopback beacon
#endif
#ifndef STATUS_PERIOD_MS
#define STATUS_PERIOD_MS 5000   // how often to print a rolling status/PDR line
#endif
