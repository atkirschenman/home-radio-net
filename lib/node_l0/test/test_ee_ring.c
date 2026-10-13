/* Host tests for the EEPROM wear-levelled ring: blank start, newest-wins,
 * wrap-around, torn writes at every word, write errors, corrupt slots. */
#include <string.h>

#include "node_l0/ee_ring.h"
#include "unity.h"

#define MEM_WORDS 256u
#define BASE 0x08080000u

typedef struct {
    uint32_t mem[MEM_WORDS];
    int writes_left;   /* < 0: unlimited; 0: the next write "tears" (is lost and fails) */
    uint32_t fail_addr; /* writes to this address fail (0 = none) */
    uint32_t write_count;
} fake_ee_t;

static uint32_t fake_read(void *ctx, uint32_t addr)
{
    fake_ee_t *f = ctx;
    return f->mem[(addr - BASE) / 4u];
}

static bool fake_write(void *ctx, uint32_t addr, uint32_t word)
{
    fake_ee_t *f = ctx;
    if (f->writes_left == 0 || addr == f->fail_addr) {
        return false;
    }
    if (f->writes_left > 0) {
        f->writes_left--;
    }
    f->mem[(addr - BASE) / 4u] = word;
    f->write_count++;
    return true;
}

static fake_ee_t ee;
static const nl0_ee_backend_t be = {fake_read, fake_write, &ee};

static nl0_ee_ring_t make_ring(uint16_t slots, uint8_t words)
{
    nl0_ee_ring_t r = {.be = &be, .base = BASE, .slots = slots, .words = words};
    return r;
}

void setUp(void)
{
    memset(&ee, 0, sizeof ee); /* erased L0 data EEPROM reads 0 */
    ee.writes_left = -1;
}

void tearDown(void)
{
}

static void test_blank_ring_loads_nothing(void)
{
    nl0_ee_ring_t r = make_ring(8, 2);
    uint32_t p[2] = {0xAAAAAAAAu, 0xBBBBBBBBu};
    TEST_ASSERT_FALSE(nl0_ee_ring_load(&r, p));
    TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAu, p[0]); /* untouched */
    TEST_ASSERT_EQUAL_UINT32(1u, r.seq);
    TEST_ASSERT_EQUAL_UINT16(0u, r.next);
}

static void test_store_then_load_after_reset(void)
{
    nl0_ee_ring_t r = make_ring(8, 2);
    uint32_t p[2];
    nl0_ee_ring_load(&r, p);
    uint32_t v[2] = {12345u, 0xDEADBEEFu};
    TEST_ASSERT_TRUE(nl0_ee_ring_store(&r, v));

    nl0_ee_ring_t r2 = make_ring(8, 2); /* "reset": fresh RAM state */
    TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
    TEST_ASSERT_EQUAL_UINT32(12345u, p[0]);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, p[1]);
    TEST_ASSERT_EQUAL_UINT16(1u, r2.next);
}

static void test_newest_wins_across_many_wraps(void)
{
    nl0_ee_ring_t r = make_ring(5, 1);
    uint32_t p[1];
    nl0_ee_ring_load(&r, p);
    for (uint32_t i = 1; i <= 23; i++) {
        TEST_ASSERT_TRUE(nl0_ee_ring_store(&r, &i));
        nl0_ee_ring_t r2 = make_ring(5, 1);
        TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
        TEST_ASSERT_EQUAL_UINT32(i, p[0]);
        TEST_ASSERT_EQUAL_UINT16(r.next, r2.next);
        TEST_ASSERT_EQUAL_UINT32(r.seq, r2.seq);
    }
}

static void test_writes_spread_over_all_slots(void)
{
    nl0_ee_ring_t r = make_ring(4, 1);
    uint32_t p[1];
    nl0_ee_ring_load(&r, p);
    for (uint32_t i = 0; i < 40; i++) {
        uint32_t v = i;
        nl0_ee_ring_store(&r, &v);
    }
    /* each slot's seq word was written 10 times: every slot holds a record */
    for (uint16_t s = 0; s < 4; s++) {
        TEST_ASSERT_NOT_EQUAL_UINT32(0u, ee.mem[s * 3u]);
    }
}

static void test_torn_write_keeps_previous_value(void)
{
    /* Tear the 2nd store after k words, for every k: the load must return
     * the first value, never garbage. */
    const uint32_t words_per_store = 1u + 3u + 1u; /* invalidate + seq,2 payload,crc */
    for (uint32_t k = 0; k < words_per_store; k++) {
        setUp();
        nl0_ee_ring_t r = make_ring(3, 2);
        uint32_t p[2];
        nl0_ee_ring_load(&r, p);
        uint32_t a[2] = {111u, 222u};
        TEST_ASSERT_TRUE(nl0_ee_ring_store(&r, a));
        ee.writes_left = (int)k;
        uint32_t b[2] = {333u, 444u};
        TEST_ASSERT_FALSE(nl0_ee_ring_store(&r, b));

        ee.writes_left = -1;
        nl0_ee_ring_t r2 = make_ring(3, 2);
        TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
        TEST_ASSERT_EQUAL_UINT32(111u, p[0]);
        TEST_ASSERT_EQUAL_UINT32(222u, p[1]);
    }
}

static void test_torn_overwrite_of_old_slot(void)
{
    /* Fill a 2-slot ring so the next store overwrites the older record, then
     * tear that overwrite: the newer surviving record must win. */
    nl0_ee_ring_t r = make_ring(2, 1);
    uint32_t p[1];
    nl0_ee_ring_load(&r, p);
    uint32_t v1 = 1, v2 = 2, v3 = 3;
    nl0_ee_ring_store(&r, &v1);
    nl0_ee_ring_store(&r, &v2);
    ee.writes_left = 2; /* invalidate + seq, payload lost */
    TEST_ASSERT_FALSE(nl0_ee_ring_store(&r, &v3));
    ee.writes_left = -1;
    nl0_ee_ring_t r2 = make_ring(2, 1);
    TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
    TEST_ASSERT_EQUAL_UINT32(2u, p[0]);
}

static void test_failed_slot_is_skipped(void)
{
    nl0_ee_ring_t r = make_ring(4, 1);
    uint32_t p[1];
    nl0_ee_ring_load(&r, p);
    ee.fail_addr = BASE + 3u * 4u; /* slot 1, word 0 (seq) is worn out */
    uint32_t v = 10;
    TEST_ASSERT_TRUE(nl0_ee_ring_store(&r, &v));  /* slot 0 */
    v = 11;
    TEST_ASSERT_FALSE(nl0_ee_ring_store(&r, &v)); /* slot 1 fails */
    v = 12;
    TEST_ASSERT_TRUE(nl0_ee_ring_store(&r, &v));  /* slot 2 */
    nl0_ee_ring_t r2 = make_ring(4, 1);
    TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
    TEST_ASSERT_EQUAL_UINT32(12u, p[0]);
}

static void test_bit_flip_rejects_slot(void)
{
    nl0_ee_ring_t r = make_ring(3, 1);
    uint32_t p[1];
    nl0_ee_ring_load(&r, p);
    uint32_t v = 7;
    nl0_ee_ring_store(&r, &v);
    v = 8;
    nl0_ee_ring_store(&r, &v);
    ee.mem[1u * 3u + 1u] ^= 0x00010000u; /* corrupt the payload of slot 1 */
    nl0_ee_ring_t r2 = make_ring(3, 1);
    TEST_ASSERT_TRUE(nl0_ee_ring_load(&r2, p));
    TEST_ASSERT_EQUAL_UINT32(7u, p[0]);
}

static void test_crc32_known_value(void)
{
    /* CRC-32/IEEE of the bytes "1234" (0x34333231 little-endian) */
    uint32_t w = 0x34333231u;
    TEST_ASSERT_EQUAL_HEX32(0x9BE3E0A3u, nl0_crc32(0u, &w, 1));
}

static void test_size(void)
{
    TEST_ASSERT_EQUAL_UINT32(16u * 4u * 4u, nl0_ee_ring_size(16, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_blank_ring_loads_nothing);
    RUN_TEST(test_store_then_load_after_reset);
    RUN_TEST(test_newest_wins_across_many_wraps);
    RUN_TEST(test_writes_spread_over_all_slots);
    RUN_TEST(test_torn_write_keeps_previous_value);
    RUN_TEST(test_torn_overwrite_of_old_slot);
    RUN_TEST(test_failed_slot_is_skipped);
    RUN_TEST(test_bit_flip_rejects_slot);
    RUN_TEST(test_crc32_known_value);
    RUN_TEST(test_size);
    return UNITY_END();
}
