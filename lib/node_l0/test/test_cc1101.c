/* CC1101 driver against a simulated chip that decodes the SPI byte stream
 * (header byte, single/burst, FIFO, strobes, status registers) and models
 * IDLE/RX/TX/SLEEP enough to check what the driver writes and when. */
#include <string.h>

#include "node_l0/cc1101.h"
#include "rnet_phy.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

#define M_IDLE 0x01u
#define M_RX 0x0Du
#define M_TX 0x13u

typedef struct {
    uint8_t regs[0x30];
    uint8_t patable;
    uint8_t marc;
    bool sleeping, wake_pending;
    int ready_delay; /* polls of MISO high after a select while asleep */
    bool selected;
    int nbytes; /* bytes since select */
    uint8_t header, addr;
    /* TX */
    uint8_t txfifo[64];
    int ntx;
    int tx_polls;
    uint8_t sent[64];
    int nsent;
    int sends;
    /* RX */
    uint8_t rxfifo[70];
    int nrx, rxpos;
    int rx_polls;
    uint8_t inject[64];
    int ninject;
    uint8_t inject_rssi;
    bool inject_bad_crc;
    uint32_t elapsed_us;
} sim_t;

static sim_t S;

static void sim_select(void *ctx, bool on)
{
    (void)ctx;
    if (on && !S.selected) {
        S.nbytes = 0;
        if (S.sleeping) {
            S.sleeping = false;
            S.ready_delay = 3;
            /* SLEEP loses TEST0..2 and PATABLE (SWRS061I 19.5) */
            S.regs[0x2C] = S.regs[0x2D] = S.regs[0x2E] = 0;
            S.patable = 0;
        }
    }
    if (!on && S.selected && S.wake_pending) {
        S.wake_pending = false;
        S.sleeping = true; /* SPWD takes effect when CSn goes high */
    }
    S.selected = on;
}

static bool sim_miso(void *ctx)
{
    (void)ctx;
    if (S.ready_delay > 0) {
        S.ready_delay--;
        return true;
    }
    return false;
}

static void sim_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    S.elapsed_us += us;
}

static void strobe(uint8_t cmd)
{
    switch (cmd) {
    case 0x30: /* SRES */
        memset(S.regs, 0, sizeof S.regs);
        S.patable = 0xC6;
        S.marc = M_IDLE;
        break;
    case 0x34: /* SRX */
        S.marc = M_RX;
        S.rx_polls = S.ninject ? 3 : -1;
        break;
    case 0x35: /* STX */
        S.marc = M_TX;
        S.tx_polls = 3;
        break;
    case 0x36: /* SIDLE */
        S.marc = M_IDLE;
        break;
    case 0x39: /* SPWD */
        S.wake_pending = true;
        break;
    case 0x3A: /* SFRX */
        S.nrx = S.rxpos = 0;
        break;
    case 0x3B: /* SFTX */
        S.ntx = 0;
        break;
    default:
        break;
    }
}

static uint8_t status_reg(uint8_t a)
{
    switch (a) {
    case 0x30:
        return 0x00; /* PARTNUM */
    case 0x31:
        return 0x14; /* VERSION */
    case 0x35:       /* MARCSTATE: advance the simulated radio on each poll */
        if (S.marc == M_TX && --S.tx_polls == 0) {
            memcpy(S.sent, S.txfifo, (size_t)S.ntx);
            S.nsent = S.ntx;
            S.sends++;
            S.ntx = 0;
            S.marc = M_IDLE;
        }
        if (S.marc == M_RX && S.rx_polls > 0 && --S.rx_polls == 0) {
            S.marc = M_IDLE;
            if (!S.inject_bad_crc) { /* autoflush drops bad-CRC packets */
                S.rxfifo[0] = (uint8_t)S.ninject;
                memcpy(&S.rxfifo[1], S.inject, (size_t)S.ninject);
                S.rxfifo[1 + S.ninject] = S.inject_rssi;
                S.rxfifo[2 + S.ninject] = 0x80 | 0x12;
                S.nrx = S.ninject + 3;
                S.rxpos = 0;
            }
            S.ninject = 0;
        }
        return S.marc;
    case 0x3B:
        return (uint8_t)(S.nrx - S.rxpos);
    default:
        return 0;
    }
}

static uint8_t sim_xfer(void *ctx, uint8_t out)
{
    (void)ctx;
    if (S.nbytes++ == 0) {
        S.header = out;
        S.addr = out & 0x3F;
        bool burst = out & 0x40;
        if (S.addr >= 0x30 && S.addr <= 0x3D && !burst) {
            strobe(S.addr);
        }
        return (uint8_t)(S.marc << 4);
    }
    bool rd = S.header & 0x80, burst = S.header & 0x40;
    uint8_t a = S.addr;
    if (burst && a < 0x30) {
        S.addr++;
    }
    if (a == 0x3F) {
        if (rd) {
            return S.rxfifo[S.rxpos++];
        }
        S.txfifo[S.ntx++] = out;
        return 0;
    }
    if (a == 0x3E) {
        if (!rd) {
            S.patable = out;
        }
        return S.patable;
    }
    if (a >= 0x30) {
        return status_reg(a);
    }
    if (rd) {
        return S.regs[a];
    }
    S.regs[a] = out;
    return 0;
}

static const nl0_cc1101_bus_t BUS = {sim_select, sim_xfer, sim_miso, sim_delay, 0};

static nl0_cc1101_t init_radio(void)
{
    memset(&S, 0, sizeof S);
    nl0_cc1101_t r;
    TEST_ASSERT_EQUAL(NL0_CC1101_OK, nl0_cc1101_init(&r, &BUS, 0x1E));
    return r;
}

static void test_init_writes_table(void)
{
    nl0_cc1101_t r = init_radio();
    for (uint32_t i = 0; i < RNET_CC1101_NREGS; i++) {
        TEST_ASSERT_EQUAL_HEX8(rnet_cc1101_regs[i][1], S.regs[rnet_cc1101_regs[i][0]]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x1E, S.patable);
    TEST_ASSERT_EQUAL_HEX8(0x14, r.version);
    TEST_ASSERT_EQUAL_HEX8(0xB5, S.regs[0x04]); /* sync word from the schema */
    TEST_ASSERT_EQUAL_HEX8(0x4E, S.regs[0x05]);
}

static void test_no_chip(void)
{
    memset(&S, 0, sizeof S);
    S.ready_delay = 1000000; /* MISO never goes low */
    nl0_cc1101_t r;
    TEST_ASSERT_EQUAL(NL0_CC1101_NOT_READY, nl0_cc1101_init(&r, &BUS, 0x1E));
}

static void test_send_frame(void)
{
    nl0_cc1101_t r = init_radio();
    const uint8_t msg[] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL(NL0_CC1101_OK, nl0_cc1101_send(&r, msg, sizeof msg));
    TEST_ASSERT_EQUAL(1, S.sends);
    TEST_ASSERT_EQUAL(6, S.nsent);
    TEST_ASSERT_EQUAL_HEX8(5, S.sent[0]); /* length byte */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, &S.sent[1], 5);
    TEST_ASSERT_EQUAL(M_IDLE, S.marc);
}

static void test_send_rejects_bad_length(void)
{
    nl0_cc1101_t r = init_radio();
    uint8_t big[62] = {0};
    TEST_ASSERT_EQUAL(NL0_CC1101_TOO_LONG, nl0_cc1101_send(&r, big, 62));
    TEST_ASSERT_EQUAL(NL0_CC1101_TOO_LONG, nl0_cc1101_send(&r, big, 0));
    TEST_ASSERT_EQUAL(0, S.sends);
}

static void test_sleep_then_send_restores_lost_regs(void)
{
    nl0_cc1101_t r = init_radio();
    nl0_cc1101_sleep(&r);
    TEST_ASSERT_TRUE(S.sleeping);
    const uint8_t msg[] = {0xAA};
    TEST_ASSERT_EQUAL(NL0_CC1101_OK, nl0_cc1101_send(&r, msg, 1));
    TEST_ASSERT_EQUAL_HEX8(0x1E, S.patable);
    TEST_ASSERT_EQUAL_HEX8(0x81, S.regs[0x2C]); /* TEST2 */
    TEST_ASSERT_EQUAL_HEX8(0x35, S.regs[0x2D]); /* TEST1 */
    TEST_ASSERT_EQUAL_HEX8(0x09, S.regs[0x2E]); /* TEST0 */
    TEST_ASSERT_EQUAL(1, S.sends);
}

static void test_recv_packet(void)
{
    nl0_cc1101_t r = init_radio();
    const uint8_t msg[] = {9, 8, 7};
    memcpy(S.inject, msg, 3);
    S.ninject = 3;
    S.inject_rssi = 0xD0; /* -48 raw -> -24 - 74 = -98 dBm */
    uint8_t buf[61];
    nl0_cc1101_rx_t rx;
    TEST_ASSERT_EQUAL(NL0_CC1101_OK, nl0_cc1101_recv(&r, buf, &rx, 120));
    TEST_ASSERT_EQUAL(3, rx.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, buf, 3);
    TEST_ASSERT_EQUAL_INT16(-98, rx.rssi);
    TEST_ASSERT_EQUAL_UINT8(0x12, rx.lqi);
}

static void test_recv_timeout_and_bad_crc(void)
{
    nl0_cc1101_t r = init_radio();
    uint8_t buf[61];
    nl0_cc1101_rx_t rx;
    S.elapsed_us = 0;
    TEST_ASSERT_EQUAL(NL0_CC1101_TIMEOUT, nl0_cc1101_recv(&r, buf, &rx, 120));
    TEST_ASSERT_UINT32_WITHIN(1000, 120000, S.elapsed_us);
    TEST_ASSERT_EQUAL(M_IDLE, S.marc);

    S.ninject = 2;
    S.inject_bad_crc = true;
    TEST_ASSERT_EQUAL(NL0_CC1101_TIMEOUT, nl0_cc1101_recv(&r, buf, &rx, 50));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_writes_table);
    RUN_TEST(test_no_chip);
    RUN_TEST(test_send_frame);
    RUN_TEST(test_send_rejects_bad_length);
    RUN_TEST(test_sleep_then_send_restores_lost_regs);
    RUN_TEST(test_recv_packet);
    RUN_TEST(test_recv_timeout_and_bad_crc);
    return UNITY_END();
}
