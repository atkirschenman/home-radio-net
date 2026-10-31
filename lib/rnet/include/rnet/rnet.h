/* rnet: frames, keys and replay protection (01-architecture.md sections 4-5).
 * Portable C99, no malloc. AES-128 and AES-CMAC are TinyCrypt's (vendored,
 * lib/rnet/vendor/tinycrypt); message layouts are generated (rnet_msgs.h). */
#ifndef RNET_RNET_H
#define RNET_RNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rnet_msgs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RNET_KEY_LEN 16u

typedef struct {
    uint8_t ctrl; /* RNET_CTRL_* bits; ACK is set by rnet_frame_build when has_ack */
    uint8_t net_id;
    uint16_t node_id;
    uint8_t node_type;
    uint32_t ctr;
    uint8_t msg_type;
    bool has_ack;
    uint16_t ack_ctr;
} rnet_hdr_t;

/* K_node = AES-128(master, node_id LE || node_type || "rnet" || zeros) (5.1). */
void rnet_derive_key(const uint8_t master[RNET_KEY_LEN], uint16_t node_id, uint8_t node_type,
                     uint8_t out[RNET_KEY_LEN]);

/* Header + payload + 4-byte CMAC into out. Returns the frame length, or 0 if
 * it would not fit in cap or in one CC1101 FIFO (RNET_MAX_PAYLOAD). */
size_t rnet_frame_build(const uint8_t key[RNET_KEY_LEN], const rnet_hdr_t *h,
                        const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap);

/* Parse the header and locate the payload. Does NOT check the MIC: the
 * receiver needs node_id/node_type first to pick the key. False if the frame
 * is too short or has an unknown frame version. */
bool rnet_frame_parse(const uint8_t *frame, size_t len, rnet_hdr_t *h, const uint8_t **payload,
                      size_t *payload_len);

/* True if the frame's MIC matches under key (constant-time compare). */
bool rnet_frame_verify(const uint8_t key[RNET_KEY_LEN], const uint8_t *frame, size_t len);

/* ---- replay (5.2) ---- */
typedef enum { RNET_FRESH, RNET_DUPLICATE, RNET_REPLAY } rnet_replay_t;

typedef struct {
    bool have;
    uint32_t last; /* last accepted counter */
} rnet_rx_state_t;

/* Classify ctr against the last accepted one; rnet_replay_accept records it.
 * A duplicate (same ctr) is a repeat of an accepted frame: drop silently. */
rnet_replay_t rnet_replay_check(const rnet_rx_state_t *s, uint32_t ctr);
void rnet_replay_accept(rnet_rx_state_t *s, uint32_t ctr);

/* ---- sender counter persistence (5.2) ---- */
/* Counter to use after a reset, given the last value stored. */
static inline uint32_t rnet_ctr_resume(uint32_t stored)
{
    return stored + RNET_COUNTER_PERSIST_EVERY;
}
/* True when ctr is the one to store before sending (every Nth frame). */
static inline bool rnet_ctr_due(uint32_t ctr)
{
    return (ctr % RNET_COUNTER_PERSIST_EVERY) == 0u;
}

#ifdef __cplusplus
}
#endif

#endif
