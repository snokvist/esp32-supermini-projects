/* Host unit test: Meshtastic-compatible channel hash + PSK expansion.
 * Asserts wm_chan_hash / wm_expand_psk match golden vectors generated from
 * meshtastic/firmware @ v2.6.4.b89355f (tools/gen_meshtastic_vectors.sh).
 * Run: pio test -e native -f test_channel_hash */
#include <string.h>
#include <unity.h>

#include "vectors_channel.h"
#include "waymesh_crypto.h"

void setUp(void) {}
void tearDown(void) {}

/* PSK expansion matches upstream getKey() for every vector. */
static void test_expand_psk(void)
{
    for (size_t i = 0; i < WM_CHANNEL_VEC_COUNT; i++) {
        const wm_channel_vec_t *v = &WM_CHANNEL_VECS[i];
        uint8_t key[WM_MAX_KEY_LEN];
        size_t klen = 0xdead;
        int rc = wm_expand_psk(v->psk, v->psk_len, key, &klen);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, v->name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(v->exp_key_len, klen, v->name);
        if (klen)
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->exp_key, key, klen, v->name);
    }
}

/* Channel hash matches upstream generateHash() for every vector. */
static void test_chan_hash(void)
{
    for (size_t i = 0; i < WM_CHANNEL_VEC_COUNT; i++) {
        const wm_channel_vec_t *v = &WM_CHANNEL_VECS[i];
        int h = wm_chan_hash(v->name, v->psk, v->psk_len);
        TEST_ASSERT_EQUAL_INT_MESSAGE(v->hash, h, v->name);
    }
}

/* The well-known Meshtastic default channel hash is 8 (public anchor). */
static void test_default_channel_hash_is_8(void)
{
    const uint8_t psk1[1] = {0x01};
    TEST_ASSERT_EQUAL_INT(8, wm_chan_hash("LongFast", psk1, 1));
}

/* PSK index 1 expands to exactly the famous default key. */
static void test_index1_equals_default_key(void)
{
    const uint8_t psk1[1] = {0x01};
    uint8_t key[WM_MAX_KEY_LEN];
    size_t klen = 0;
    TEST_ASSERT_EQUAL_INT(0, wm_expand_psk(psk1, 1, key, &klen));
    TEST_ASSERT_EQUAL_UINT(16, klen);
    TEST_ASSERT_EQUAL_MEMORY(WM_DEFAULT_PSK, key, 16);
}

/* Empty name and disabled (key-less) channels yield no usable hash. */
static void test_invalid_inputs(void)
{
    const uint8_t psk1[1] = {0x01};
    TEST_ASSERT_EQUAL_INT(-1, wm_chan_hash("", psk1, 1));      /* empty name */
    const uint8_t off[1] = {0x00};
    TEST_ASSERT_EQUAL_INT(-1, wm_chan_hash("any", off, 1));    /* encryption off */
    TEST_ASSERT_EQUAL_INT(-1, wm_chan_hash("any", NULL, 0));   /* no key */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_expand_psk);
    RUN_TEST(test_chan_hash);
    RUN_TEST(test_default_channel_hash_is_8);
    RUN_TEST(test_index1_equals_default_key);
    RUN_TEST(test_invalid_inputs);
    return UNITY_END();
}
