/* Waymesh v2 beacon codec + RX/relay decisions (doc 13 §3/§6). Portable C99. */
#include "wm_beacon.h"

#include <string.h>

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
