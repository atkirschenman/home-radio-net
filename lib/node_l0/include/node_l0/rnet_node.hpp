// acronym  meaning
// nl0      node_l0: STM32L0 node platform library
// rnet     home radio network (this protocol)
// ee       EEPROM
#pragma once

#include <array>
#include <cstdint>

#include "node_l0/ee_ring.h"
#include "rnet/rnet.hpp"

namespace nl0
{

// data EEPROM, rnet region (offsets from 0x08080000)
//
//   0x000 +----------------------------------------------+
//         | identity record, 8 words                     |
//         |   0  magic 0x31504E52 "RNP1"                 |
//         |   1  node_id[15:0] node_type[23:16] net_id[31:24]
//         |   2-5  node key, little-endian words         |
//         |   6  reserved 0                              |
//         |   7  crc32 of words 0-6                      |
//   0x040 +----------------------------------------------+
//         | uplink counter ring, 16 slots x 3 words      |
//   0x100 +----------------------------------------------+  project-owned beyond

inline constexpr uint32_t identity_magic = 0x31504E52;
inline constexpr uint32_t identity_offset = 0x000;
inline constexpr unsigned identity_words = 8;
inline constexpr uint32_t counter_ring_offset = 0x040;
inline constexpr uint16_t counter_ring_slots = 16;
inline constexpr uint32_t rnet_region_end = 0x100;

struct node_identity {
    uint16_t node_id = 0;
    uint8_t node_type = 0;
    uint8_t net_id = 0;
    rnet::aes_key key{};
    bool provisioned = false;
};

node_identity load_identity_or_test_identity(const nl0_ee_backend_t &eeprom, uint32_t eeprom_base,
                                             uint8_t node_type, uint32_t chip_uid);

std::array<uint32_t, identity_words> identity_record(const node_identity &identity);

class uplink_counter
{
  public:
    void load(const nl0_ee_backend_t &eeprom, uint32_t eeprom_base);
    bool take_next(uint32_t &counter);

  private:
    nl0_ee_ring_t ring_{};
    uint32_t next_ = 0;
};

} // namespace nl0
