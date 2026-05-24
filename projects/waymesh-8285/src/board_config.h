#pragma once
// =============================================================================
// Board configuration — ESP8285 + Semtech SX1280, "BayckRC 7PWM" class
// (ExpressLRS hardware target "Generic 2400 PWMP7"). Waymesh Tier 2/3, PoC #0.
//
// Pin map derived from the ExpressLRS targets repo, RX/"Generic 2400 PWMP7.json"
// (no dedicated BayckRC target exists upstream; the 7-PWM ESP8285/SX1280 RX
// builds against this generic layout). SPI pins are the ESP8266 fixed HW-SPI
// pins, which the PWMP7 layout matches exactly.
// =============================================================================

#include <RadioLib.h>  // for RADIOLIB_NC

// ---- SX1280 SPI + control pins ---------------------------------------------
// ESP8266/ESP8285 hardware SPI is on FIXED pins: SCK=GPIO14, MISO=GPIO12,
// MOSI=GPIO13, HW-CS=GPIO15. SPI.begin() takes no pin args. PWMP7 puts
// radio_sck/miso/mosi/nss on exactly those pins, so no remap is needed.
//   (GPIO15 is a boot strap that must be LOW at reset; SPI CS idles HIGH after
//    boot — standard for ELRS 8285 SX1280 boards, the board has the strap R.)
#ifndef PIN_LORA_NSS
#define PIN_LORA_NSS 15   // radio_nss (hardware CS)
#endif
#ifndef PIN_LORA_DIO1
#define PIN_LORA_DIO1 4   // radio_dio1 (IRQ)
#endif

// BUSY / RST: the PWMP7 generic target SACRIFICES these GPIOs to free a 6th/7th
// PWM output, so it declares no MCU BUSY/RST. PoC #0 uses NO PWM, so we instead
// wire them to the SX1280 using the PWMP5/PWMP6 canonical assignment (busy=5,
// rst=2) — this gives RadioLib a real BUSY line, which is the reliable path.
//
// EMPIRICAL: if begin() returns a timeout (-707 / SPI_CMD_TIMEOUT), the physical
// board likely follows the PWMP7 "no BUSY/RST" wiring (GPIO5/GPIO2 routed to
// servo headers, not the radio). In that case set both to RADIOLIB_NC and
// rebuild — RadioLib SX128x falls back to timed waits without a BUSY pin.
//   (GPIO2 is a boot strap that must be HIGH at reset; SX1280 RST idles HIGH, OK.)
#ifndef PIN_LORA_BUSY
#define PIN_LORA_BUSY 5   // radio_busy   (set to RADIOLIB_NC if PWMP7 wiring)
#endif
#ifndef PIN_LORA_RST
#define PIN_LORA_RST 2    // radio_rst    (set to RADIOLIB_NC if PWMP7 wiring)
#endif

// ---- Status LED (PWMP7 led=16) ----------------------------------------------
#ifndef PIN_LED
#define PIN_LED 16
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0  // polarity unconfirmed on this board; flip if dark
#endif

// ---- SX1280 2.4 GHz LoRa radio configuration --------------------------------
// THESE MUST MATCH THE XR2 (waymesh-node board_config.h) FOR INTEROP (PoC #0):
//   freq 2450 MHz, BW 812.5 kHz, SF9, CR 4/5, preamble 8, sync PRIVATE.
#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ 2450.0f
#endif
#ifndef LORA_BW_KHZ
#define LORA_BW_KHZ 812.5f      // SX1280 BW set: 203.125 / 406.25 / 812.5 / 1625
#endif
#ifndef LORA_SF
#define LORA_SF 9               // SF5..SF12
#endif
#ifndef LORA_CR
#define LORA_CR 5               // 4/5..4/8 -> 5..8. STANDARD interleave only:
                                // the LR1121 lacks the SX1280 long-interleave CR
                                // variants, so we force longInterleave=false.
#endif
#ifndef LORA_POWER_DBM
#define LORA_POWER_DBM 10       // match XR2 stock telemetry power
#endif
#ifndef LORA_PREAMBLE
#define LORA_PREAMBLE 8
#endif
// Sync word: LR1121 used RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE (0x12). SX128x
// begin() takes NO sync-word arg, so we set it explicitly. Whether 0x12 produces
// the SAME on-air sync across the two chip families is THE interop question PoC
// #0 answers — keep this tunable and sweep on-air if no frames are heard.
#ifndef LORA_SYNC_WORD
#define LORA_SYNC_WORD 0x12
#endif

// ---- Behavior (match XR2 cadence) -------------------------------------------
#ifndef BEACON_PERIOD_MS
#define BEACON_PERIOD_MS 2000
#endif
#ifndef STATUS_PERIOD_MS
#define STATUS_PERIOD_MS 5000
#endif

// ---- GPS (Tier 2, -DWAYMESH_GPS=1) ------------------------------------------
// The ESP8285 has no spare UART (UART1 is TX-only and its pin is the SX1280 RST;
// Serial.swap()'s alternate pins are the SX1280 SPI), so UART0 (GPIO1 TX /
// GPIO3 RX) is TIME-SHARED: it boots as the debug console for GPS_GRACE_MS, then
// listens GPS_PROBE_MS for valid NMEA. If a GPS is talking it locks GPS mode
// (and, in the production build, goes silent — the line is the GPS's); otherwise
// it reverts to the debug console. The firmware is VENDOR-NEUTRAL: it parses
// standard NMEA (TinyGPSPlus) from any module and emits NO config bytes. u-blox
// modules are tuned once, out-of-band, via tools/gps_provision.py (which itself
// confirms the model before writing). GPS_BAUD defaults to the debug baud so the
// switch is software-only (no Serial.begin() baud change).
#if WAYMESH_GPS
#ifndef GPS_GRACE_MS
#define GPS_GRACE_MS 25000      // debug-console window before GPS auto-detect
#endif
#ifndef GPS_PROBE_MS
#define GPS_PROBE_MS 4000       // listen window for valid NMEA after the grace
#endif
#ifndef GPS_BAUD
#define GPS_BAUD 115200         // == debug baud -> software-only switch
#endif
#ifndef GPS_PROBE_MIN_SENTENCES
#define GPS_PROBE_MIN_SENTENCES 2  // checksum-valid NMEA sentences => GPS present
#endif
#ifndef GPS_FIX_MAX_AGE_MS
#define GPS_FIX_MAX_AGE_MS 5000 // beacon a position only if the fix is fresher
#endif
#endif  // WAYMESH_GPS

// ---- Managed-flood relay (Tier 2/3) -----------------------------------------
// Compiled ONLY when WAYMESH_RELAY is set in the env build_flags. Re-floods
// unseen beacons VERBATIM (same srcId/seq, so the gateway still attributes
// presence to the true originator) with: a seen-set for dedup, an
// SNR-proportional rebroadcast delay (weaker-SNR receivers wait longer so the
// best-placed relay goes first), and overhear suppression (cancel a pending
// rebroadcast if the same MessageID is heard again). See
// docs/hybrid-mesh/05-protocol.md §"Suppression & dedup state".
//
// NO hop-limit: the as-built beacon (v0/v1) carries no hop field, so the
// seen-set alone bounds the flood — each node relays each MessageID = (srcId,
// seq) at most once, and a node ignores its own srcId, so the flood terminates.
// A real decrementing hop-limit arrives with the LRP-header migration (05 §LRP).
#if WAYMESH_RELAY
#ifndef RELAY_SEEN_SET_SIZE
#define RELAY_SEEN_SET_SIZE 32      // recent MessageIDs remembered (ring buffer)
#endif
#ifndef RELAY_PENDING_SLOTS
#define RELAY_PENDING_SLOTS 4       // rebroadcasts that can be in flight at once
#endif
#ifndef RELAY_DELAY_BASE_MS
#define RELAY_DELAY_BASE_MS 20      // floor wait before a rebroadcast
#endif
#ifndef RELAY_DELAY_PER_DB_MS
#define RELAY_DELAY_PER_DB_MS 12    // extra wait per dB below the SNR reference
#endif
#ifndef RELAY_SNR_REF_DB
#define RELAY_SNR_REF_DB 12.0f      // SNR at/above which delay == base
#endif
#ifndef RELAY_DELAY_MAX_MS
#define RELAY_DELAY_MAX_MS 250      // cap (also the suppression listen window)
#endif
#ifndef RELAY_DELAY_JITTER_MS
#define RELAY_DELAY_JITTER_MS 15    // tie-break jitter added to each delay
#endif
#endif  // WAYMESH_RELAY
