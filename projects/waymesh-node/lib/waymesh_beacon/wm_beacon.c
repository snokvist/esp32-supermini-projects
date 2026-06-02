/* Waymesh v2 beacon codec + RX/relay decisions (doc 13 §3/§6). Portable C99. */
#include "wm_beacon.h"

#include <string.h>

#include "waymesh_crypto.h" /* wm_ccm_seal / wm_ccm_open (phase-2 AEAD, §5) */

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static void wr_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int wm_beacon_parse(const uint8_t *raw, size_t len, wm_beacon_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < 2 || raw[0] != WM_BEACON_MAGIC) return -1;
    uint8_t ver = raw[1];
    out->version = ver;

    if (ver <= WM_BEACON_V1) {
        /* v0/v1: magic,version,srcId(2-5 LE),seq(6-7 LE),[v1 tail] */
        if (len < 8) return -1;
        out->src_id = rd_u32le(raw + 2);
        out->packet_id = rd_u16le(raw + 6);
        out->chan_hash = WM_OPEN_GROUP_HASH; /* legacy = open group (§3) */
        if (ver >= WM_BEACON_V1 && len >= 18) {
            uint8_t flags = raw[17];
            out->flags = flags;
            if (flags & WM_BFLAG_POS) {
                out->lat_i = (int32_t)rd_u32le(raw + 8);
                out->lon_i = (int32_t)rd_u32le(raw + 12);
                out->sats = raw[16];
                out->has_pos = true;
            }
        }
        return 0;
    }

    if (ver == WM_BEACON_V2) {
        if (len < WM_BEACON_V2_HDR) return -1;
        out->chan_hash = raw[2];
        out->flags = raw[3];
        out->src_id = rd_u32le(raw + 4);
        out->packet_id = rd_u32le(raw + 8);

        const uint8_t *p = raw + WM_BEACON_V2_HDR;
        size_t plen = len - WM_BEACON_V2_HDR;
        size_t mic = (out->flags & WM_BFLAG_HASMIC) ? WM_BEACON_MIC_LEN : 0;
        if (plen < mic) return -1;
        out->payload = plen > mic ? p : NULL;
        out->payload_len = plen - mic;
        out->mic = mic ? p + (plen - mic) : NULL;
        out->mic_len = mic;

        /* Clear POS only (phase 1). Encrypted payloads are opened by the caller
         * (phase 2); leave has_pos=false so a pre-decrypt read never trusts
         * ciphertext as a position. */
        if ((out->flags & WM_BFLAG_POS) && !(out->flags & WM_BFLAG_ENCRYPTED)) {
            if (out->payload_len < WM_BEACON_POS_LEN) return -1;
            out->lat_i = (int32_t)rd_u32le(out->payload + 0);
            out->lon_i = (int32_t)rd_u32le(out->payload + 4);
            out->sats = out->payload[8];
            out->has_pos = true;
        }
        return 0;
    }

    return -1; /* unknown version */
}

size_t wm_beacon_build_v2_clear(uint8_t *buf, uint8_t chan_hash, uint32_t src_id,
                                uint32_t packet_id, bool pos_valid,
                                int32_t lat_i, int32_t lon_i, uint8_t sats)
{
    buf[0] = WM_BEACON_MAGIC;
    buf[1] = WM_BEACON_V2;
    buf[2] = chan_hash;
    buf[3] = pos_valid ? WM_BFLAG_POS : 0; /* clear, no MIC (phase 1) */
    wr_u32le(buf + 4, src_id);
    wr_u32le(buf + 8, packet_id);
    size_t n = WM_BEACON_V2_HDR;
    if (pos_valid) {
        wr_u32le(buf + 12, (uint32_t)lat_i);
        wr_u32le(buf + 16, (uint32_t)lon_i);
        buf[20] = sats;
        buf[21] = 0; /* payload-flags rsvd */
        n += WM_BEACON_POS_LEN;
    }
    return n;
}

/* --- v2 AEAD (phase 2, §5): nonce + AAD assembly -------------------------- */

#define WM_V2_AAD_LEN 11   /* clear header[1..11]: version..packetId */
#define WM_V2_NONCE_LEN 13 /* CCM L=2 */

/* AAD = the clear header bytes [1..11] (version, chanHash, flags, srcId,
 * packetId). Authenticating flags stops an outsider flipping ENCRYPTED/POS, and
 * authenticating srcId/packetId binds the position to its originator + nonce.
 * Rebuilt identically on TX (from args) and RX (from parsed fields). */
static void v2_aad(uint8_t aad[WM_V2_AAD_LEN], uint8_t chan_hash, uint8_t flags,
                   uint32_t src_id, uint32_t packet_id)
{
    aad[0] = WM_BEACON_V2;
    aad[1] = chan_hash;
    aad[2] = flags;
    wr_u32le(aad + 3, src_id);
    wr_u32le(aad + 7, packet_id);
}

/* 13-byte CCM nonce. Uniqueness per (key, message) rests on the reboot-safe
 * monotonic packetId (§3/§8) never repeating under one key. The layout is OURS
 * (this payload never reaches the Meshtastic app) but fully deterministic, so
 * any key-holder reconstructs it from the clear header. The trailing "WM" +
 * version + 0x00 is fixed domain separation. */
static void v2_nonce(uint8_t n[WM_V2_NONCE_LEN], uint8_t chan_hash,
                     uint32_t src_id, uint32_t packet_id)
{
    wr_u32le(n + 0, packet_id);
    wr_u32le(n + 4, src_id);
    n[8]  = chan_hash;
    n[9]  = 'W';
    n[10] = 'M';
    n[11] = WM_BEACON_V2;
    n[12] = 0x00;
}

size_t wm_beacon_build_v2_enc(uint8_t *buf, uint8_t chan_hash, uint32_t src_id,
                              uint32_t packet_id, int32_t lat_i, int32_t lon_i,
                              uint8_t sats, const uint8_t *key, size_t key_len)
{
    if (key_len != 16 && key_len != 32) return 0; /* no usable key -> clear */

    buf[0] = WM_BEACON_MAGIC;
    buf[1] = WM_BEACON_V2;
    buf[2] = chan_hash;
    buf[3] = WM_BFLAG_POS | WM_BFLAG_ENCRYPTED | WM_BFLAG_HASMIC;
    wr_u32le(buf + 4, src_id);
    wr_u32le(buf + 8, packet_id);

    /* clear POS payload (same layout as the clear builder), sealed into buf */
    uint8_t pt[WM_BEACON_POS_LEN];
    wr_u32le(pt + 0, (uint32_t)lat_i);
    wr_u32le(pt + 4, (uint32_t)lon_i);
    pt[8] = sats;
    pt[9] = 0; /* payload-flags rsvd */

    uint8_t aad[WM_V2_AAD_LEN], nonce[WM_V2_NONCE_LEN];
    v2_aad(aad, chan_hash, buf[3], src_id, packet_id);
    v2_nonce(nonce, chan_hash, src_id, packet_id);

    uint8_t *ct = buf + WM_BEACON_V2_HDR;                       /* 12..21 */
    uint8_t *tag = ct + WM_BEACON_POS_LEN;                      /* 22..25 */
    if (wm_ccm_seal(key, key_len, nonce, WM_V2_NONCE_LEN, aad, WM_V2_AAD_LEN,
                    pt, WM_BEACON_POS_LEN, ct, tag, WM_BEACON_MIC_LEN) != 0)
        return 0;
    return WM_BEACON_V2_HDR + WM_BEACON_POS_LEN + WM_BEACON_MIC_LEN; /* 26 */
}

size_t wm_beacon_build_v2_text(uint8_t *buf, uint8_t chan_hash, uint32_t src_id,
                               uint32_t packet_id, const uint8_t *text,
                               size_t text_len, const uint8_t *key,
                               size_t key_len)
{
    if (key_len != 16 && key_len != 32) return 0;          /* no usable key */
    if (text_len == 0 || text_len > WM_BEACON_TEXT_MAX) return 0;

    buf[0] = WM_BEACON_MAGIC;
    buf[1] = WM_BEACON_V2;
    buf[2] = chan_hash;
    buf[3] = WM_BFLAG_TEXT | WM_BFLAG_ENCRYPTED | WM_BFLAG_HASMIC;
    wr_u32le(buf + 4, src_id);
    wr_u32le(buf + 8, packet_id);

    uint8_t aad[WM_V2_AAD_LEN], nonce[WM_V2_NONCE_LEN];
    v2_aad(aad, chan_hash, buf[3], src_id, packet_id);
    v2_nonce(nonce, chan_hash, src_id, packet_id);

    uint8_t *ct = buf + WM_BEACON_V2_HDR;
    uint8_t *tag = ct + text_len;
    if (wm_ccm_seal(key, key_len, nonce, WM_V2_NONCE_LEN, aad, WM_V2_AAD_LEN,
                    text, text_len, ct, tag, WM_BEACON_MIC_LEN) != 0)
        return 0;
    return WM_BEACON_V2_HDR + text_len + WM_BEACON_MIC_LEN;
}

int wm_beacon_open(wm_beacon_t *b, const uint8_t *key, size_t key_len,
                   uint8_t *pt_out, size_t pt_cap, size_t *pt_len)
{
    if (!(b->flags & WM_BFLAG_ENCRYPTED) || !(b->flags & WM_BFLAG_HASMIC))
        return -1;
    if (key_len != 16 && key_len != 32) return -1;
    if (b->mic_len != WM_BEACON_MIC_LEN || !b->payload || !b->mic ||
        b->payload_len == 0 || b->payload_len > pt_cap)
        return -1;

    uint8_t aad[WM_V2_AAD_LEN], nonce[WM_V2_NONCE_LEN];
    v2_aad(aad, b->chan_hash, b->flags, b->src_id, b->packet_id);
    v2_nonce(nonce, b->chan_hash, b->src_id, b->packet_id);

    /* verify-then-decrypt; on a bad tag wm_ccm_open zeroizes pt_out and returns
     * <0, so a tampered/foreign frame exposes no plaintext (the caller DROPs). */
    if (wm_ccm_open(key, key_len, nonce, WM_V2_NONCE_LEN, aad, WM_V2_AAD_LEN,
                    b->payload, b->payload_len, b->mic, b->mic_len, pt_out) != 0)
        return -1;

    if (pt_len) *pt_len = b->payload_len;

    /* A POS beacon (POS flag, not TEXT) also fills the parsed position. */
    if ((b->flags & WM_BFLAG_POS) && !(b->flags & WM_BFLAG_TEXT)) {
        if (b->payload_len < WM_BEACON_POS_LEN) return -1;
        b->lat_i = (int32_t)rd_u32le(pt_out + 0);
        b->lon_i = (int32_t)rd_u32le(pt_out + 4);
        b->sats  = pt_out[8];
        b->has_pos = true;
    }
    return 0;
}

wm_rx_decision_t wm_beacon_accept(const wm_beacon_t *b, uint32_t self_id,
                                  const wm_config_t *cfg)
{
    if (b->src_id == self_id) return WM_RX_DROP_SELF;
    if (!wm_config_accepts_hash(cfg, b->chan_hash)) return WM_RX_DROP_FOREIGN_GROUP;
    return WM_RX_ACCEPT;
}

bool wm_beacon_should_relay(const wm_beacon_t *b, const wm_config_t *cfg)
{
    return wm_config_relays_hash(cfg, b->chan_hash);
}
