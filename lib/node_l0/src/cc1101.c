#include "node_l0/cc1101.h"

#include "rnet_phy.h"

/* Header byte (SWRS061I 10.1): bit 7 read, bit 6 burst. */
#define RD 0x80u
#define BURST 0x40u
#define FIFO 0x3Fu
#define PATABLE 0x3Eu

/* Command strobes (SWRS061I table 42) */
#define SRES 0x30u
#define SRX 0x34u
#define STX 0x35u
#define SIDLE 0x36u
#define SPWD 0x39u
#define SFRX 0x3Au
#define SFTX 0x3Bu

/* MARCSTATE values (table 47) */
#define MS_IDLE 0x01u
#define MS_RXFIFO_OVERFLOW 0x11u
#define MS_TXFIFO_UNDERFLOW 0x16u

#define READY_POLLS 500u     /* x 10 µs: crystal start is ~150 µs typical */
#define TX_TIMEOUT_US 60000u /* 61 bytes at 38.4 kbps ~ 15 ms, plus calibration */
#define POLL_US 250u

static bool wait_ready(const nl0_cc1101_bus_t *b)
{
    for (uint32_t i = 0; i < READY_POLLS; i++) {
        if (!b->miso(b->ctx)) {
            return true;
        }
        b->delay_us(b->ctx, 10u);
    }
    return false;
}

static uint8_t access(nl0_cc1101_t *r, uint8_t header, uint8_t value)
{
    const nl0_cc1101_bus_t *b = r->bus;
    b->select(b->ctx, true);
    (void)wait_ready(b);
    (void)b->xfer(b->ctx, header);
    uint8_t v = b->xfer(b->ctx, value);
    b->select(b->ctx, false);
    return v;
}

uint8_t nl0_cc1101_read(nl0_cc1101_t *r, uint8_t addr)
{
    return access(r, (uint8_t)(addr | RD), 0u);
}

uint8_t nl0_cc1101_status(nl0_cc1101_t *r, uint8_t addr)
{
    return access(r, (uint8_t)(addr | RD | BURST), 0u);
}

void nl0_cc1101_write(nl0_cc1101_t *r, uint8_t addr, uint8_t value)
{
    (void)access(r, addr, value);
}

uint8_t nl0_cc1101_strobe(nl0_cc1101_t *r, uint8_t cmd)
{
    const nl0_cc1101_bus_t *b = r->bus;
    b->select(b->ctx, true);
    (void)wait_ready(b);
    uint8_t st = b->xfer(b->ctx, cmd);
    b->select(b->ctx, false);
    return st;
}

static void burst(nl0_cc1101_t *r, uint8_t header, uint8_t *data, uint8_t n, bool rd)
{
    const nl0_cc1101_bus_t *b = r->bus;
    b->select(b->ctx, true);
    (void)wait_ready(b);
    (void)b->xfer(b->ctx, header);
    for (uint8_t i = 0; i < n; i++) {
        uint8_t v = b->xfer(b->ctx, rd ? 0u : data[i]);
        if (rd) {
            data[i] = v;
        }
    }
    b->select(b->ctx, false);
}

/* RXBYTES can be read mid-update (errata SWRZ020): read until two agree. */
static uint8_t rxbytes(nl0_cc1101_t *r)
{
    uint8_t a, b = nl0_cc1101_status(r, NL0_CC1101_RXBYTES);
    do {
        a = b;
        b = nl0_cc1101_status(r, NL0_CC1101_RXBYTES);
    } while (a != b);
    return (uint8_t)(a & 0x7Fu);
}

static uint8_t marcstate(nl0_cc1101_t *r)
{
    return (uint8_t)(nl0_cc1101_status(r, NL0_CC1101_MARCSTATE) & 0x1Fu);
}

/* TEST2..TEST0 and PATABLE are not kept in SLEEP: write them after a wake. */
static void write_volatile_regs(nl0_cc1101_t *r)
{
    for (uint32_t i = 0; i < RNET_CC1101_NREGS; i++) {
        if (rnet_cc1101_regs[i][0] >= 0x2Cu) {
            nl0_cc1101_write(r, rnet_cc1101_regs[i][0], rnet_cc1101_regs[i][1]);
        }
    }
    nl0_cc1101_write(r, PATABLE, r->patable);
}

static bool wake(nl0_cc1101_t *r)
{
    if (!r->asleep) {
        return true;
    }
    const nl0_cc1101_bus_t *b = r->bus;
    b->select(b->ctx, true);
    bool ok = wait_ready(b);
    b->select(b->ctx, false);
    if (ok) {
        r->asleep = false;
        write_volatile_regs(r);
    }
    return ok;
}

nl0_cc1101_err_t nl0_cc1101_init(nl0_cc1101_t *r, const nl0_cc1101_bus_t *bus, uint8_t patable)
{
    r->bus = bus;
    r->patable = patable;
    r->asleep = false;
    r->version = 0;

    /* Manual power-on reset (SWRS061I 19.1.2): strobe CSn, then SRES. */
    bus->select(bus->ctx, true);
    bus->delay_us(bus->ctx, 10u);
    bus->select(bus->ctx, false);
    bus->delay_us(bus->ctx, 40u);
    bus->select(bus->ctx, true);
    if (!wait_ready(bus)) {
        bus->select(bus->ctx, false);
        return NL0_CC1101_NOT_READY;
    }
    (void)bus->xfer(bus->ctx, SRES);
    bus->delay_us(bus->ctx, 100u); /* CHIP_RDYn goes high during the reset */
    bool ok = wait_ready(bus);
    bus->select(bus->ctx, false);
    if (!ok) {
        return NL0_CC1101_NOT_READY;
    }

    uint8_t part = nl0_cc1101_status(r, NL0_CC1101_PARTNUM);
    r->version = nl0_cc1101_status(r, NL0_CC1101_VERSION);
    if (part != 0x00u || r->version == 0x00u || r->version == 0xFFu) {
        return NL0_CC1101_WRONG_PART; /* 0x00/0xFF: no chip answering */
    }
    for (uint32_t i = 0; i < RNET_CC1101_NREGS; i++) {
        nl0_cc1101_write(r, rnet_cc1101_regs[i][0], rnet_cc1101_regs[i][1]);
    }
    nl0_cc1101_write(r, PATABLE, patable);
    (void)nl0_cc1101_strobe(r, SIDLE);
    return NL0_CC1101_OK;
}

nl0_cc1101_err_t nl0_cc1101_send(nl0_cc1101_t *r, const uint8_t *data, uint8_t len)
{
    if (len == 0u || len > RNET_PHY_MAX_PAYLOAD) {
        return NL0_CC1101_TOO_LONG;
    }
    if (!wake(r)) {
        return NL0_CC1101_NOT_READY;
    }
    (void)nl0_cc1101_strobe(r, SIDLE);
    (void)nl0_cc1101_strobe(r, SFTX);

    uint8_t frame[1u + RNET_PHY_MAX_PAYLOAD];
    frame[0] = len;
    for (uint8_t i = 0; i < len; i++) {
        frame[1u + i] = data[i];
    }
    burst(r, (uint8_t)(FIFO | BURST), frame, (uint8_t)(len + 1u), false);
    (void)nl0_cc1101_strobe(r, STX);

    for (uint32_t t = 0; t < TX_TIMEOUT_US; t += POLL_US) {
        r->bus->delay_us(r->bus->ctx, POLL_US);
        uint8_t ms = marcstate(r);
        if (ms == MS_IDLE) {
            return NL0_CC1101_OK;
        }
        if (ms == MS_TXFIFO_UNDERFLOW) {
            break;
        }
    }
    (void)nl0_cc1101_strobe(r, SIDLE);
    (void)nl0_cc1101_strobe(r, SFTX);
    return NL0_CC1101_TIMEOUT;
}

nl0_cc1101_err_t nl0_cc1101_recv(nl0_cc1101_t *r, uint8_t *buf, nl0_cc1101_rx_t *out,
                                 uint32_t timeout_ms)
{
    if (!wake(r)) {
        return NL0_CC1101_NOT_READY;
    }
    (void)nl0_cc1101_strobe(r, SIDLE);
    (void)nl0_cc1101_strobe(r, SFRX);
    (void)nl0_cc1101_strobe(r, SRX);

    const uint32_t limit = timeout_ms * 1000u;
    for (uint32_t t = 0; t < limit; t += POLL_US) {
        r->bus->delay_us(r->bus->ctx, POLL_US);
        uint8_t ms = marcstate(r);
        if (ms == MS_RXFIFO_OVERFLOW) {
            (void)nl0_cc1101_strobe(r, SFRX);
            (void)nl0_cc1101_strobe(r, SRX);
            continue;
        }
        if (ms != MS_IDLE) {
            continue; /* still listening or receiving */
        }
        /* RX ended (MCSM1: RXOFF -> IDLE). A bad CRC was auto-flushed. */
        uint8_t n = rxbytes(r);
        if (n >= 3u) {
            uint8_t len = 0;
            burst(r, (uint8_t)(FIFO | RD | BURST), &len, 1u, true);
            if (len >= 1u && len <= RNET_PHY_MAX_PAYLOAD && n >= (uint8_t)(len + 3u)) {
                uint8_t st[2];
                burst(r, (uint8_t)(FIFO | RD | BURST), buf, len, true);
                burst(r, (uint8_t)(FIFO | RD | BURST), st, 2u, true);
                if (st[1] & 0x80u) { /* CRC_OK */
                    int16_t raw = (int16_t)(st[0] >= 128u ? st[0] - 256 : st[0]);
                    out->len = len;
                    out->rssi = (int16_t)(raw / 2 - RNET_CC1101_RSSI_OFFSET);
                    out->lqi = (uint8_t)(st[1] & 0x7Fu);
                    (void)nl0_cc1101_strobe(r, SFRX);
                    return NL0_CC1101_OK;
                }
            }
        }
        (void)nl0_cc1101_strobe(r, SFRX);
        (void)nl0_cc1101_strobe(r, SRX);
    }
    (void)nl0_cc1101_strobe(r, SIDLE);
    (void)nl0_cc1101_strobe(r, SFRX);
    return NL0_CC1101_TIMEOUT;
}

void nl0_cc1101_sleep(nl0_cc1101_t *r)
{
    if (r->asleep) {
        return;
    }
    (void)nl0_cc1101_strobe(r, SIDLE);
    (void)nl0_cc1101_strobe(r, SPWD); /* SLEEP once CSn goes high */
    r->asleep = true;
}
