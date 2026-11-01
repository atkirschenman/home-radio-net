#pragma once

#include <cstdint>

#include "modules/CC1101/CC1101.h"
#include "rnet_phy.hpp"

namespace rnet
{

inline constexpr int8_t cc1101_power_steps_dbm[] = {-30, -20, -15, -10, 0, 5, 7, 10};

inline int16_t configure_cc1101(CC1101 &radio, int8_t power_dbm)
{
    ConfigFSK_t settings;
    settings.frequency = phy::frequency_mhz;
    settings.bitRate = phy::bit_rate_kbps;
    settings.frequencyDeviation = phy::deviation_khz;
    settings.receiverBandwidth = phy::rx_bandwidth_khz;
    settings.power = power_dbm;
    settings.preambleLength = phy::preamble_bits;

    const uint8_t encoding =
        phy::whitening_enabled ? RADIOLIB_ENCODING_WHITENING : RADIOLIB_ENCODING_NRZ;
    int16_t status = radio.begin(settings);
    if (status == RADIOLIB_ERR_NONE) {
        status = radio.setSyncWord(phy::sync_word_high, phy::sync_word_low, 0, false,
                                   phy::sync_word_sent_twice);
    }
    if (status == RADIOLIB_ERR_NONE) {
        status = radio.setPreambleLength(phy::preamble_bits, phy::preamble_quality_threshold_bits);
    }
    if (status == RADIOLIB_ERR_NONE) {
        status = radio.variablePacketLengthMode(phy::max_packet_length);
    }
    if (status == RADIOLIB_ERR_NONE) {
        status = radio.setCrcFiltering(phy::crc16_enabled);
    }
    if (status == RADIOLIB_ERR_NONE) {
        status = radio.setEncoding(encoding);
    }
    return status;
}

} // namespace rnet
