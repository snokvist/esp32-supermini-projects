/*
 * waymesh_config — portable persistent config + packetId counter (doc 13 §8).
 *
 * Pure C99, no Arduino / NVS dependency: the actual storage is a pluggable
 * key-value backend (wm_store_t), so the correctness-critical logic — the
 * crash-safe packetId reserve-ahead and the channel model — is host-unit-tested
 * (test/test_config) with a mock backend that simulates power-loss. The device
 * backends are thin shims over the same interface (C3: Preferences/NVS; the
 * 8285 tier: EEPROM/flash) wired in later steps.
 *
 * Holds (§8): the channel list [(name, psk, index)] with explicit non-empty
 * names (§4), the home TX channel, the relay policy, and the §8 packetId
 * high-water. One blob write on channel-set; one u32 write per reserve-ahead.
 */
#ifndef WAYMESH_CONFIG_H
#define WAYMESH_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "waymesh_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WM_MAX_CHANNELS 8     /* Meshtastic max channels bridged at the gateway */
#define WM_CHAN_NAME_MAX 16   /* explicit name, NUL-terminated (Meshtastic ~11) */
#define WM_PSK_MAX 32         /* AES-256 raw key */
#define WM_PKTID_BLOCK 1024u  /* reserve-ahead block size (§11, tunable) */

/* Persistence keys (kept short for NVS). */
#define WM_KEY_CHANS "wm.chans"
#define WM_KEY_PID   "wm.pid"

typedef enum {
    WM_RELAY_ALL = 0,   /* re-flood every chanHash verbatim (dumb public relay) */
    WM_RELAY_KNOWN = 1, /* relay only configured chanHashes */
} wm_relay_policy_t;

/* A configured channel. Stored: name, psk, index. Derived on load/add. */
typedef struct {
    char name[WM_CHAN_NAME_MAX];
    uint8_t psk[WM_PSK_MAX];
    uint8_t psk_len;
    uint8_t index;                          /* Meshtastic channel index 0..7 */
    uint8_t expanded_key[WM_MAX_KEY_LEN];   /* derived */
    size_t  key_len;                        /* derived; 0 = clear (no crypto) */
    int     hash;                           /* derived; 0..255 or -1 */
} wm_channel_t;

/* Pluggable key-value persistence (maps 1:1 onto ESP32 Preferences). Each
 * accessor returns 0 on success, -1 on absent/error. rng() supplies a HW-random
 * seed when the packetId high-water is missing/corrupt (fresh NVS). */
typedef struct {
    int (*get_u32)(void *ctx, const char *key, uint32_t *out);
    int (*put_u32)(void *ctx, const char *key, uint32_t v);
    int (*get_blob)(void *ctx, const char *key, void *out, size_t cap,
                    size_t *out_len);
    int (*put_blob)(void *ctx, const char *key, const void *data, size_t len);
    uint32_t (*rng)(void *ctx);
    void *ctx;
} wm_store_t;

typedef struct {
    wm_store_t store;
    wm_channel_t channels[WM_MAX_CHANNELS];
    uint8_t channel_count;
    uint8_t home_slot;                /* index into channels[] used for TX */
    wm_relay_policy_t relay_policy;
    uint32_t pid_next;                /* next packetId to issue */
    uint32_t pid_ceiling;             /* persisted high-water; pid_next < this */
} wm_config_t;

/* Load channels/relay/home from the store (or seed the Meshtastic default
 * channel "LongFast"/psk={0x01} -> hash 8 if none), then initialise the
 * crash-safe packetId counter (reserve a fresh block, seeding from rng() on
 * fresh/corrupt NVS). Returns 0 on success. */
int wm_config_init(wm_config_t *cfg, const wm_store_t *store);

/* Reboot-safe monotonic packetId (§8): never repeats under one key, even across
 * crashes — the high-water ceiling is persisted before a block of IDs is used.
 * A failed persist is a hard fault the device backend must handle (halt/reset);
 * see test_config for the crash/reboot non-reuse soak. */
uint32_t wm_config_next_packet_id(wm_config_t *cfg);

/* Add/replace a channel (matched by Meshtastic index). Computes the derived
 * hash/key (§4) and persists the channel blob. name must be non-empty. The
 * first channel added becomes the home (TX) channel. Returns 0 on success. */
int wm_config_add_channel(wm_config_t *cfg, const char *name,
                          const uint8_t *psk, size_t psk_len, uint8_t index);

/* Set which configured channel is the home (TX) channel, by Meshtastic index.
 * Returns 0 on success, -1 if no such channel. Persists. */
int wm_config_set_home(wm_config_t *cfg, uint8_t index);

/* Set the relay policy and persist. */
int wm_config_set_relay_policy(wm_config_t *cfg, wm_relay_policy_t policy);

const wm_channel_t *wm_config_home(const wm_config_t *cfg);

/* First configured channel whose derived hash == h (NULL if none). chanHash is
 * a 1-byte filter so collisions are possible; the caller resolves a collision
 * by attempting AEAD-open and trying the next match on failure (§5/§6). */
const wm_channel_t *wm_config_channel_by_hash(const wm_config_t *cfg, uint8_t h);

/* Acceptance step 3 (§6): is chanHash h in our accepted set? */
bool wm_config_accepts_hash(const wm_config_t *cfg, uint8_t h);

/* Relay policy (§6): should the relay re-flood chanHash h? relay-all -> always;
 * relay-known -> only configured hashes. */
bool wm_config_relays_hash(const wm_config_t *cfg, uint8_t h);

#ifdef __cplusplus
}
#endif

#endif /* WAYMESH_CONFIG_H */
