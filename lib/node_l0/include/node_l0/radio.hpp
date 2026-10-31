// acronym  meaning
// nl0      node_l0: STM32L0 node platform library
// csn      chip select, active low
// gdo      CC1101 general digital output pin
// mosi     SPI controller out, peripheral in
// miso     SPI controller in, peripheral out
// lqi      link quality indicator (CC1101)
#pragma once

#include <cstddef>
#include <cstdint>

#include "Hal.h"
#include "Module.h"
#include "modules/CC1101/CC1101.h"
#include "stm32l0xx.h"

namespace nl0
{

struct gpio_line {
    GPIO_TypeDef *port;
    uint32_t pin_number;
};

struct radio_lines {
    gpio_line csn;
    gpio_line gdo0;
    gpio_line gdo2;
    gpio_line sck;
    gpio_line mosi;
    gpio_line miso;
};

class radiolib_hal_l0 : public RadioLibHal
{
  public:
    enum radiolib_pin : uint32_t { csn_pin = 0, gdo0_pin = 1, gdo2_pin = 2 };

    explicit radiolib_hal_l0(const radio_lines &lines);

    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t, void (*)(void), uint32_t) override {}
    void detachInterrupt(uint32_t) override {}
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t, uint32_t, RadioLibTime_t) override
    {
        return 0;
    }
    void spiBegin() override;
    void spiBeginTransaction() override {}
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;
    void spiEndTransaction() override {}
    void spiEnd() override {}

  private:
    const gpio_line &line_for(uint32_t pin) const;
    void select_and_wait_for_chip_ready();
    void deselect();
    uint8_t transfer_byte(uint8_t out);
    uint64_t microseconds_since_start();

    radio_lines lines_;
    uint32_t last_rtc_ticks_ = 0;
    uint64_t rtc_ticks_since_start_ = 0;
    bool clock_started_ = false;
};

struct received_packet {
    size_t length = 0;
    float rssi_dbm = 0;
    uint8_t lqi = 0;
};

class cc1101_radio
{
  public:
    explicit cc1101_radio(const radio_lines &lines);

    int16_t configure_then_sleep(int8_t power_dbm);
    int16_t send(const uint8_t *data, size_t length);
    int16_t receive(uint8_t *buffer, uint32_t timeout_ms, received_packet &packet);
    void sleep();

  private:
    int16_t wake_and_restore_power();

    radiolib_hal_l0 hal_;
    Module module_;
    CC1101 chip_;
    int8_t power_dbm_ = -15;
    bool asleep_ = false;
};

} // namespace nl0
