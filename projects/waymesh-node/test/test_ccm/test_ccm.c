/* Host unit test: AES block cipher + AES-CCM AEAD (our OTA payload cipher).
 * Vectors (tools/gen_ccm_vectors.py): AES from FIPS-197, CCM from RFC 3610 +
 * Waymesh-wire params (nonce 13, tag 4), cross-checked via pyca.
 * Run: pio test -e native -f test_ccm */
#include <string.h>
#include <unity.h>

#include "aes.h"
#include "vectors_ccm.h"
#include "waymesh_crypto.h"

void setUp(void) {}
void tearDown(void) {}

/* AES single-block encrypt matches FIPS-197 Appendix C. */
static void test_aes_block(void)
{
    for (size_t i = 0; i < WM_AES_BLOCK_VEC_COUNT; i++) {
        const wm_aes_block_vec_t *v = &WM_AES_BLOCK_VECS[i];
        wm_aes_ctx ctx;
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, wm_aes_init(&ctx, v->key, v->key_len),
                                      v->label);
        uint8_t out[16];
        wm_aes_encrypt_block(&ctx, v->pt, out);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->ct, out, 16, v->label);
    }
}

/* CCM seal produces the expected ciphertext + tag for every vector. */
static void test_ccm_seal(void)
{
    for (size_t i = 0; i < WM_CCM_VEC_COUNT; i++) {
        const wm_ccm_vec_t *v = &WM_CCM_VECS[i];
        uint8_t ct[32], tag[16];
        int rc = wm_ccm_seal(v->key, v->key_len, v->nonce, v->nonce_len,
                             v->aad, v->aad_len, v->pt, v->pt_len,
                             ct, tag, v->tag_len);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, v->label);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->ct, ct, v->pt_len, v->label);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->tag, tag, v->tag_len, v->label);
    }
}

/* CCM open recovers the plaintext and accepts a valid tag. */
static void test_ccm_open(void)
{
    for (size_t i = 0; i < WM_CCM_VEC_COUNT; i++) {
        const wm_ccm_vec_t *v = &WM_CCM_VECS[i];
        uint8_t pt[32];
        int rc = wm_ccm_open(v->key, v->key_len, v->nonce, v->nonce_len,
                             v->aad, v->aad_len, v->ct, v->pt_len,
                             v->tag, v->tag_len, pt);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, v->label);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->pt, pt, v->pt_len, v->label);
    }
}

/* Any single-byte tamper (ct / tag / aad / nonce) fails the open with no
 * plaintext leak. Uses the Waymesh AES-128 vector (index 2). */
static void test_ccm_tamper(void)
{
    const wm_ccm_vec_t *v = &WM_CCM_VECS[2];
    uint8_t pt[32];
    uint8_t ct[32], tag[16], aad[16], nonce[13];
    memcpy(ct, v->ct, v->pt_len);
    memcpy(tag, v->tag, v->tag_len);
    memcpy(aad, v->aad, v->aad_len);
    memcpy(nonce, v->nonce, v->nonce_len);

    /* baseline opens cleanly */
    TEST_ASSERT_EQUAL_INT(0, wm_ccm_open(v->key, v->key_len, nonce, v->nonce_len,
                                         aad, v->aad_len, ct, v->pt_len, tag,
                                         v->tag_len, pt));

    /* flip a ciphertext bit */
    ct[0] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-2, wm_ccm_open(v->key, v->key_len, nonce, v->nonce_len,
                                          aad, v->aad_len, ct, v->pt_len, tag,
                                          v->tag_len, pt));
    for (size_t i = 0; i < v->pt_len; i++)
        TEST_ASSERT_EQUAL_UINT8(0, pt[i]); /* zeroed on auth failure */
    ct[0] ^= 0x01;

    /* flip a tag bit */
    tag[0] ^= 0x80;
    TEST_ASSERT_EQUAL_INT(-2, wm_ccm_open(v->key, v->key_len, nonce, v->nonce_len,
                                          aad, v->aad_len, ct, v->pt_len, tag,
                                          v->tag_len, pt));
    tag[0] ^= 0x80;

    /* flip an AAD bit (e.g. a spoofed header field) */
    aad[1] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-2, wm_ccm_open(v->key, v->key_len, nonce, v->nonce_len,
                                          aad, v->aad_len, ct, v->pt_len, tag,
                                          v->tag_len, pt));
    aad[1] ^= 0x01;

    /* flip a nonce bit */
    nonce[0] ^= 0x01;
    TEST_ASSERT_EQUAL_INT(-2, wm_ccm_open(v->key, v->key_len, nonce, v->nonce_len,
                                          aad, v->aad_len, ct, v->pt_len, tag,
                                          v->tag_len, pt));
}

/* Round-trip on fresh data at our wire params (AES-128 and AES-256). */
static void test_ccm_roundtrip(void)
{
    const size_t keylens[2] = {16, 32};
    for (int k = 0; k < 2; k++) {
        uint8_t key[32], nonce[13] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
        uint8_t aad[11] = {2,8,7,0x44,0x33,0x22,0x11,0x39,5,0,0};
        uint8_t pt[10] = {9,8,7,6,5,4,3,2,1,0};
        uint8_t ct[10], tag[4], out[10];
        for (size_t i = 0; i < 32; i++) key[i] = (uint8_t)(0xA0 + i);
        TEST_ASSERT_EQUAL_INT(0, wm_ccm_seal(key, keylens[k], nonce, 13, aad, 11,
                                             pt, 10, ct, tag, 4));
        TEST_ASSERT_EQUAL_INT(0, wm_ccm_open(key, keylens[k], nonce, 13, aad, 11,
                                             ct, 10, tag, 4, out));
        TEST_ASSERT_EQUAL_MEMORY(pt, out, 10);
    }
}

/* Bad parameters are rejected. */
static void test_ccm_bad_params(void)
{
    uint8_t key[16] = {0}, nonce[13] = {0}, pt[4] = {0}, ct[4], tag[4];
    TEST_ASSERT_EQUAL_INT(-1, wm_ccm_seal(key, 24, nonce, 13, NULL, 0, pt, 4,
                                          ct, tag, 4));     /* bad key_len */
    TEST_ASSERT_EQUAL_INT(-1, wm_ccm_seal(key, 16, nonce, 6, NULL, 0, pt, 4,
                                          ct, tag, 4));      /* nonce too short */
    TEST_ASSERT_EQUAL_INT(-1, wm_ccm_seal(key, 16, nonce, 13, NULL, 0, pt, 4,
                                          ct, tag, 5));      /* odd tag_len */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_aes_block);
    RUN_TEST(test_ccm_seal);
    RUN_TEST(test_ccm_open);
    RUN_TEST(test_ccm_tamper);
    RUN_TEST(test_ccm_roundtrip);
    RUN_TEST(test_ccm_bad_params);
    return UNITY_END();
}
