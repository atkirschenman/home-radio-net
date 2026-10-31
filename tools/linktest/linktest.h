/* Radio link test packet (bench only, not part of the protocol). Sent by a
 * node's link-test image once a second on the shared PHY; the ESP32 receiver
 * in tools/linktest/esp32 prints each one with RSSI/LQI and counts gaps.
 * 40-byte payload: the size of a real WX_REPORT frame (35 + 14 overhead
 * would be 49; 40 keeps it inside one FIFO with room to spare). */
#ifndef RNET_LINKTEST_H
#define RNET_LINKTEST_H

#include <stdint.h>

#define LINKTEST_LEN 40u
#define LINKTEST_MAGIC0 0x4Cu /* 'L' */
#define LINKTEST_MAGIC1 0x54u /* 'T' */

/* bytes 0-1 magic, 2-5 seq (LE), 6 PATABLE value, 7-8 VBAT mV (LE),
 * 9 reserved (0x80), 10..39 a pattern that changes with seq */
static inline void linktest_pack(uint8_t *p, uint32_t seq, uint8_t patable, uint16_t vbat_mv)
{
    p[0] = LINKTEST_MAGIC0;
    p[1] = LINKTEST_MAGIC1;
    for (int i = 0; i < 4; i++) {
        p[2 + i] = (uint8_t)(seq >> (8 * i));
    }
    p[6] = patable;
    p[7] = (uint8_t)vbat_mv;
    p[8] = (uint8_t)(vbat_mv >> 8);
    p[9] = 0x80u;
    for (unsigned i = 10; i < LINKTEST_LEN; i++) {
        p[i] = (uint8_t)(i * 37u + seq); /* changes every frame: whitening/CRC exercise */
    }
}

/* Returns 1 and fills the fields if p is a valid link-test packet. */
static inline int linktest_parse(const uint8_t *p, uint8_t len, uint32_t *seq, uint8_t *patable,
                                 uint16_t *vbat_mv)
{
    if (len != LINKTEST_LEN || p[0] != LINKTEST_MAGIC0 || p[1] != LINKTEST_MAGIC1) {
        return 0;
    }
    uint32_t s = 0;
    for (int i = 0; i < 4; i++) {
        s |= (uint32_t)p[2 + i] << (8 * i);
    }
    for (unsigned i = 10; i < LINKTEST_LEN; i++) {
        if (p[i] != (uint8_t)(i * 37u + s)) {
            return 0;
        }
    }
    *seq = s;
    *patable = p[6];
    *vbat_mv = (uint16_t)(p[7] | p[8] << 8);
    return 1;
}

#endif
