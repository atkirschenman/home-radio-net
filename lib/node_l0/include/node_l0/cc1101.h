/* CC1101 driver for the shared PHY (01-architecture.md section 3; register
 * table generated into lib/rnet/gen/rnet_phy.h). Blocking, polled, no
 * interrupts: a node sends a few frames a minute and listens 120 ms an hour.
 *
 * The board supplies the SPI bus as callbacks (chip select, one-byte
 * transfer, MISO level for CHIP_RDYn, microsecond delay), so this file is
 * portable and is host-tested against a simulated CC1101. */
#ifndef NODE_L0_CC1101_H
#define NODE_L0_CC1101_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*select)(void *ctx, bool on); /* on = CSn low */
    uint8_t (*xfer)(void *ctx, uint8_t out);
    bool (*miso)(void *ctx); /* SO level while selected: low = CHIP_RDYn (crystal running) */
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} nl0_cc1101_bus_t;

typedef enum {
    NL0_CC1101_OK = 0,
    NL0_CC1101_NOT_READY,  /* CHIP_RDYn never went low */
    NL0_CC1101_WRONG_PART, /* PARTNUM/VERSION not a CC1101 */
    NL0_CC1101_TIMEOUT,
    NL0_CC1101_TOO_LONG,
} nl0_cc1101_err_t;

typedef struct {
    const nl0_cc1101_bus_t *bus;
    uint8_t patable; /* PATABLE[0]: the node's TX power cap */
    bool asleep;
    uint8_t version; /* VERSION register, read at init */
} nl0_cc1101_t;

typedef struct {
    uint8_t len;  /* payload bytes in buf */
    int16_t rssi; /* dBm */
    uint8_t lqi;  /* 0..127, lower is better */
} nl0_cc1101_rx_t;

/* Reset, check the part, write the shared register table and PATABLE, then
 * leave the radio in IDLE. */
nl0_cc1101_err_t nl0_cc1101_init(nl0_cc1101_t *r, const nl0_cc1101_bus_t *bus, uint8_t patable);

/* Send one variable-length packet (len 1..61) and wait for it to leave
 * (~12 ms for 40 bytes). Wakes the radio if it is asleep; ends in IDLE. */
nl0_cc1101_err_t nl0_cc1101_send(nl0_cc1101_t *r, const uint8_t *data, uint8_t len);

/* Listen up to timeout_ms for one CRC-good packet. Returns OK with *out
 * filled, or TIMEOUT. buf must hold 61 bytes. Ends in IDLE. */
nl0_cc1101_err_t nl0_cc1101_recv(nl0_cc1101_t *r, uint8_t *buf, nl0_cc1101_rx_t *out,
                                 uint32_t timeout_ms);

/* SLEEP (< 1 µA). Registers are kept; the TEST registers and PATABLE are
 * rewritten on the next wake (SWRS061I 19.5). */
void nl0_cc1101_sleep(nl0_cc1101_t *r);

/* Raw register access, for bring-up and tests. */
uint8_t nl0_cc1101_read(nl0_cc1101_t *r, uint8_t addr);
uint8_t nl0_cc1101_status(nl0_cc1101_t *r, uint8_t addr); /* 0x30..0x3D status registers */
void nl0_cc1101_write(nl0_cc1101_t *r, uint8_t addr, uint8_t value);
uint8_t nl0_cc1101_strobe(nl0_cc1101_t *r, uint8_t cmd);

/* Register and command numbers used outside the driver. */
#define NL0_CC1101_PARTNUM 0x30u
#define NL0_CC1101_VERSION 0x31u
#define NL0_CC1101_RSSI 0x34u
#define NL0_CC1101_MARCSTATE 0x35u
#define NL0_CC1101_RXBYTES 0x3Bu

#ifdef __cplusplus
}
#endif

#endif
