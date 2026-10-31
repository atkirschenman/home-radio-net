/* Node identity and uplink counter in data EEPROM (01-architecture.md 5.1,
 * 5.2, O-4). Shared by every node type, so the provisioning script writes
 * one layout. Region 0x000-0x0FF of the 2 KB data EEPROM belongs to rnet:
 *
 *   0x000  identity record, 8 words (written by tools/rnet_py provisioning):
 *            word 0  magic 0x31504E52 ("RNP1")
 *            word 1  node_id (bits 0-15) | node_type (16-23) | net_id (24-31)
 *            word 2-5  K_node (16 bytes, little-endian words)
 *            word 6  reserved (0)
 *            word 7  CRC-32 (nl0_crc32) over words 0-6
 *   0x040  uplink counter ring: 16 slots x (1 + 2) words (nl0_ee_ring)
 *   0x100  end; projects own the rest (weather: rain ring at 0x100)
 *
 * Pure logic over the nl0_ee_backend_t, so it is host-tested. */
#ifndef NODE_L0_RNET_NODE_H
#define NODE_L0_RNET_NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "node_l0/ee_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NL0_ID_MAGIC 0x31504E52u
#define NL0_ID_OFFSET 0x000u
#define NL0_ID_WORDS 8u
#define NL0_CTR_OFFSET 0x040u
#define NL0_CTR_SLOTS 16u
#define NL0_RNET_REGION_END 0x100u

typedef struct {
    uint16_t node_id;
    uint8_t node_type;
    uint8_t net_id;
    uint8_t key[16];
    bool provisioned; /* false: the test identity below is in use */
} nl0_identity_t;

/* Published test master key (00 01 .. 0F), the same as home-radio-net's
 * golden vectors. A gateway configured with it accepts unprovisioned nodes:
 * bench only, never on a real network. */
extern const uint8_t nl0_test_master_key[16];

/* Read the identity record at eeprom_base + NL0_ID_OFFSET. If it is missing
 * or corrupt, fill a test identity instead: node_id from uid (never 0xFFFF),
 * the given node_type, net_id 0, key derived from the test master key. */
void nl0_identity_load(const nl0_ee_backend_t *be, uint32_t eeprom_base, uint8_t node_type,
                       uint32_t uid, nl0_identity_t *out);

/* Pack an identity into the 8-word record (for the provisioning tools and
 * tests). */
void nl0_identity_record(const nl0_identity_t *id, uint32_t words[NL0_ID_WORDS]);

/* Uplink counter (5.2): resumes at stored + 256 after a reset and stores
 * every 256th value before it is used, so no value is ever sent twice. */
typedef struct {
    nl0_ee_ring_t ring;
    uint32_t next;
} nl0_counter_t;

void nl0_counter_init(nl0_counter_t *c, const nl0_ee_backend_t *be, uint32_t eeprom_base);

/* The counter for the next frame. Returns false only if the EEPROM write
 * that had to come first failed (the frame must then not be sent). */
bool nl0_counter_take(nl0_counter_t *c, uint32_t *ctr);

#ifdef __cplusplus
}
#endif

#endif
