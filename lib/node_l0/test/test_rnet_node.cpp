#include <cstring>

#include "node_l0/rnet_node.hpp"
#include "unity.h"

namespace
{

constexpr uint32_t eeprom_base = 0x08080000u;
constexpr uint32_t eeprom_size = 2048u;
constexpr uint8_t weather_acurite = 0x01;

uint32_t eeprom_words[eeprom_size / 4];
bool eeprom_writes_fail;

uint32_t read_word(void *, uint32_t address)
{
    return eeprom_words[(address - eeprom_base) / 4u];
}

bool write_word(void *, uint32_t address, uint32_t word)
{
    if (eeprom_writes_fail) {
        return false;
    }
    eeprom_words[(address - eeprom_base) / 4u] = word;
    return true;
}

const nl0_ee_backend_t eeprom = {read_word, write_word, nullptr};

void erase_eeprom()
{
    std::memset(eeprom_words, 0, sizeof eeprom_words);
}

} // namespace

void setUp()
{
    erase_eeprom();
    eeprom_writes_fail = false;
}
void tearDown() {}

using namespace nl0;

void test_blank_eeprom_gives_test_identity()
{
    node_identity identity =
        load_identity_or_test_identity(eeprom, eeprom_base, weather_acurite, 0x12345678u);
    TEST_ASSERT_FALSE(identity.provisioned);
    TEST_ASSERT_EQUAL_HEX16(0x1234 ^ 0x5678, identity.node_id);
    TEST_ASSERT_EQUAL_HEX8(weather_acurite, identity.node_type);
    const rnet::aes_key expected_key =
        rnet::derive_node_key(rnet::test_master_key, identity.node_id, weather_acurite);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected_key.data(), identity.key.data(), 16);

    identity = load_identity_or_test_identity(eeprom, eeprom_base, weather_acurite, 0xFFFF0000u);
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, identity.node_id);
}

void test_provisioned_record_is_used_and_corruption_detected()
{
    node_identity provisioned;
    provisioned.node_id = 0x0042;
    provisioned.node_type = 0x03;
    provisioned.net_id = 0x5A;
    for (std::size_t i = 0; i < provisioned.key.size(); i++) {
        provisioned.key[i] = static_cast<uint8_t>(0xA0 + i);
    }
    const auto record = identity_record(provisioned);
    std::memcpy(eeprom_words, record.data(), sizeof record);

    node_identity loaded = load_identity_or_test_identity(eeprom, eeprom_base, weather_acurite, 0);
    TEST_ASSERT_TRUE(loaded.provisioned);
    TEST_ASSERT_EQUAL_HEX16(0x0042, loaded.node_id);
    TEST_ASSERT_EQUAL_HEX8(0x03, loaded.node_type);
    TEST_ASSERT_EQUAL_HEX8(0x5A, loaded.net_id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(provisioned.key.data(), loaded.key.data(), 16);

    eeprom_words[3] ^= 1u;
    loaded = load_identity_or_test_identity(eeprom, eeprom_base, weather_acurite, 7);
    TEST_ASSERT_FALSE(loaded.provisioned);
    TEST_ASSERT_EQUAL_HEX16(7, loaded.node_id);
}

void test_counter_never_repeats_across_resets()
{
    uplink_counter counter;
    counter.load(eeprom, eeprom_base);
    uint32_t value = 0;
    for (uint32_t expected = 0; expected <= 300u; expected++) {
        TEST_ASSERT_TRUE(counter.take_next(value));
        TEST_ASSERT_EQUAL_UINT32(expected, value);
    }
    counter.load(eeprom, eeprom_base);
    TEST_ASSERT_TRUE(counter.take_next(value));
    TEST_ASSERT_EQUAL_UINT32(512u, value);

    counter.load(eeprom, eeprom_base);
    TEST_ASSERT_TRUE(counter.take_next(value));
    TEST_ASSERT_EQUAL_UINT32(768u, value);
}

void test_counter_refuses_when_the_required_store_fails()
{
    uplink_counter counter;
    counter.load(eeprom, eeprom_base);
    uint32_t value = 99;
    eeprom_writes_fail = true;
    TEST_ASSERT_FALSE(counter.take_next(value));
    eeprom_writes_fail = false;
    TEST_ASSERT_TRUE(counter.take_next(value));
    TEST_ASSERT_EQUAL_UINT32(0, value);
    eeprom_writes_fail = true;
    TEST_ASSERT_TRUE(counter.take_next(value));
    TEST_ASSERT_EQUAL_UINT32(1, value);
}

void test_layout_fits_the_rnet_region()
{
    TEST_ASSERT_TRUE(identity_offset + identity_words * 4u <= counter_ring_offset);
    TEST_ASSERT_TRUE(counter_ring_offset + nl0_ee_ring_size(counter_ring_slots, 1) <=
                     rnet_region_end);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_eeprom_gives_test_identity);
    RUN_TEST(test_provisioned_record_is_used_and_corruption_detected);
    RUN_TEST(test_counter_never_repeats_across_resets);
    RUN_TEST(test_counter_refuses_when_the_required_store_fails);
    RUN_TEST(test_layout_fits_the_rnet_region);
    return UNITY_END();
}
