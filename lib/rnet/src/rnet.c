#include "rnet/rnet.h"

#include <string.h>

#include <tinycrypt/aes.h>
#include <tinycrypt/cmac_mode.h>
#include <tinycrypt/constants.h>
#include <tinycrypt/utils.h>

void rnet_derive_key(const uint8_t master[RNET_KEY_LEN], uint16_t node_id, uint8_t node_type,
                     uint8_t out[RNET_KEY_LEN])
{
    uint8_t block[TC_AES_BLOCK_SIZE] = {0};
    block[0] = (uint8_t)node_id;
    block[1] = (uint8_t)(node_id >> 8);
    block[2] = node_type;
    memcpy(&block[3], RNET_KEY_LABEL, sizeof RNET_KEY_LABEL - 1u);
    struct tc_aes_key_sched_struct sched;
    (void)tc_aes128_set_encrypt_key(&sched, master);
    (void)tc_aes_encrypt(out, block, &sched);
}

static void mic(const uint8_t key[RNET_KEY_LEN], const uint8_t *data, size_t len,
                uint8_t tag[TC_AES_BLOCK_SIZE])
{
    struct tc_aes_key_sched_struct sched;
    struct tc_cmac_struct state;
    (void)tc_cmac_setup(&state, key, &sched);
    (void)tc_cmac_update(&state, data, len);
    (void)tc_cmac_final(tag, &state);
}

size_t rnet_frame_build(const uint8_t key[RNET_KEY_LEN], const rnet_hdr_t *h,
                        const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap)
{
    const size_t hdr = RNET_HEADER_LEN + (h->has_ack ? 2u : 0u);
    const size_t total = hdr + payload_len + RNET_MIC_LEN;
    if (total > cap || total > RNET_MAX_PAYLOAD) {
        return 0;
    }
    uint8_t ctrl = (uint8_t)(h->ctrl & ~(3u << RNET_CTRL_VERSION_SHIFT));
    ctrl = (uint8_t)(ctrl | (RNET_FRAME_VERSION << RNET_CTRL_VERSION_SHIFT));
    ctrl = (uint8_t)(h->has_ack ? (ctrl | RNET_CTRL_ACK) : (ctrl & ~RNET_CTRL_ACK));
    out[0] = ctrl;
    out[1] = h->net_id;
    out[2] = (uint8_t)h->node_id;
    out[3] = (uint8_t)(h->node_id >> 8);
    out[4] = h->node_type;
    for (unsigned i = 0; i < 4u; i++) {
        out[5u + i] = (uint8_t)(h->ctr >> (8u * i));
    }
    out[9] = h->msg_type;
    if (h->has_ack) {
        out[10] = (uint8_t)h->ack_ctr;
        out[11] = (uint8_t)(h->ack_ctr >> 8);
    }
    if (payload_len) {
        memcpy(&out[hdr], payload, payload_len);
    }
    uint8_t tag[TC_AES_BLOCK_SIZE];
    mic(key, out, hdr + payload_len, tag);
    memcpy(&out[hdr + payload_len], tag, RNET_MIC_LEN);
    return total;
}

bool rnet_frame_parse(const uint8_t *frame, size_t len, rnet_hdr_t *h, const uint8_t **payload,
                      size_t *payload_len)
{
    if (len < RNET_HEADER_LEN + RNET_MIC_LEN) {
        return false;
    }
    if (((frame[0] >> RNET_CTRL_VERSION_SHIFT) & 3u) != RNET_FRAME_VERSION) {
        return false;
    }
    h->ctrl = frame[0];
    h->net_id = frame[1];
    h->node_id = (uint16_t)(frame[2] | frame[3] << 8);
    h->node_type = frame[4];
    h->ctr = (uint32_t)frame[5] | (uint32_t)frame[6] << 8 | (uint32_t)frame[7] << 16 |
             (uint32_t)frame[8] << 24;
    h->msg_type = frame[9];
    h->has_ack = (frame[0] & RNET_CTRL_ACK) != 0u;
    h->ack_ctr = 0;
    size_t off = RNET_HEADER_LEN;
    if (h->has_ack) {
        if (len < RNET_HEADER_LEN + 2u + RNET_MIC_LEN) {
            return false;
        }
        h->ack_ctr = (uint16_t)(frame[10] | frame[11] << 8);
        off += 2u;
    }
    *payload = &frame[off];
    *payload_len = len - off - RNET_MIC_LEN;
    return true;
}

bool rnet_frame_verify(const uint8_t key[RNET_KEY_LEN], const uint8_t *frame, size_t len)
{
    if (len <= RNET_MIC_LEN) {
        return false;
    }
    uint8_t tag[TC_AES_BLOCK_SIZE];
    mic(key, frame, len - RNET_MIC_LEN, tag);
    return _compare(tag, &frame[len - RNET_MIC_LEN], RNET_MIC_LEN) ==
           0; /* TinyCrypt, constant time */
}

rnet_replay_t rnet_replay_check(const rnet_rx_state_t *s, uint32_t ctr)
{
    if (!s->have || ctr > s->last) {
        return RNET_FRESH;
    }
    return ctr == s->last ? RNET_DUPLICATE : RNET_REPLAY;
}

void rnet_replay_accept(rnet_rx_state_t *s, uint32_t ctr)
{
    s->have = true;
    s->last = ctr;
}
