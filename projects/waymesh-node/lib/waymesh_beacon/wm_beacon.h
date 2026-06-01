/*
 * waymesh_beacon — Waymesh v2 beacon wire codec + RX/relay decisions (doc 13
 * §3/§6). Portable C99; the header stays PLAINTEXT so the keyless Tier-3 relay
 * re-floods verbatim (crypto is end-to-end originator->gateway, §6). Host-tested
 * in test/test_beacon, including the exact byte offsets relays depend on.
 *
 * v2 header (12 B, always clear):
 *   0    magic     = 0x57
 *   1    version   = 2
 *   2    chanHash  (u8)        group filter (Meshtastic channel hash, §4)
 *   3    flags     (u8)        bit0 POS, bit1 ENCRYPTED, bit2 HAS-MIC
 *   4-7  srcNodeID (u32 LE)
 *   8-11 packetId  (u32 LE)    dedup MessageID + (phase 2) AEAD nonce material
 * POS payload (10 B, present when flags.POS; clear in phase 1):
 *   0-3 lat_i (i32 LE,1e-7) 4-7 lon_i (i32 LE) 8 sats 9 payload-flags(rsvd)
 * MIC tail (4 B, present when flags.HAS-MIC; phase 2): AEAD tag.
 *
 * Back-compat: v0 (8 B) / v1 (18 B) keep the as-built layout
 * (magic,version,srcId LE,seq u16 LE,[v1: lat,lon,sats,flags]) and are parsed as
 * the "open" default group (§3).
 */
#ifndef WAYMESH_BEACON_H
#define WAYMESH_BEACON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "waymesh_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WM_BEACON_MAGIC 0x57
#define WM_BEACON_V0 0
#define WM_BEACON_V1 1
#define WM_BEACON_V2 2

#define WM_BFLAG_POS       0x01
#define WM_BFLAG_ENCRYPTED 0x02
#define WM_BFLAG_HASMIC    0x04

#define WM_BEACON_V2_HDR  12
#define WM_BEACON_POS_LEN 10
#define WM_BEACON_MIC_LEN 4
#define WM_BEACON_V2_MAX  (WM_BEACON_V2_HDR + WM_BEACON_POS_LEN + WM_BEACON_MIC_LEN)

/* The Meshtastic default "open" channel hash (LongFast / psk={0x01}); see the
 * channel-hash vectors. v0/v1 beacons (no chanHash) are treated as this group. */
#define WM_OPEN_GROUP_HASH 8

typedef struct {
    uint8_t  version;
    uint8_t  chan_hash;   /* v2: byte 2; v0/v1: WM_OPEN_GROUP_HASH */
    uint8_t  flags;       /* normalized; bit0 POS for all versions */
    uint32_t src_id;
    uint32_t packet_id;   /* v2; v0/v1: the u16 seq, zero-extended */
    /* clear POS (when has_pos): */
    int32_t  lat_i, lon_i;
    uint8_t  sats;
    bool     has_pos;
    /* raw payload region (phase-2 AEAD): for v2, the bytes after the header,
     * excluding the MIC tail. NULL/0 for v0/v1. */
    const uint8_t *payload; size_t payload_len;
    const uint8_t *mic;     size_t mic_len;
} wm_beacon_t;

/* Parse a received frame. Returns 0 if it is a recognized Waymesh beacon
 * (magic ok, known version, length sufficient for that version), else -1.
 * Does NOT decrypt: when flags.ENCRYPTED, has_pos stays false and the caller
 * runs the AEAD-open (phase 2, step 4) over payload/mic. */
int wm_beacon_parse(const uint8_t *raw, size_t len, wm_beacon_t *out);

/* Build a clear (phase-1) v2 beacon into buf (>= WM_BEACON_V2_MAX). A presence
 * beacon (pos_valid=false) is the bare 12-byte header. Returns bytes written. */
size_t wm_beacon_build_v2_clear(uint8_t *buf, uint8_t chan_hash, uint32_t src_id,
                                uint32_t packet_id, bool pos_valid,
                                int32_t lat_i, int32_t lon_i, uint8_t sats);

/* Build an ENCRYPTED + MIC'd v2 POS beacon (flags = POS|ENCRYPTED|HASMIC, §5):
 * the 12-byte header stays CLEAR (relays/dedup read it keyless); the 10-byte POS
 * payload is AES-CCM sealed and a 4-byte AEAD tag (MIC) is appended -> 26 bytes.
 * The tag authenticates the clear header (AAD) + payload, so an outsider cannot
 * bit-flip a latitude undetected. key is the channel's expanded key (16 or 32 B,
 * §4); the nonce is derived deterministically from the clear header so any
 * key-holder reconstructs it. Writes to buf (>= WM_BEACON_V2_MAX); returns bytes
 * written (26), or 0 if key_len is not 16/32 (caller falls back to clear). */
size_t wm_beacon_build_v2_enc(uint8_t *buf, uint8_t chan_hash, uint32_t src_id,
                              uint32_t packet_id, int32_t lat_i, int32_t lon_i,
                              uint8_t sats, const uint8_t *key, size_t key_len);

/* AEAD-open a parsed ENCRYPTED beacon (§6 step 5): verify the MIC over the clear
 * header (AAD) and decrypt the POS payload under the channel key. On success
 * fills lat_i/lon_i/sats and sets has_pos. Returns 0 on success; -1 on a bad MIC
 * / wrong key / malformed shape (has_pos stays false and NO plaintext is exposed
 * -> the caller DROPs). Only meaningful when b->flags has ENCRYPTED set (parse
 * deliberately leaves has_pos=false for encrypted frames so a pre-open read
 * never trusts ciphertext as a position). */
int wm_beacon_open(wm_beacon_t *b, const uint8_t *key, size_t key_len);

typedef enum {
    WM_RX_ACCEPT = 0,            /* passes steps 1-3 (phase 2 then AEAD-opens) */
    WM_RX_DROP_SELF,             /* srcNodeID == self */
    WM_RX_DROP_FOREIGN_GROUP,    /* chanHash not in accepted set (step 3) */
} wm_rx_decision_t;

/* Acceptance steps 2-3 (§6); step 1 = a successful parse. (Step 4 dedup and
 * step 5 AEAD-open are caller/phase-2 concerns.) */
wm_rx_decision_t wm_beacon_accept(const wm_beacon_t *b, uint32_t self_id,
                                  const wm_config_t *cfg);

/* Managed-flood relay policy (§6): relay-all -> always; relay-known -> only
 * configured chanHashes. The relay re-floods raw bytes verbatim regardless. */
bool wm_beacon_should_relay(const wm_beacon_t *b, const wm_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* WAYMESH_BEACON_H */
