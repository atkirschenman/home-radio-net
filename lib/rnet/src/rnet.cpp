#include "rnet/rnet.hpp"

#include <cstring>

#include "utils/Cryptography.h"

namespace rnet
{
namespace
{

using aes_block = std::array<uint8_t, 16>;

aes_block aes_cmac(const aes_key &key, const uint8_t *data, std::size_t length)
{
    aes_key key_kept_alive_during_use = key;
    RadioLibSoftwareAES128 aes;
    aes.init(key_kept_alive_during_use.data());
    aes_block tag{};
    aes.generateCMAC(data, length, tag.data());
    return tag;
}

aes_block aes_encrypt_block(const aes_key &key, const aes_block &plaintext)
{
    aes_key key_kept_alive_during_use = key;
    RadioLibSoftwareAES128 aes;
    aes.init(key_kept_alive_during_use.data());
    aes_block ciphertext{};
    aes.encryptECB(plaintext.data(), plaintext.size(), ciphertext.data());
    return ciphertext;
}

bool equal_in_constant_time(const uint8_t *a, const uint8_t *b, std::size_t length)
{
    uint8_t difference = 0;
    for (std::size_t i = 0; i < length; i++) {
        difference = static_cast<uint8_t>(difference | (a[i] ^ b[i]));
    }
    return difference == 0;
}

std::size_t header_length(bool has_ack)
{
    return header_length_without_ack + (has_ack ? 2 : 0);
}

uint8_t control_byte(const frame_header &header)
{
    const auto version_mask = static_cast<uint8_t>(3u << control_bits::version_shift);
    auto control = static_cast<uint8_t>(header.control & ~version_mask & ~control_bits::ack);
    control = static_cast<uint8_t>(control | frame_version << control_bits::version_shift);
    return header.has_ack ? static_cast<uint8_t>(control | control_bits::ack) : control;
}

uint8_t frame_version_of(uint8_t control_value)
{
    return static_cast<uint8_t>((control_value >> control_bits::version_shift) & 3u);
}

void put_u16(uint8_t *at, uint16_t value)
{
    at[0] = static_cast<uint8_t>(value);
    at[1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(uint8_t *at, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) {
        at[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint16_t get_u16(const uint8_t *at)
{
    return static_cast<uint16_t>(at[0] | at[1] << 8);
}

uint32_t get_u32(const uint8_t *at)
{
    return uint32_t{at[0]} | uint32_t{at[1]} << 8 | uint32_t{at[2]} << 16 | uint32_t{at[3]} << 24;
}

} // namespace

aes_key derive_node_key(const aes_key &master_key, uint16_t node_id, uint8_t node_type)
{
    aes_block block{};
    put_u16(&block[0], node_id);
    block[2] = node_type;
    std::memcpy(&block[3], key_label, sizeof key_label - 1);
    return aes_encrypt_block(master_key, block);
}

std::size_t build_frame(const aes_key &node_key, const frame_header &header, const uint8_t *payload,
                        std::size_t payload_length, uint8_t *frame, std::size_t frame_capacity)
{
    const std::size_t payload_offset = header_length(header.has_ack);
    const std::size_t mic_offset = payload_offset + payload_length;
    const std::size_t frame_length = mic_offset + mic_length;
    if (frame_length > frame_capacity || frame_length > max_payload) {
        return 0;
    }
    frame[0] = control_byte(header);
    frame[1] = header.net_id;
    put_u16(&frame[2], header.node_id);
    frame[4] = header.node_type;
    put_u32(&frame[5], header.counter);
    frame[9] = header.message_type;
    if (header.has_ack) {
        put_u16(&frame[10], header.ack_counter);
    }
    if (payload_length) {
        std::memcpy(&frame[payload_offset], payload, payload_length);
    }
    const aes_block tag = aes_cmac(node_key, frame, mic_offset);
    std::memcpy(&frame[mic_offset], tag.data(), mic_length);
    return frame_length;
}

bool parse_frame_without_mic_check(const uint8_t *frame, std::size_t frame_length,
                                   parsed_frame &parsed)
{
    if (frame_length < header_length_without_ack + mic_length ||
        frame_version_of(frame[0]) != frame_version) {
        return false;
    }
    frame_header &header = parsed.header;
    header.control = frame[0];
    header.net_id = frame[1];
    header.node_id = get_u16(&frame[2]);
    header.node_type = frame[4];
    header.counter = get_u32(&frame[5]);
    header.message_type = frame[9];
    header.has_ack = (frame[0] & control_bits::ack) != 0;
    header.ack_counter = 0;
    const std::size_t payload_offset = header_length(header.has_ack);
    if (frame_length < payload_offset + mic_length) {
        return false;
    }
    if (header.has_ack) {
        header.ack_counter = get_u16(&frame[10]);
    }
    parsed.payload = &frame[payload_offset];
    parsed.payload_length = frame_length - payload_offset - mic_length;
    return true;
}

bool mic_is_valid(const aes_key &node_key, const uint8_t *frame, std::size_t frame_length)
{
    if (frame_length <= mic_length) {
        return false;
    }
    const std::size_t mic_offset = frame_length - mic_length;
    const aes_block tag = aes_cmac(node_key, frame, mic_offset);
    return equal_in_constant_time(tag.data(), &frame[mic_offset], mic_length);
}

} // namespace rnet
