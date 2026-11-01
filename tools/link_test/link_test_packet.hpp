// link test packet, 40 bytes
//
//   offset  0-1    2-5              6              7-8               9     10-39
//   field   "LT"   sequence (LE)    power setting  battery mV (LE)   0x80  pattern(sequence)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace link_test
{

inline constexpr std::size_t packet_length = 40;
inline constexpr uint8_t magic[2] = {'L', 'T'};
inline constexpr uint8_t reserved_byte = 0x80;
inline constexpr std::size_t pattern_start = 10;

using packet = std::array<uint8_t, packet_length>;

struct contents {
    uint32_t sequence;
    uint8_t power_setting;
    uint16_t battery_mv;
};

constexpr uint8_t pattern_byte(std::size_t offset, uint32_t sequence)
{
    return static_cast<uint8_t>(offset * 37u + sequence);
}

inline packet make_packet(const contents &c)
{
    packet p{};
    p[0] = magic[0];
    p[1] = magic[1];
    for (std::size_t i = 0; i < 4; i++) {
        p[2 + i] = static_cast<uint8_t>(c.sequence >> (8 * i));
    }
    p[6] = c.power_setting;
    p[7] = static_cast<uint8_t>(c.battery_mv);
    p[8] = static_cast<uint8_t>(c.battery_mv >> 8);
    p[9] = reserved_byte;
    for (std::size_t i = pattern_start; i < packet_length; i++) {
        p[i] = pattern_byte(i, c.sequence);
    }
    return p;
}

inline std::optional<contents> read_packet(const uint8_t *bytes, std::size_t length)
{
    if (length != packet_length || bytes[0] != magic[0] || bytes[1] != magic[1]) {
        return std::nullopt;
    }
    contents c{};
    for (std::size_t i = 0; i < 4; i++) {
        c.sequence |= uint32_t{bytes[2 + i]} << (8 * i);
    }
    for (std::size_t i = pattern_start; i < packet_length; i++) {
        if (bytes[i] != pattern_byte(i, c.sequence)) {
            return std::nullopt;
        }
    }
    c.power_setting = bytes[6];
    c.battery_mv = static_cast<uint16_t>(bytes[7] | bytes[8] << 8);
    return c;
}

} // namespace link_test
