/*
 * gen_vectors_main.c — generator for test/test_channel_hash/vectors_channel.h.
 *
 * NOT compiled into firmware or tests. Run via tools/gen_meshtastic_vectors.sh,
 * which clones meshtastic/firmware at the pinned ref, extracts the verbatim
 * defaultpsk[] into upstream_defaultpsk.h (so the key constant has provable
 * upstream provenance, not a retype), then builds + runs this to emit golden
 * vectors. The committed repo carries only this generator + the produced
 * vectors header — no GPL upstream source. See docs/hybrid-mesh/13 §10.
 *
 * The channel hash uses xorHash(name) ^ xorHash(expandedKey) exactly as
 * meshtastic/firmware Channels.cpp::generateHash. Full 16/32-byte-key cases
 * need no PSK expansion at all (fully independent of our lib's expansion); the
 * 1-byte-index cases are anchored to the upstream defaultpsk constant.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "upstream_defaultpsk.h" /* generated: UPSTREAM_DEFAULTPSK[16], UPSTREAM_REF */

static uint8_t xor_hash(const uint8_t *p, size_t len)
{
    uint8_t code = 0;
    for (size_t i = 0; i < len; i++) code ^= p[i];
    return code;
}

/* Expand a stored PSK exactly as Channels.cpp::getKey. */
static int expand_psk(const uint8_t *psk, size_t psk_len,
                      uint8_t out[32], size_t *out_len)
{
    memset(out, 0, 32);
    if (psk_len == 0) { *out_len = 0; return 0; }
    if (psk_len == 1) {
        uint8_t idx = psk[0];
        if (idx == 0) { *out_len = 0; return 0; }
        memcpy(out, UPSTREAM_DEFAULTPSK, 16);
        out[15] = (uint8_t)(out[15] + idx - 1);
        *out_len = 16;
        return 0;
    }
    if (psk_len <= 16) { memcpy(out, psk, psk_len); *out_len = 16; return 0; }
    if (psk_len <= 32) { memcpy(out, psk, psk_len); *out_len = 32; return 0; }
    return -1;
}

struct chan {
    const char *name;
    uint8_t psk[32];
    size_t psk_len;
};

/* Channel inputs covering: default index 1 (== verbatim defaultpsk), other
 * indices (last-byte bump), full AES-128 and AES-256 keys (no expansion),
 * a short padded key, and an encryption-off channel. */
static const struct chan CHANS[] = {
    {"LongFast",     {0x01}, 1},
    {"waymesh-open", {0x01}, 1},
    {"alpha",        {0x02}, 1},
    {"bravo",        {0x0a}, 1},
    {"team-blue",    {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                      0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f}, 16},
    {"team-red",     {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                      0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
                      0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                      0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f}, 32},
    {"padme",        {0xde,0xad,0xbe,0xef}, 4},
    {"silent",       {0x00}, 1},
};

int main(int argc, char **argv)
{
    FILE *f = (argc > 1) ? fopen(argv[1], "w") : stdout;
    if (!f) { perror("fopen"); return 1; }

    fprintf(f,
"/* GENERATED — do not edit. Channel-hash / PSK-expansion known-answer vectors.\n"
" *\n"
" * Produced by tools/gen_meshtastic_vectors.sh from meshtastic/firmware @\n"
" *   %s\n"
" * defaultpsk[] extracted verbatim from upstream src/mesh/Channels.h; hash =\n"
" * xorHash(name) ^ xorHash(expandedKey) per Channels.cpp::generateHash.\n"
" * Regenerate after changing the pinned ref or the input channel set. */\n"
"#ifndef WAYMESH_VECTORS_CHANNEL_H\n"
"#define WAYMESH_VECTORS_CHANNEL_H\n"
"#include <stddef.h>\n#include <stdint.h>\n\n"
"typedef struct {\n"
"  const char *name;\n"
"  uint8_t psk[32]; size_t psk_len;\n"
"  uint8_t exp_key[32]; size_t exp_key_len;\n"
"  int hash; /* 0..255, or -1 if no usable channel (encryption off) */\n"
"} wm_channel_vec_t;\n\n"
"static const wm_channel_vec_t WM_CHANNEL_VECS[] = {\n", UPSTREAM_REF);

    size_t n = sizeof(CHANS) / sizeof(CHANS[0]);
    for (size_t i = 0; i < n; i++) {
        uint8_t key[32]; size_t klen = 0;
        expand_psk(CHANS[i].psk, CHANS[i].psk_len, key, &klen);
        int hash = -1;
        if (klen > 0 && CHANS[i].name[0])
            hash = xor_hash((const uint8_t *)CHANS[i].name,
                            strlen(CHANS[i].name)) ^ xor_hash(key, klen);

        fprintf(f, "  { \"%s\", {", CHANS[i].name);
        for (size_t j = 0; j < CHANS[i].psk_len; j++)
            fprintf(f, "%s0x%02x", j ? "," : "", CHANS[i].psk[j]);
        fprintf(f, "}, %zu, {", CHANS[i].psk_len);
        for (size_t j = 0; j < klen; j++)
            fprintf(f, "%s0x%02x", j ? "," : "", key[j]);
        fprintf(f, "}, %zu, %d },\n", klen, hash);
    }
    fprintf(f, "};\n\n#define WM_CHANNEL_VEC_COUNT %zu\n\n", n);
    fprintf(f, "#endif /* WAYMESH_VECTORS_CHANNEL_H */\n");

    if (f != stdout) fclose(f);
    return 0;
}
