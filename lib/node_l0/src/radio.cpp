#include "node_l0/radio.hpp"

#include "node_l0/platform.h"
#include "rnet/cc1101_config.hpp"
#include "stm32l0xx_ll_gpio.h"

namespace nl0
{
namespace
{

constexpr uint32_t radiolib_input = 0;
constexpr uint32_t radiolib_output = 1;
constexpr uint32_t radiolib_low = 0;
constexpr uint32_t radiolib_high = 1;
constexpr uint32_t radiolib_rising = 1;
constexpr uint32_t radiolib_falling = 2;
constexpr uint32_t chip_ready_poll_limit = 2000;
constexpr uint32_t rtc_ticks_per_second = 32768;
constexpr uint32_t cpu_cycles_per_delay_loop = 4;

uint32_t mask_of(const gpio_line &line)
{
    return 1u << line.pin_number;
}

bool is_high(const gpio_line &line)
{
    return LL_GPIO_IsInputPinSet(line.port, mask_of(line)) != 0u;
}

void drive(const gpio_line &line, bool high)
{
    if (high) {
        LL_GPIO_SetOutputPin(line.port, mask_of(line));
    } else {
        LL_GPIO_ResetOutputPin(line.port, mask_of(line));
    }
}

void set_mode(const gpio_line &line, uint32_t ll_mode)
{
    LL_GPIO_SetPinPull(line.port, mask_of(line), LL_GPIO_PULL_NO);
    LL_GPIO_SetPinOutputType(line.port, mask_of(line), LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinMode(line.port, mask_of(line), ll_mode);
}

} // namespace

radiolib_hal_l0::radiolib_hal_l0(const radio_lines &lines)
    : RadioLibHal(radiolib_input, radiolib_output, radiolib_low, radiolib_high, radiolib_rising,
                  radiolib_falling),
      lines_(lines)
{
}

void radiolib_hal_l0::init()
{
    spiBegin();
    set_mode(lines_.gdo0, LL_GPIO_MODE_INPUT);
    set_mode(lines_.gdo2, LL_GPIO_MODE_INPUT);
}

const gpio_line &radiolib_hal_l0::line_for(uint32_t pin) const
{
    switch (pin) {
    case csn_pin:
        return lines_.csn;
    case gdo0_pin:
        return lines_.gdo0;
    default:
        return lines_.gdo2;
    }
}

void radiolib_hal_l0::select_and_wait_for_chip_ready()
{
    set_mode(lines_.miso, LL_GPIO_MODE_INPUT);
    drive(lines_.csn, false);
    for (uint32_t poll = 0; poll < chip_ready_poll_limit && is_high(lines_.miso); poll++) {
    }
}

void radiolib_hal_l0::deselect()
{
    drive(lines_.csn, true);
    set_mode(lines_.miso, LL_GPIO_MODE_ANALOG);
}

void radiolib_hal_l0::pinMode(uint32_t pin, uint32_t mode)
{
    if (pin == RADIOLIB_NC) {
        return;
    }
    if (pin == csn_pin) {
        drive(lines_.csn, true);
    }
    set_mode(line_for(pin), mode == radiolib_output ? LL_GPIO_MODE_OUTPUT : LL_GPIO_MODE_INPUT);
}

void radiolib_hal_l0::digitalWrite(uint32_t pin, uint32_t value)
{
    if (pin == RADIOLIB_NC) {
        return;
    }
    const bool high = value != radiolib_low;
    if (pin == csn_pin) {
        high ? deselect() : select_and_wait_for_chip_ready();
        return;
    }
    drive(line_for(pin), high);
}

uint32_t radiolib_hal_l0::digitalRead(uint32_t pin)
{
    if (pin == RADIOLIB_NC) {
        return radiolib_low;
    }
    return is_high(line_for(pin)) ? radiolib_high : radiolib_low;
}

void radiolib_hal_l0::delay(RadioLibTime_t ms)
{
    nl0_delay_ms(static_cast<uint32_t>(ms));
}

void radiolib_hal_l0::delayMicroseconds(RadioLibTime_t us)
{
    const uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    uint32_t loops = static_cast<uint32_t>(us) * cycles_per_us / cpu_cycles_per_delay_loop + 1u;
    while (loops--) {
        __NOP();
    }
}

uint64_t radiolib_hal_l0::microseconds_since_start()
{
    const uint32_t now = nl0_rtc_ticks();
    if (clock_started_) {
        rtc_ticks_since_start_ += nl0_rtc_elapsed(last_rtc_ticks_, now);
    }
    clock_started_ = true;
    last_rtc_ticks_ = now;
    return rtc_ticks_since_start_ * 1000000u / rtc_ticks_per_second;
}

RadioLibTime_t radiolib_hal_l0::millis()
{
    return static_cast<RadioLibTime_t>(microseconds_since_start() / 1000u);
}

RadioLibTime_t radiolib_hal_l0::micros()
{
    return static_cast<RadioLibTime_t>(microseconds_since_start());
}

void radiolib_hal_l0::spiBegin()
{
    drive(lines_.sck, false);
    drive(lines_.mosi, false);
    set_mode(lines_.sck, LL_GPIO_MODE_OUTPUT);
    set_mode(lines_.mosi, LL_GPIO_MODE_OUTPUT);
    set_mode(lines_.miso, LL_GPIO_MODE_ANALOG);
}

// SPI mode 0, most significant bit first:
//
//   sck   ___/‾‾‾\___/‾‾‾\___
//   mosi  =X=bit7====X=bit6==   set while sck is low
//   miso        ^        ^      sampled on the rising edge
uint8_t radiolib_hal_l0::transfer_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
        drive(lines_.mosi, (out >> bit) & 1u);
        drive(lines_.sck, true);
        in = static_cast<uint8_t>(in << 1 | (is_high(lines_.miso) ? 1u : 0u));
        drive(lines_.sck, false);
    }
    return in;
}

void radiolib_hal_l0::spiTransfer(uint8_t *out, size_t len, uint8_t *in)
{
    for (size_t i = 0; i < len; i++) {
        const uint8_t received = transfer_byte(out ? out[i] : 0);
        if (in) {
            in[i] = received;
        }
    }
}

cc1101_radio::cc1101_radio(const radio_lines &lines)
    : hal_(lines), module_(&hal_, radiolib_hal_l0::csn_pin, radiolib_hal_l0::gdo0_pin, RADIOLIB_NC,
                           radiolib_hal_l0::gdo2_pin),
      chip_(&module_)
{
}

int16_t cc1101_radio::configure_then_sleep(int8_t power_dbm)
{
    power_dbm_ = power_dbm;
    asleep_ = false;
    const int16_t status = rnet::configure_cc1101(chip_, power_dbm);
    sleep();
    return status;
}

int16_t cc1101_radio::wake_and_restore_power()
{
    if (!asleep_) {
        return RADIOLIB_ERR_NONE;
    }
    asleep_ = false;
    const int16_t status = chip_.standby();
    return status == RADIOLIB_ERR_NONE ? chip_.setOutputPower(power_dbm_) : status;
}

int16_t cc1101_radio::send(const uint8_t *data, size_t length)
{
    const int16_t status = wake_and_restore_power();
    return status == RADIOLIB_ERR_NONE ? chip_.transmit(data, length) : status;
}

int16_t cc1101_radio::receive(uint8_t *buffer, uint32_t timeout_ms, received_packet &packet)
{
    int16_t status = wake_and_restore_power();
    if (status != RADIOLIB_ERR_NONE) {
        return status;
    }
    status = chip_.receive(buffer, rnet::phy::max_packet_length, timeout_ms);
    if (status == RADIOLIB_ERR_NONE) {
        packet.length = chip_.getPacketLength();
        packet.rssi_dbm = chip_.getRSSI();
        packet.lqi = chip_.getLQI();
    }
    chip_.standby();
    return status;
}

void cc1101_radio::sleep()
{
    chip_.sleep();
    asleep_ = true;
}

} // namespace nl0
