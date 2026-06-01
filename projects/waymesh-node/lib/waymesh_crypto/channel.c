/* Meshtastic-compatible channel identity (doc 13 §4).
 *
 * The 1-byte channel hash and the PSK / default-key expansion MUST byte-match
 * upstream Meshtastic so a channel created in the app maps 1:1 to a Waymesh
 * group. Independent implementation from the documented algorithm
 * (meshtastic/firmware src/mesh/Channels.cpp: xorHash / generateHash / getKey);
 * validated in test/test_channel_hash against vectors generated from upstream
 * at the pinned ref. */
#include "waymesh_crypto.h"

#include <string.h>

/* meshtastic/firmware Channels.h defaultpsk[] (AES-128). Index 1 expands to
 * exactly this; index N adds (N-1) to the last byte. */
const uint8_t WM_DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

/* xorHash: XOR of all bytes (single byte). */
static uint8_t xor_hash(const uint8_t *p, size_t len)
{
    uint8_t code = 0;
    for (size_t i = 0; i < len; i++) code ^= p[i];
    return code;
}

int wm_expand_psk(const uint8_t *psk, size_t psk_len,
                  uint8_t *out, size_t *out_len)
{
    if (out == NULL || out_len == NULL) return -1;
    if (psk_len > 0 && psk == NULL) return -1;

    memset(out, 0, WM_MAX_KEY_LEN);

    if (psk_len == 0) {            /* encryption disabled */
        *out_len = 0;
        return 0;
    }
    if (psk_len == 1) {            /* short single-byte index */
        uint8_t idx = psk[0];
        if (idx == 0) {            /* explicit "off" */
            *out_len = 0;
            return 0;
        }
        memcpy(out, WM_DEFAULT_PSK, sizeof(WM_DEFAULT_PSK));
        out[sizeof(WM_DEFAULT_PSK) - 1] =
            (uint8_t)(out[sizeof(WM_DEFAULT_PSK) - 1] + idx - 1);
        *out_len = sizeof(WM_DEFAULT_PSK);
        return 0;
    }
    if (psk_len <= 16) {           /* 2..16: used / zero-padded to AES-128 */
        memcpy(out, psk, psk_len);
        *out_len = 16;
        return 0;
    }
    if (psk_len <= 32) {           /* 17..32: used / zero-padded to AES-256 */
        memcpy(out, psk, psk_len);
        *out_len = 32;
        return 0;
    }
    return -1;                     /* oversized */
}

int wm_chan_hash_expanded(const char *name,
                          const uint8_t *expanded_key, size_t key_len)
{
    if (name == NULL || name[0] == '\0') return -1; /* explicit names only */
    if (key_len == 0) return -1;                    /* no usable channel */
    uint8_t h = xor_hash((const uint8_t *)name, strlen(name));
    h ^= xor_hash(expanded_key, key_len);
    return (int)h;
}

int wm_chan_hash(const char *name, const uint8_t *psk, size_t psk_len)
{
    uint8_t key[WM_MAX_KEY_LEN];
    size_t klen = 0;
    if (wm_expand_psk(psk, psk_len, key, &klen) != 0) return -1;
    return wm_chan_hash_expanded(name, key, klen);
}
