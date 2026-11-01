#include <cstring>

#include "../../../test/vectors/frames.hpp"
#include "rnet/rnet.hpp"
#include "unity.h"

void setUp() {}
void tearDown() {}

namespace
{

rnet::frame_header header_of(const vectors::frame &v)
{
    rnet::frame_header h;
    h.control = v.control;
    h.net_id = v.net_id;
    h.node_id = v.node_id;
    h.node_type = v.node_type;
    h.counter = v.counter;
    h.message_type = v.message_type;
    h.has_ack = v.ack_counter != vectors::no_ack;
    h.ack_counter = static_cast<uint16_t>(h.has_ack ? v.ack_counter : 0);
    return h;
}

void test_node_keys_match_openssl_reference()
{
    for (const auto &expected : vectors::derived_keys) {
        const rnet::aes_key key =
            rnet::derive_node_key(vectors::master_key, expected.node_id, expected.node_type);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.key.data(), key.data(), 16);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(vectors::master_key.data(), rnet::test_master_key.data(), 16);
}

void test_frames_match_openssl_reference()
{
    for (const auto &v : vectors::frames) {
        uint8_t frame[64];
        const std::size_t length = rnet::build_frame(v.key, header_of(v), v.payload.data(),
                                                     v.payload.size(), frame, sizeof frame);
        TEST_ASSERT_EQUAL_MESSAGE(v.bytes.size(), length, v.description);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.bytes.data(), frame, length, v.description);
    }
}

void test_reference_frames_parse_and_verify()
{
    for (const auto &v : vectors::frames) {
        rnet::parsed_frame parsed;
        TEST_ASSERT_TRUE(
            rnet::parse_frame_without_mic_check(v.bytes.data(), v.bytes.size(), parsed));
        TEST_ASSERT_EQUAL_UINT16(v.node_id, parsed.header.node_id);
        TEST_ASSERT_EQUAL_UINT32(v.counter, parsed.header.counter);
        TEST_ASSERT_EQUAL_UINT8(v.message_type, parsed.header.message_type);
        TEST_ASSERT_EQUAL(v.ack_counter != vectors::no_ack, parsed.header.has_ack);
        TEST_ASSERT_EQUAL(v.payload.size(), parsed.payload_length);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(v.payload.data(), parsed.payload, parsed.payload_length);
        TEST_ASSERT_TRUE(rnet::mic_is_valid(v.key, v.bytes.data(), v.bytes.size()));
    }
}

void test_any_flipped_bit_or_wrong_key_fails_the_mic()
{
    const auto &v = vectors::frames[0];
    for (std::size_t i = 0; i < v.bytes.size(); i++) {
        std::vector<uint8_t> tampered = v.bytes;
        tampered[i] ^= 0x01;
        TEST_ASSERT_FALSE(rnet::mic_is_valid(v.key, tampered.data(), tampered.size()));
    }
    rnet::aes_key wrong_key = v.key;
    wrong_key[15] ^= 0x80;
    TEST_ASSERT_FALSE(rnet::mic_is_valid(wrong_key, v.bytes.data(), v.bytes.size()));
}

void test_frame_size_limits_and_unknown_version()
{
    rnet::frame_header h;
    uint8_t payload[61] = {}, frame[64];
    const auto &key = rnet::test_master_key;
    TEST_ASSERT_EQUAL(61, rnet::build_frame(key, h, payload, 47, frame, sizeof frame));
    TEST_ASSERT_EQUAL(0, rnet::build_frame(key, h, payload, 48, frame, sizeof frame));
    TEST_ASSERT_EQUAL(0, rnet::build_frame(key, h, payload, 10, frame, 20));

    rnet::parsed_frame parsed;
    TEST_ASSERT_FALSE(rnet::parse_frame_without_mic_check(frame, 13, parsed));
    frame[0] = 1u << rnet::control_bits::version_shift;
    TEST_ASSERT_FALSE(rnet::parse_frame_without_mic_check(frame, 20, parsed));
}

void test_replay_verdicts()
{
    rnet::receive_counter counter;
    TEST_ASSERT_TRUE(counter.judge(5) == rnet::replay_verdict::fresh);
    counter.accept(5);
    TEST_ASSERT_TRUE(counter.judge(5) == rnet::replay_verdict::duplicate);
    TEST_ASSERT_TRUE(counter.judge(4) == rnet::replay_verdict::old);
    TEST_ASSERT_TRUE(counter.judge(6) == rnet::replay_verdict::fresh);
    static_assert(rnet::counter_after_reset(300) == 556);
    static_assert(rnet::counter_must_be_stored_before_use(512));
    static_assert(!rnet::counter_must_be_stored_before_use(513));
}

void test_weather_report_round_trip_and_short_payload()
{
    const auto &v = vectors::frames[0];
    rnet::weather_report report;
    TEST_ASSERT_EQUAL(rnet::weather_report::field_count,
                      report.unpack(v.payload.data(), v.payload.size()));
    uint8_t packed[rnet::weather_report::length];
    TEST_ASSERT_EQUAL(rnet::weather_report::length, report.pack(packed));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v.payload.data(), packed, rnet::weather_report::length);

    const std::size_t without_previous_wind_direction = rnet::weather_report::length - 2;
    TEST_ASSERT_EQUAL(rnet::weather_report::field_count - 1,
                      report.unpack(v.payload.data(), without_previous_wind_direction));
    TEST_ASSERT_EQUAL_HEX16(rnet::weather_report::previous_wind_direction_invalid, report.previous_wind_direction);

    rnet::weather_report fresh;
    TEST_ASSERT_EQUAL_INT16(rnet::weather_report::temperature_invalid, fresh.temperature);
    fresh.temperature = -1234;
    fresh.pack(packed);
    rnet::weather_report decoded;
    decoded.unpack(packed, sizeof packed);
    TEST_ASSERT_EQUAL_INT16(-1234, decoded.temperature);
}

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_node_keys_match_openssl_reference);
    RUN_TEST(test_frames_match_openssl_reference);
    RUN_TEST(test_reference_frames_parse_and_verify);
    RUN_TEST(test_any_flipped_bit_or_wrong_key_fails_the_mic);
    RUN_TEST(test_frame_size_limits_and_unknown_version);
    RUN_TEST(test_replay_verdicts);
    RUN_TEST(test_weather_report_round_trip_and_short_payload);
    return UNITY_END();
}
