#include "node_l0/rnet_node.hpp"

namespace nl0
{
namespace
{

constexpr uint32_t crc_seed = 0xFFFFFFFFu;
constexpr unsigned crc_covered_words = 7;
constexpr uint16_t reserved_node_id = 0xFFFF;
constexpr uint8_t test_net_id = 0;

uint32_t pack_ids(const node_identity &identity)
{
    return uint32_t{identity.node_id} | uint32_t{identity.node_type} << 16 |
           uint32_t{identity.net_id} << 24;
}

uint32_t key_word(const rnet::aes_key &key, unsigned word)
{
    const unsigned b = 4 * word;
    return uint32_t{key[b]} | uint32_t{key[b + 1]} << 8 | uint32_t{key[b + 2]} << 16 |
           uint32_t{key[b + 3]} << 24;
}

bool record_is_valid(const std::array<uint32_t, identity_words> &w)
{
    return w[0] == identity_magic && nl0_crc32(crc_seed, w.data(), crc_covered_words) == w[7];
}

node_identity identity_from_record(const std::array<uint32_t, identity_words> &w)
{
    node_identity identity;
    identity.node_id = static_cast<uint16_t>(w[1]);
    identity.node_type = static_cast<uint8_t>(w[1] >> 16);
    identity.net_id = static_cast<uint8_t>(w[1] >> 24);
    for (unsigned i = 0; i < identity.key.size(); i++) {
        identity.key[i] = static_cast<uint8_t>(w[2 + i / 4] >> (8 * (i % 4)));
    }
    identity.provisioned = true;
    return identity;
}

uint16_t node_id_from_uid(uint32_t chip_uid)
{
    const auto folded = static_cast<uint16_t>(chip_uid ^ (chip_uid >> 16));
    return folded == reserved_node_id ? reserved_node_id - 1 : folded;
}

node_identity test_identity(uint8_t node_type, uint32_t chip_uid)
{
    node_identity identity;
    identity.node_id = node_id_from_uid(chip_uid);
    identity.node_type = node_type;
    identity.net_id = test_net_id;
    identity.key = rnet::derive_node_key(rnet::test_master_key, identity.node_id, node_type);
    return identity;
}

} // namespace

std::array<uint32_t, identity_words> identity_record(const node_identity &identity)
{
    std::array<uint32_t, identity_words> w{};
    w[0] = identity_magic;
    w[1] = pack_ids(identity);
    for (unsigned i = 0; i < 4; i++) {
        w[2 + i] = key_word(identity.key, i);
    }
    w[7] = nl0_crc32(crc_seed, w.data(), crc_covered_words);
    return w;
}

node_identity load_identity_or_test_identity(const nl0_ee_backend_t &eeprom, uint32_t eeprom_base,
                                             uint8_t node_type, uint32_t chip_uid)
{
    std::array<uint32_t, identity_words> w{};
    for (unsigned i = 0; i < identity_words; i++) {
        w[i] = eeprom.read(eeprom.ctx, eeprom_base + identity_offset + 4 * i);
    }
    return record_is_valid(w) ? identity_from_record(w) : test_identity(node_type, chip_uid);
}

void uplink_counter::load(const nl0_ee_backend_t &eeprom, uint32_t eeprom_base)
{
    ring_ = nl0_ee_ring_t{};
    ring_.be = &eeprom;
    ring_.base = eeprom_base + counter_ring_offset;
    ring_.slots = counter_ring_slots;
    ring_.words = 1;
    uint32_t last_stored = 0;
    const bool any_stored = nl0_ee_ring_load(&ring_, &last_stored);
    next_ = any_stored ? rnet::counter_after_reset(last_stored) : 0;
}

bool uplink_counter::take_next(uint32_t &counter)
{
    if (rnet::counter_must_be_stored_before_use(next_) && !nl0_ee_ring_store(&ring_, &next_)) {
        return false;
    }
    counter = next_++;
    return true;
}

} // namespace nl0
