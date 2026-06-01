/*
 * waymesh_crypto — portable crypto core for the Waymesh v2 beacon.
 *
 * Pure C99, no Arduino / RTOS / platform dependencies, so it compiles
 * byte-identically on the host (pio test -e native), the ESP32-C3 (XR2)
 * and the ESP8285 originator. See docs/hybrid-mesh/13-auth-and-groups.md.
 *
 * Two responsibilities:
 *   1. Meshtastic-compatible channel identity (§4): the 1-byte channel hash
 *      and the PSK / default-key expansion. These MUST byte-match upstream
 *      Meshtastic (validated in test/test_channel_hash against vectors
 *      generated from meshtastic/firmware @ v2.6.4.b89355f).
 *   2. Our own OTA payload AEAD (§5): AES-CCM (confidentiality + a 4-byte
 *      integrity tag). This never reaches the Meshtastic app, so it is ours
 *      to choose; validated in test/test_ccm against RFC 3610 + a pyca
 *      AES-CCM vector at our exact wire params.
 *
 * Upstream pin (record kept in sync with WAYMESH_FW_VERSION in ble_gatt.cpp):
 *   meshtastic/firmware  tag v2.6.4.b89355f  (b89355ffa60b3893417004b07e7b96f04b17022c)
 *   meshtastic/protobufs tag v2.6.4          (f00e96f12da48abfa9a992f8b5546fd75a370250)
 */
#ifndef WAYMESH_CRYPTO_H
#define WAYMESH_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Channel identity (Meshtastic-compatible) ----------------------------- */

/* The famous Meshtastic default channel key (AES-128). A 1-byte PSK index of 1
 * expands to exactly this; index N adds (N-1) to the last byte. Exposed for
 * tests / the "open" default group. */
extern const uint8_t WM_DEFAULT_PSK[16];

/* Max expanded key length (AES-256). */
#define WM_MAX_KEY_LEN 32

/* Expand a stored PSK into the AES key actually used, the Meshtastic way:
 *   - len 0           : encryption disabled            -> *out_len = 0, returns 0
 *   - len 1, byte 0   : encryption disabled            -> *out_len = 0, returns 0
 *   - len 1, byte N>0 : WM_DEFAULT_PSK with last += N-1 -> 16 bytes
 *   - len 16 / 32     : used directly                  -> 16 / 32 bytes
 *   - 1<len<16        : zero-padded up to 16           -> 16 bytes
 *   - 16<len<32       : zero-padded up to 32           -> 32 bytes
 * Note: a length-0 SECONDARY channel inheriting the PRIMARY key (upstream
 * getKey recursion) is a channel-store concern, not handled here.
 * out must hold WM_MAX_KEY_LEN bytes. Returns 0 on success, -1 on bad args. */
int wm_expand_psk(const uint8_t *psk, size_t psk_len,
                  uint8_t *out, size_t *out_len);

/* 1-byte channel hash over an explicit (non-empty) channel name and an already
 * expanded key:  xorHash(name) ^ xorHash(expanded_key).
 * Returns 0..255, or -1 if name is empty / key_len is 0 (no usable channel). */
int wm_chan_hash_expanded(const char *name,
                          const uint8_t *expanded_key, size_t key_len);

/* Convenience: expand psk then hash. Returns 0..255 or -1. */
int wm_chan_hash(const char *name, const uint8_t *psk, size_t psk_len);

/* --- AES-CCM AEAD (our OTA payload cipher) -------------------------------- */
/*
 * General AES-CCM (NIST SP 800-38C / RFC 3610). The Waymesh v2 wire layout
 * (nonce assembly from chanHash/srcNodeID/packetId, AAD = header[1..]) lives in
 * the beacon code; this primitive stays generic so it can be KAT-validated.
 *
 *   key_len  : 16 (AES-128) or 32 (AES-256)
 *   nonce_len: 7..13   (Waymesh uses 13 -> L=2)
 *   tag_len  : 4,6,8,10,12,14,16 (Waymesh uses 4)
 * Returns 0 on success, -1 on invalid parameters, -2 (open only) on auth fail.
 */
int wm_ccm_seal(const uint8_t *key, size_t key_len,
                const uint8_t *nonce, size_t nonce_len,
                const uint8_t *aad, size_t aad_len,
                const uint8_t *pt, size_t pt_len,
                uint8_t *ct_out, uint8_t *tag_out, size_t tag_len);

int wm_ccm_open(const uint8_t *key, size_t key_len,
                const uint8_t *nonce, size_t nonce_len,
                const uint8_t *aad, size_t aad_len,
                const uint8_t *ct, size_t ct_len,
                const uint8_t *tag, size_t tag_len,
                uint8_t *pt_out);

#ifdef __cplusplus
}
#endif

#endif /* WAYMESH_CRYPTO_H */
