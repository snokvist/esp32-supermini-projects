/* AES-CCM AEAD (NIST SP 800-38C / RFC 3610), built on the forward AES block
 * cipher (CTR + CBC-MAC). This is Waymesh's OWN OTA payload cipher (§5); it
 * never reaches the Meshtastic app. KAT-validated in test/test_ccm against
 * RFC 3610 and a pyca AES-CCM vector at our wire params (nonce 13, tag 4). */
#include "waymesh_crypto.h"
#include "aes.h"

#include <string.h>

static void xor_bytes(uint8_t *dst, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) dst[i] ^= src[i];
}

/* Write a big-endian integer of `len` bytes into out. */
static void be_store(uint8_t *out, size_t len, uint64_t v)
{
    for (size_t i = 0; i < len; i++)
        out[len - 1 - i] = (uint8_t)(v >> (8 * i));
}

/* Validate params shared by seal/open. Returns L (15-nonce_len) or -1.
 * Rejecting aad_len here (not just in a comment) keeps seal and open in lockstep
 * so they can never disagree on what is encodable. */
static int ccm_validate(size_t key_len, size_t nonce_len, size_t tag_len,
                        size_t aad_len, size_t msg_len)
{
    if (key_len != 16 && key_len != 32) return -1;
    if (nonce_len < 7 || nonce_len > 13) return -1;
    if (tag_len < 4 || tag_len > 16 || (tag_len & 1)) return -1;
    /* We only emit the 2-byte AAD length prefix (RFC 3610 §2.2 / SP 800-38C):
     * valid for 0 < l(a) < 2^16 - 2^8. Larger AAD would need the 0xFFFE/0xFFFF
     * markers we don't implement, so reject it rather than mis-MAC silently. */
    if (aad_len >= 0xFF00) return -1;
    int L = (int)(15 - nonce_len);
    /* message length must fit in L bytes */
    if (L < 8 && msg_len >= ((uint64_t)1 << (8 * L))) return -1;
    return L;
}

/* CBC-MAC over B0 || formatted-AAD || padded-message, returns full 16-byte T. */
static void ccm_cbc_mac(const wm_aes_ctx *ctx, int L,
                        const uint8_t *nonce, size_t nonce_len,
                        const uint8_t *aad, size_t aad_len,
                        const uint8_t *msg, size_t msg_len,
                        size_t tag_len, uint8_t T[16])
{
    uint8_t b[16];
    uint8_t x[16];
    memset(x, 0, 16);

    /* B0 = flags || nonce || l(m) */
    b[0] = (uint8_t)((aad_len ? 0x40 : 0x00) |
                     ((((tag_len - 2) / 2) & 0x07) << 3) |
                     ((L - 1) & 0x07));
    memcpy(b + 1, nonce, nonce_len);
    be_store(b + 1 + nonce_len, (size_t)L, (uint64_t)msg_len);
    xor_bytes(x, b, 16);
    wm_aes_encrypt_block(ctx, x, x);

    /* Associated data, length-prefixed, zero-padded to the block. We only
     * support 0 < l(a) < 2^16 - 2^8 (two-byte length) — ample for our 11-byte
     * header AAD and RFC 3610's 8-byte headers. */
    if (aad_len > 0) {
        size_t pos = 0;
        memset(b, 0, 16);
        b[0] = (uint8_t)(aad_len >> 8);
        b[1] = (uint8_t)(aad_len & 0xff);
        size_t take = aad_len < 14 ? aad_len : 14;
        memcpy(b + 2, aad, take);
        xor_bytes(x, b, 16);
        wm_aes_encrypt_block(ctx, x, x);
        pos = take;
        while (pos < aad_len) {
            size_t n = aad_len - pos < 16 ? aad_len - pos : 16;
            memset(b, 0, 16);
            memcpy(b, aad + pos, n);
            xor_bytes(x, b, 16);
            wm_aes_encrypt_block(ctx, x, x);
            pos += n;
        }
    }

    /* Payload blocks, zero-padded. */
    {
        size_t pos = 0;
        while (pos < msg_len) {
            size_t n = msg_len - pos < 16 ? msg_len - pos : 16;
            memset(b, 0, 16);
            memcpy(b, msg + pos, n);
            xor_bytes(x, b, 16);
            wm_aes_encrypt_block(ctx, x, x);
            pos += n;
        }
    }

    memcpy(T, x, 16);
}

/* Build counter block A_i. */
static void ccm_ctr_block(uint8_t a[16], int L,
                          const uint8_t *nonce, size_t nonce_len, uint64_t ctr)
{
    a[0] = (uint8_t)((L - 1) & 0x07);
    memcpy(a + 1, nonce, nonce_len);
    be_store(a + 1 + nonce_len, (size_t)L, ctr);
}

/* CTR-crypt msg (in==out allowed). Returns S0 (keystream for the tag) in s0. */
static void ccm_ctr_crypt(const wm_aes_ctx *ctx, int L,
                          const uint8_t *nonce, size_t nonce_len,
                          const uint8_t *in, uint8_t *out, size_t len,
                          uint8_t s0[16])
{
    uint8_t a[16], s[16];
    ccm_ctr_block(a, L, nonce, nonce_len, 0);
    wm_aes_encrypt_block(ctx, a, s0);

    uint64_t ctr = 1;
    size_t pos = 0;
    while (pos < len) {
        ccm_ctr_block(a, L, nonce, nonce_len, ctr);
        wm_aes_encrypt_block(ctx, a, s);
        size_t n = len - pos < 16 ? len - pos : 16;
        for (size_t i = 0; i < n; i++) out[pos + i] = in[pos + i] ^ s[i];
        pos += n;
        ctr++;
    }
}

int wm_ccm_seal(const uint8_t *key, size_t key_len,
                const uint8_t *nonce, size_t nonce_len,
                const uint8_t *aad, size_t aad_len,
                const uint8_t *pt, size_t pt_len,
                uint8_t *ct_out, uint8_t *tag_out, size_t tag_len)
{
    int L = ccm_validate(key_len, nonce_len, tag_len, aad_len, pt_len);
    if (L < 0) return -1;

    wm_aes_ctx ctx;
    if (wm_aes_init(&ctx, key, key_len) != 0) return -1;

    uint8_t T[16], s0[16];
    ccm_cbc_mac(&ctx, L, nonce, nonce_len, aad, aad_len, pt, pt_len, tag_len, T);
    ccm_ctr_crypt(&ctx, L, nonce, nonce_len, pt, ct_out, pt_len, s0);

    /* Encrypted MAC: U = T xor S0[0..tag_len-1] */
    for (size_t i = 0; i < tag_len; i++) tag_out[i] = T[i] ^ s0[i];

    /* Wipe the round-key schedule + MAC/keystream scratch (captured-RAM hygiene). */
    wm_secure_zero(&ctx, sizeof ctx);
    wm_secure_zero(T, sizeof T);
    wm_secure_zero(s0, sizeof s0);
    return 0;
}

int wm_ccm_open(const uint8_t *key, size_t key_len,
                const uint8_t *nonce, size_t nonce_len,
                const uint8_t *aad, size_t aad_len,
                const uint8_t *ct, size_t ct_len,
                const uint8_t *tag, size_t tag_len,
                uint8_t *pt_out)
{
    int L = ccm_validate(key_len, nonce_len, tag_len, aad_len, ct_len);
    if (L < 0) return -1;

    wm_aes_ctx ctx;
    if (wm_aes_init(&ctx, key, key_len) != 0) return -1;

    uint8_t s0[16], T[16];
    /* CTR-decrypt first so we have the plaintext for the MAC recomputation. */
    ccm_ctr_crypt(&ctx, L, nonce, nonce_len, ct, pt_out, ct_len, s0);
    ccm_cbc_mac(&ctx, L, nonce, nonce_len, aad, aad_len, pt_out, ct_len, tag_len, T);

    /* Recompute expected tag and compare in constant time. */
    uint8_t diff = 0;
    for (size_t i = 0; i < tag_len; i++)
        diff |= (uint8_t)((T[i] ^ s0[i]) ^ tag[i]);
    int rc = 0;
    if (diff != 0) {
        memset(pt_out, 0, ct_len); /* no plaintext leak on auth failure */
        rc = -2;
    }
    /* Wipe the round-key schedule + MAC/keystream scratch before returning. */
    wm_secure_zero(&ctx, sizeof ctx);
    wm_secure_zero(T, sizeof T);
    wm_secure_zero(s0, sizeof s0);
    return rc;
}
