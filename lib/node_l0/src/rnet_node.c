#include "node_l0/rnet_node.h"

#include <string.h>

#include "rnet/rnet.h"

const uint8_t nl0_test_master_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                         0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

void nl0_identity_record(const nl0_identity_t *id, uint32_t w[NL0_ID_WORDS])
{
    w[0] = NL0_ID_MAGIC;
    w[1] = (uint32_t)id->node_id | (uint32_t)id->node_type << 16 | (uint32_t)id->net_id << 24;
    for (unsigned i = 0; i < 4u; i++) {
        w[2u + i] = (uint32_t)id->key[4u * i] | (uint32_t)id->key[4u * i + 1u] << 8 |
                    (uint32_t)id->key[4u * i + 2u] << 16 | (uint32_t)id->key[4u * i + 3u] << 24;
    }
    w[6] = 0;
    w[7] = nl0_crc32(0xFFFFFFFFu, w, 7u);
}

void nl0_identity_load(const nl0_ee_backend_t *be, uint32_t eeprom_base, uint8_t node_type,
                       uint32_t uid, nl0_identity_t *out)
{
    uint32_t w[NL0_ID_WORDS];
    for (unsigned i = 0; i < NL0_ID_WORDS; i++) {
        w[i] = be->read(be->ctx, eeprom_base + NL0_ID_OFFSET + 4u * i);
    }
    if (w[0] == NL0_ID_MAGIC && nl0_crc32(0xFFFFFFFFu, w, 7u) == w[7]) {
        out->node_id = (uint16_t)w[1];
        out->node_type = (uint8_t)(w[1] >> 16);
        out->net_id = (uint8_t)(w[1] >> 24);
        for (unsigned i = 0; i < 16u; i++) {
            out->key[i] = (uint8_t)(w[2u + i / 4u] >> (8u * (i % 4u)));
        }
        out->provisioned = true;
        return;
    }
    uint16_t id = (uint16_t)(uid ^ (uid >> 16));
    out->node_id = id == 0xFFFFu ? 0xFFFEu : id;
    out->node_type = node_type;
    out->net_id = 0;
    rnet_derive_key(nl0_test_master_key, out->node_id, node_type, out->key);
    out->provisioned = false;
}

void nl0_counter_init(nl0_counter_t *c, const nl0_ee_backend_t *be, uint32_t eeprom_base)
{
    memset(c, 0, sizeof *c);
    c->ring.be = be;
    c->ring.base = eeprom_base + NL0_CTR_OFFSET;
    c->ring.slots = NL0_CTR_SLOTS;
    c->ring.words = 1;
    uint32_t stored = 0;
    /* Blank ring: start at 0 (and store it before use, as rnet_ctr_due(0)). */
    c->next = nl0_ee_ring_load(&c->ring, &stored) ? rnet_ctr_resume(stored) : 0u;
}

bool nl0_counter_take(nl0_counter_t *c, uint32_t *ctr)
{
    if (rnet_ctr_due(c->next) && !nl0_ee_ring_store(&c->ring, &c->next)) {
        return false;
    }
    *ctr = c->next++;
    return true;
}
