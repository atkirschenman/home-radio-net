/* Identity record and uplink counter over an in-memory EEPROM. */
#include <string.h>

#include "node_l0/rnet_node.h"
#include "rnet/rnet.h"
#include "unity.h"

#define BASE 0x08080000u
#define SIZE 2048u

static uint32_t mem[SIZE / 4];
static bool fail_writes;

static uint32_t rd(void *ctx, uint32_t addr)
{
    (void)ctx;
    return mem[(addr - BASE) / 4u];
}

static bool wr(void *ctx, uint32_t addr, uint32_t word)
{
    (void)ctx;
    if (fail_writes) {
        return false;
    }
    mem[(addr - BASE) / 4u] = word;
    return true;
}

static const nl0_ee_backend_t be = {rd, wr, 0};

void setUp(void)
{
    memset(mem, 0, sizeof mem); /* erased L0 data EEPROM reads 0 */
    fail_writes = false;
}
void tearDown(void) {}

static void test_blank_gives_test_identity(void)
{
    nl0_identity_t id;
    nl0_identity_load(&be, BASE, 0x01, 0x12345678u, &id);
    TEST_ASSERT_FALSE(id.provisioned);
    TEST_ASSERT_EQUAL_HEX16(0x1234 ^ 0x5678, id.node_id);
    TEST_ASSERT_EQUAL_HEX8(0x01, id.node_type);
    uint8_t k[16];
    rnet_derive_key(nl0_test_master_key, id.node_id, 0x01, k);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(k, id.key, 16);

    nl0_identity_load(&be, BASE, 0x01, 0xFFFF0000u, &id); /* would be 0xFFFF: reserved */
    TEST_ASSERT_EQUAL_HEX16(0xFFFE, id.node_id);
}

static void test_provisioned_record(void)
{
    nl0_identity_t in = {0x0042, 0x03, 0x5A, {0}, true};
    for (int i = 0; i < 16; i++) {
        in.key[i] = (uint8_t)(0xA0 + i);
    }
    uint32_t w[NL0_ID_WORDS];
    nl0_identity_record(&in, w);
    memcpy(mem, w, sizeof w);

    nl0_identity_t out;
    nl0_identity_load(&be, BASE, 0x01, 0, &out);
    TEST_ASSERT_TRUE(out.provisioned);
    TEST_ASSERT_EQUAL_HEX16(0x0042, out.node_id);
    TEST_ASSERT_EQUAL_HEX8(0x03, out.node_type); /* the record wins over the build's type */
    TEST_ASSERT_EQUAL_HEX8(0x5A, out.net_id);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(in.key, out.key, 16);

    mem[3] ^= 1u; /* corrupt one key word: CRC fails -> test identity */
    nl0_identity_load(&be, BASE, 0x01, 0x00000007u, &out);
    TEST_ASSERT_FALSE(out.provisioned);
    TEST_ASSERT_EQUAL_HEX16(7, out.node_id);
}

static void test_counter_never_repeats_across_resets(void)
{
    nl0_counter_t c;
    nl0_counter_init(&c, &be, BASE);
    uint32_t ctr = 0;
    for (uint32_t i = 0; i <= 300u; i++) {
        TEST_ASSERT_TRUE(nl0_counter_take(&c, &ctr));
        TEST_ASSERT_EQUAL_UINT32(i, ctr);
    }
    nl0_counter_init(&c, &be, BASE); /* reset after sending 300 */
    TEST_ASSERT_TRUE(nl0_counter_take(&c, &ctr));
    TEST_ASSERT_EQUAL_UINT32(512u, ctr); /* stored 256, resumes at 256 + 256 */

    nl0_counter_init(&c, &be, BASE); /* reset straight away: 512 was stored first */
    TEST_ASSERT_TRUE(nl0_counter_take(&c, &ctr));
    TEST_ASSERT_EQUAL_UINT32(768u, ctr);
}

static void test_counter_refuses_when_store_fails(void)
{
    nl0_counter_t c;
    nl0_counter_init(&c, &be, BASE);
    fail_writes = true;
    uint32_t ctr = 99;
    TEST_ASSERT_FALSE(nl0_counter_take(&c, &ctr)); /* 0 must be stored before use */
    fail_writes = false;
    TEST_ASSERT_TRUE(nl0_counter_take(&c, &ctr));
    TEST_ASSERT_EQUAL_UINT32(0, ctr);
    fail_writes = true;
    TEST_ASSERT_TRUE(nl0_counter_take(&c, &ctr)); /* 1: no store due */
}

static void test_layout_fits_region(void)
{
    TEST_ASSERT_TRUE(NL0_ID_OFFSET + NL0_ID_WORDS * 4u <= NL0_CTR_OFFSET);
    TEST_ASSERT_TRUE(NL0_CTR_OFFSET + nl0_ee_ring_size(NL0_CTR_SLOTS, 1) <= NL0_RNET_REGION_END);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_gives_test_identity);
    RUN_TEST(test_provisioned_record);
    RUN_TEST(test_counter_never_repeats_across_resets);
    RUN_TEST(test_counter_refuses_when_store_fails);
    RUN_TEST(test_layout_fits_region);
    return UNITY_END();
}
