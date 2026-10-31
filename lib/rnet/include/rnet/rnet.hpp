// acronym  meaning
// rnet     home radio network (this protocol)
// mic      message integrity code: truncated AES-CMAC
// ack      acknowledgement
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rnet_messages.hpp"

namespace rnet
{

using aes_key = std::array<uint8_t, 16>;

inline constexpr aes_key test_master_key = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// frame: control | net_id | node_id(2) | node_type | counter(4) | message_type | [ack_counter(2)] |
// payload | mic(4)
struct frame_header {
    uint8_t control = 0;
    uint8_t net_id = 0;
    uint16_t node_id = 0;
    uint8_t node_type = 0;
    uint32_t counter = 0;
    uint8_t message_type = 0;
    bool has_ack = false;
    uint16_t ack_counter = 0;
};

struct parsed_frame {
    frame_header header;
    const uint8_t *payload = nullptr;
    std::size_t payload_length = 0;
};

aes_key derive_node_key(const aes_key &master_key, uint16_t node_id, uint8_t node_type);

std::size_t build_frame(const aes_key &node_key, const frame_header &header, const uint8_t *payload,
                        std::size_t payload_length, uint8_t *frame, std::size_t frame_capacity);

bool parse_frame_without_mic_check(const uint8_t *frame, std::size_t frame_length,
                                   parsed_frame &parsed);

bool mic_is_valid(const aes_key &node_key, const uint8_t *frame, std::size_t frame_length);

enum class replay_verdict { fresh, duplicate, old };

class receive_counter
{
  public:
    replay_verdict judge(uint32_t counter) const
    {
        if (!any_accepted_ || counter > last_accepted_) {
            return replay_verdict::fresh;
        }
        return counter == last_accepted_ ? replay_verdict::duplicate : replay_verdict::old;
    }

    void accept(uint32_t counter)
    {
        any_accepted_ = true;
        last_accepted_ = counter;
    }

    uint32_t last_accepted() const
    {
        return last_accepted_;
    }

  private:
    bool any_accepted_ = false;
    uint32_t last_accepted_ = 0;
};

constexpr uint32_t counter_after_reset(uint32_t last_stored_counter)
{
    return last_stored_counter + counter_persist_every;
}

constexpr bool counter_must_be_stored_before_use(uint32_t counter)
{
    return counter % counter_persist_every == 0;
}

} // namespace rnet
