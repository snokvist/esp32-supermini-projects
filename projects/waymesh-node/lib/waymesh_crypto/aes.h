/*
 * Minimal AES block cipher (FIPS-197), encrypt-only, runtime key length.
 *
 * CCM (the only consumer here) is built entirely on the forward block cipher
 * for both confidentiality (CTR) and integrity (CBC-MAC), so AES decryption is
 * never needed and is deliberately omitted. Supports 128- and 256-bit keys
 * selected at runtime (a node may hold channels of either key length).
 *
 * Own implementation from the FIPS-197 spec (no third-party code); validated
 * against the FIPS-197 Appendix C known-answer vectors in test/test_ccm.
 */
#ifndef WAYMESH_AES_H
#define WAYMESH_AES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WM_AES_BLOCK 16
#define WM_AES_MAX_ROUNDS 14
/* round-key schedule: (Nr+1) * 16 bytes, max for AES-256 = 15*16 = 240 */
#define WM_AES_MAX_RK (WM_AES_MAX_ROUNDS + 1) * WM_AES_BLOCK

typedef struct {
    uint8_t rk[WM_AES_MAX_RK];
    int rounds; /* 10 for AES-128, 14 for AES-256 */
} wm_aes_ctx;

/* key_len must be 16 or 32. Returns 0 on success, -1 on bad key_len. */
int wm_aes_init(wm_aes_ctx *ctx, const uint8_t *key, size_t key_len);

/* Encrypt one 16-byte block. in and out may alias. */
void wm_aes_encrypt_block(const wm_aes_ctx *ctx,
                          const uint8_t in[WM_AES_BLOCK],
                          uint8_t out[WM_AES_BLOCK]);

#ifdef __cplusplus
}
#endif

#endif /* WAYMESH_AES_H */
