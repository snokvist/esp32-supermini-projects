/* Meshtastic channel AES-CTR (byte-compatible with the stock app's channel
 * cipher, doc 13 §7.3 / constraint c). Built on the FIPS-197 forward block
 * cipher only (CTR needs no AES decrypt). Locked against meshtastic/firmware
 * v2.6.4.b89355f CryptoEngine::encryptAESCtr + rweather CTR setCounterSize(4).
 * KAT-validated in test/test_ccm (test_meshtastic_ctr_*) against pyca-computed
 * vectors at the exact nonce layout. */
#include "waymesh_crypto.h"

#include <string.h>

#include "aes.h"

static void mt_wr_u64le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void mt_wr_u32le(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

int wm_meshtastic_ctr(const uint8_t *key, size_t key_len,
                      uint32_t from_node, uint64_t packet_id,
                      const uint8_t *in, size_t len, uint8_t *out)
{
    if (key_len != 16 && key_len != 32) return -1;

    wm_aes_ctx ctx;
    if (wm_aes_init(&ctx, key, key_len) != 0) return -1;

    /* 16-byte initial counter block = the Meshtastic nonce (§7.3): packetId
     * (u64 LE) | fromNode (u32 LE) | 4 zero counter bytes. */
    uint8_t ctr[WM_AES_BLOCK];
    memset(ctr, 0, sizeof(ctr));
    mt_wr_u64le(ctr + 0, packet_id);
    mt_wr_u32le(ctr + 8, from_node);

    uint8_t ks[WM_AES_BLOCK];
    size_t off = 0;
    while (off < len) {
        wm_aes_encrypt_block(&ctx, ctr, ks);
        size_t n = (len - off < WM_AES_BLOCK) ? (len - off) : WM_AES_BLOCK;
        for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
        off += n;
        /* increment the last 4 bytes big-endian (rweather setCounterSize(4)). */
        for (int i = WM_AES_BLOCK - 1; i >= WM_AES_BLOCK - 4; i--)
            if (++ctr[i] != 0) break;
    }
    /* Wipe the round-key schedule + keystream/counter scratch. */
    wm_secure_zero(&ctx, sizeof ctx);
    wm_secure_zero(ks, sizeof ks);
    wm_secure_zero(ctr, sizeof ctr);
    return 0;
}
