#include "node_l0/ee_ring.h"

#include <stddef.h>

uint32_t nl0_crc32(uint32_t crc, const uint32_t *words, uint32_t n)
{
    /* CRC-32 (IEEE, reflected), bitwise: no table, ~60 bytes of code. Words
     * are fed little-endian. Call with crc = 0 to start. */
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t w = words[i];
        for (int b = 0; b < 4; b++) {
            crc ^= w & 0xFFu;
            w >>= 8;
            for (int k = 0; k < 8; k++) {
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
    }
    return ~crc;
}

static uint32_t slot_words(const nl0_ee_ring_t *r)
{
    return (uint32_t)r->words + 2u;
}

static uint32_t slot_addr(const nl0_ee_ring_t *r, uint16_t slot)
{
    return r->base + (uint32_t)slot * slot_words(r) * 4u;
}

uint32_t nl0_ee_ring_size(uint16_t slots, uint8_t words)
{
    return (uint32_t)slots * ((uint32_t)words + 2u) * 4u;
}

/* Reads one slot into buf (seq, payload..., crc); true if it is a valid record. */
static bool read_slot(const nl0_ee_ring_t *r, uint16_t slot, uint32_t *buf)
{
    uint32_t a = slot_addr(r, slot);
    uint32_t n = slot_words(r);
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = r->be->read(r->be->ctx, a + 4u * i);
    }
    return buf[0] != 0u && nl0_crc32(0u, buf, n - 1u) == buf[n - 1u];
}

bool nl0_ee_ring_load(nl0_ee_ring_t *r, uint32_t *payload)
{
    uint32_t buf[NL0_EE_RING_MAX_WORDS + 2u];
    uint32_t best_seq = 0u;
    uint16_t best_slot = 0u;
    bool found = false;

    for (uint16_t s = 0; s < r->slots; s++) {
        if (read_slot(r, s, buf) && (!found || buf[0] > best_seq)) {
            best_seq = buf[0];
            best_slot = s;
            found = true;
            for (uint8_t i = 0; i < r->words; i++) {
                payload[i] = buf[1u + i];
            }
        }
    }

    if (found) {
        r->seq = best_seq + 1u;
        r->next = (uint16_t)((best_slot + 1u) % r->slots);
    } else {
        r->seq = 1u;
        r->next = 0u;
    }
    return found;
}

bool nl0_ee_ring_store(nl0_ee_ring_t *r, const uint32_t *payload)
{
    uint32_t buf[NL0_EE_RING_MAX_WORDS + 2u];
    uint32_t n = slot_words(r);
    uint32_t a = slot_addr(r, r->next);

    buf[0] = r->seq;
    for (uint8_t i = 0; i < r->words; i++) {
        buf[1u + i] = payload[i];
    }
    buf[n - 1u] = nl0_crc32(0u, buf, n - 1u);

    /* Invalidate the slot first (check word = 0 never matches), then write the
     * body, then the check word: a tear at any point leaves an invalid slot. */
    bool ok = r->be->write(r->be->ctx, a + 4u * (n - 1u), 0u);
    for (uint32_t i = 0; ok && i < n; i++) {
        ok = r->be->write(r->be->ctx, a + 4u * i, buf[i]);
    }
    for (uint32_t i = 0; ok && i < n; i++) {
        ok = r->be->read(r->be->ctx, a + 4u * i) == buf[i];
    }

    /* Advance even on failure, so a worn-out slot is skipped next time. */
    r->seq++;
    r->next = (uint16_t)((r->next + 1u) % r->slots);
    return ok;
}
