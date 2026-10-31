/* lib/rnet against the golden vectors (test/vectors, made by the Python
 * reference on OpenSSL): the C side (TinyCrypt) must produce the same keys
 * and byte-identical frames. Plus parse, tamper, replay and the generated
 * message codec. */
#include <string.h>

#include "../../../test/vectors/frames.h"
#include "rnet/rnet.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void test_key_derivation_matches_python(void)
{
    for (size_t i = 0; i < N(vec_keys); i++) {
        uint8_t k[16];
        rnet_derive_key(vec_master_key, vec_keys[i].node_id, vec_keys[i].node_type, k);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(vec_keys[i].key, k, 16);
    }
}

static rnet_hdr_t hdr_of(const vec_frame_t *v)
{
    rnet_hdr_t h = {0};
    h.ctrl = v->ctrl;
    h.net_id = v->net_id;
    h.node_id = v->node_id;
    h.node_type = v->node_type;
    h.ctr = v->ctr;
    h.msg_type = v->msg_type;
    h.has_ack = v->ack_ctr >= 0;
    h.ack_ctr = (uint16_t)(v->ack_ctr >= 0 ? v->ack_ctr : 0);
    return h;
}

static void test_frames_match_python(void)
{
    for (size_t i = 0; i < N(vec_frames); i++) {
        const vec_frame_t *v = &vec_frames[i];
        rnet_hdr_t h = hdr_of(v);
        uint8_t out[64];
        size_t n = rnet_frame_build(v->key, &h, v->payload, v->payload_len, out, sizeof out);
        TEST_ASSERT_EQUAL_MESSAGE(v->frame_len, n, v->desc);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v->frame, out, n, v->desc);
    }
}

static void test_parse_and_verify(void)
{
    for (size_t i = 0; i < N(vec_frames); i++) {
        const vec_frame_t *v = &vec_frames[i];
        rnet_hdr_t h;
        const uint8_t *pl;
        size_t pl_len;
        TEST_ASSERT_TRUE(rnet_frame_parse(v->frame, v->frame_len, &h, &pl, &pl_len));
        TEST_ASSERT_EQUAL_UINT16(v->node_id, h.node_id);
        TEST_ASSERT_EQUAL_UINT32(v->ctr, h.ctr);
        TEST_ASSERT_EQUAL_UINT8(v->msg_type, h.msg_type);
        TEST_ASSERT_EQUAL(v->ack_ctr >= 0, h.has_ack);
        TEST_ASSERT_EQUAL(v->payload_len, pl_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v->payload, pl, pl_len);
        TEST_ASSERT_TRUE(rnet_frame_verify(v->key, v->frame, v->frame_len));
    }
}

static void test_tamper_and_wrong_key_fail(void)
{
    const vec_frame_t *v = &vec_frames[0];
    uint8_t f[64];
    for (size_t i = 0; i < v->frame_len; i++) {
        memcpy(f, v->frame, v->frame_len);
        f[i] ^= 0x01;
        TEST_ASSERT_FALSE(rnet_frame_verify(v->key, f, v->frame_len));
    }
    uint8_t other[16];
    memcpy(other, v->key, 16);
    other[15] ^= 0x80;
    TEST_ASSERT_FALSE(rnet_frame_verify(other, v->frame, v->frame_len));
}

static void test_build_limits(void)
{
    rnet_hdr_t h = {0};
    uint8_t pl[61] = {0}, out[64];
    TEST_ASSERT_EQUAL(61, rnet_frame_build(vec_master_key, &h, pl, 47, out, sizeof out));
    TEST_ASSERT_EQUAL(0, rnet_frame_build(vec_master_key, &h, pl, 48, out, sizeof out));
    TEST_ASSERT_EQUAL(0, rnet_frame_build(vec_master_key, &h, pl, 10, out, 20));
    rnet_hdr_t p;
    const uint8_t *pp;
    size_t n;
    TEST_ASSERT_FALSE(rnet_frame_parse(out, 13, &p, &pp, &n));
    out[0] = 0x40; /* frame version 1: unknown */
    TEST_ASSERT_FALSE(rnet_frame_parse(out, 20, &p, &pp, &n));
}

static void test_replay(void)
{
    rnet_rx_state_t s = {0};
    TEST_ASSERT_EQUAL(RNET_FRESH, rnet_replay_check(&s, 5));
    rnet_replay_accept(&s, 5);
    TEST_ASSERT_EQUAL(RNET_DUPLICATE, rnet_replay_check(&s, 5));
    TEST_ASSERT_EQUAL(RNET_REPLAY, rnet_replay_check(&s, 4));
    TEST_ASSERT_EQUAL(RNET_FRESH, rnet_replay_check(&s, 6));
    TEST_ASSERT_EQUAL(256u + 300u, rnet_ctr_resume(300));
    TEST_ASSERT_TRUE(rnet_ctr_due(512));
    TEST_ASSERT_FALSE(rnet_ctr_due(513));
}

static void test_wx_report_codec(void)
{
    const vec_frame_t *v = &vec_frames[0]; /* WX_REPORT */
    rnet_wx_report_t m;
    TEST_ASSERT_EQUAL(RNET_WX_REPORT_NFIELDS,
                      rnet_wx_report_unpack(&m, v->payload, v->payload_len));
    uint8_t again[RNET_WX_REPORT_LEN];
    TEST_ASSERT_EQUAL(RNET_WX_REPORT_LEN, rnet_wx_report_pack(&m, again));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v->payload, again, RNET_WX_REPORT_LEN);

    /* An older, shorter payload: the missing tail reads as invalid (D3). */
    TEST_ASSERT_EQUAL(RNET_WX_REPORT_NFIELDS - 1u, rnet_wx_report_unpack(&m, v->payload, 33));
    TEST_ASSERT_EQUAL_HEX16(RNET_WX_REPORT_PREV_DIR_INVALID, m.prev_dir);

    /* Signed fields survive the round trip. */
    m.temp = -1234;
    rnet_wx_report_pack(&m, again);
    rnet_wx_report_t back;
    rnet_wx_report_unpack(&back, again, sizeof again);
    TEST_ASSERT_EQUAL_INT16(-1234, back.temp);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_key_derivation_matches_python);
    RUN_TEST(test_frames_match_python);
    RUN_TEST(test_parse_and_verify);
    RUN_TEST(test_tamper_and_wrong_key_fail);
    RUN_TEST(test_build_limits);
    RUN_TEST(test_replay);
    RUN_TEST(test_wx_report_codec);
    return UNITY_END();
}
