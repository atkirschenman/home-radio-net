/* Wear-levelled record ring in data EEPROM (garden ADR-0004 D2, weather ADR-0003 D8).
 *
 * Pure logic over a word-access backend, so it is host-tested. Each slot holds
 *   word 0       sequence number (never 0)
 *   word 1..n    payload
 *   word n+1     CRC-32 over the sequence number and payload
 * and is written check-word-last, so a write torn by a brown-out leaves a slot
 * whose CRC fails; the previous slot is then still the newest valid one.
 * Erased L0 data EEPROM reads 0x00000000, which never passes the CRC. */
#ifndef NODE_L0_EE_RING_H
#define NODE_L0_EE_RING_H

#include <stdbool.h>
#include <stdint.h>

#define NL0_EE_RING_MAX_WORDS 6u /* payload words per record */

typedef struct {
    uint32_t (*read)(void *ctx, uint32_t addr);
    bool (*write)(void *ctx, uint32_t addr, uint32_t word); /* false on a write error */
    void *ctx;
} nl0_ee_backend_t;

typedef struct {
    const nl0_ee_backend_t *be;
    uint32_t base;   /* byte address of slot 0, word aligned */
    uint16_t slots;  /* >= 2 */
    uint8_t words;   /* payload words, 1..NL0_EE_RING_MAX_WORDS */
    /* state, set by nl0_ee_ring_load */
    uint32_t seq;    /* sequence number the next store will use */
    uint16_t next;   /* slot the next store will use */
} nl0_ee_ring_t;

/* Bytes the ring occupies: slots * (words + 2) * 4. */
uint32_t nl0_ee_ring_size(uint16_t slots, uint8_t words);

/* Find the newest valid record. Returns true and fills payload if one exists;
 * returns false (payload untouched) for a blank or fully corrupt ring. Either
 * way the ring is ready for nl0_ee_ring_store. */
bool nl0_ee_ring_load(nl0_ee_ring_t *r, uint32_t *payload);

/* Write payload into the next slot. Returns false on a backend write error or a
 * read-back mismatch; the previous record stays valid in that case. */
bool nl0_ee_ring_store(nl0_ee_ring_t *r, const uint32_t *payload);

uint32_t nl0_crc32(uint32_t crc, const uint32_t *words, uint32_t n);

#endif
