// acronym  meaning
// csn      chip select, active low
// gdo      CC1101 general digital output pin
// iocfg    CC1101 GDO pin configuration register
//
// wiring under test, module pin -> ESP32 pin
//
//   module  SCK  MISO  MOSI  CSN  GDO0  GDO2
//   esp32   18   19    23    5    21    22
//
// each GDO output is driven low then high by its IOCFG register;
// a correctly wired GDO pin follows on exactly its own ESP32 pin
//
// chip select   CSN high: MISO released (pulled up)   CSN low: MISO low (chip ready)
#include <Arduino.h>
#include <SPI.h>

#include <algorithm>
#include <iterator>

namespace
{

constexpr int sck_pin = 18, miso_pin = 19, mosi_pin = 23, csn_pin = 5;
constexpr int esp32_pins_to_watch[] = {21, 22, 4, 13, 14, 15, 16, 17, 25, 26, 27, 32, 33};

constexpr uint8_t register_iocfg2 = 0x00;
constexpr uint8_t register_iocfg0 = 0x02;
constexpr uint8_t status_register_partnum = 0x30;
constexpr uint8_t status_register_version = 0x31;
constexpr uint8_t strobe_reset = 0x30;
constexpr uint8_t strobe_receive = 0x34;
constexpr uint8_t strobe_idle = 0x36;
constexpr uint8_t status_register_marcstate = 0x35;
constexpr uint8_t marcstate_idle = 0x01;
constexpr uint8_t marcstate_receive = 0x0D;
constexpr uint8_t expected_versions[] = {0x14, 0x04};
constexpr int receive_mode_reads = 50;
constexpr uint8_t read_single = 0x80;
constexpr uint8_t read_burst = 0xC0;
constexpr uint8_t gdo_drive_low = 0x2F;
constexpr uint8_t gdo_drive_high = 0x6F;
constexpr uint8_t register_sync_high = 0x04;
constexpr uint8_t sync_high_reset_value = 0xD3;
constexpr uint8_t sync_high_marker = 0x5A;
constexpr uint32_t all_lines_low_ms = 250;
constexpr uint32_t chip_ready_timeout_us = 5000;

const SPISettings cc1101_spi{1000000, MSBFIRST, SPI_MODE0};

bool select_chip()
{
    digitalWrite(csn_pin, LOW);
    const uint32_t start = micros();
    while (digitalRead(miso_pin) == HIGH) {
        if (micros() - start > chip_ready_timeout_us) {
            digitalWrite(csn_pin, HIGH);
            return false;
        }
    }
    return true;
}

uint8_t transfer(uint8_t first, uint8_t second)
{
    SPI.beginTransaction(cc1101_spi);
    uint8_t reply = 0xFF;
    if (select_chip()) {
        SPI.transfer(first);
        reply = SPI.transfer(second);
        digitalWrite(csn_pin, HIGH);
    }
    SPI.endTransaction();
    return reply;
}

void strobe(uint8_t command)
{
    SPI.beginTransaction(cc1101_spi);
    if (select_chip()) {
        SPI.transfer(command);
        digitalWrite(csn_pin, HIGH);
    }
    SPI.endTransaction();
}

void write_register(uint8_t address, uint8_t value)
{
    transfer(address, value);
}

uint8_t read_status_register(uint8_t address)
{
    return transfer(address | read_burst, 0);
}

uint8_t read_register(uint8_t address)
{
    return transfer(address | read_single, 0);
}

uint32_t watched_pin_levels()
{
    uint32_t levels = 0;
    for (size_t index = 0; index < std::size(esp32_pins_to_watch); index++) {
        levels |= static_cast<uint32_t>(digitalRead(esp32_pins_to_watch[index])) << index;
    }
    return levels;
}

void report_chip_select()
{
    SPI.end();
    pinMode(miso_pin, INPUT_PULLUP);
    digitalWrite(csn_pin, HIGH);
    delay(2);
    const int miso_when_deselected = digitalRead(miso_pin);
    digitalWrite(csn_pin, LOW);
    delay(2);
    const int miso_when_selected = digitalRead(miso_pin);
    digitalWrite(csn_pin, HIGH);
    SPI.begin(sck_pin, miso_pin, mosi_pin, -1);
    const bool chip_select_works = miso_when_deselected == HIGH && miso_when_selected == LOW;
    Serial.printf("chip select: MISO deselected=%d selected=%d (expect 1, 0) %s\n", miso_when_deselected,
                  miso_when_selected, chip_select_works ? "ok" : "FAULT: CSN not reaching the chip");
}

void report_supply()
{
    write_register(register_sync_high, sync_high_marker);
    const uint8_t before = read_register(register_sync_high);
    SPI.end();
    for (const int pin : {csn_pin, sck_pin, mosi_pin, miso_pin, 21, 22}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    delay(all_lines_low_ms);
    digitalWrite(csn_pin, HIGH);
    for (const int pin : {miso_pin, 21, 22}) {
        pinMode(pin, INPUT_PULLDOWN);
    }
    SPI.begin(sck_pin, miso_pin, mosi_pin, -1);
    delay(5);
    const uint8_t after = read_register(register_sync_high);
    const char *verdict = after == sync_high_marker   ? "ok: module keeps power on its own VCC"
                          : after == sync_high_reset_value ? "FAULT: module reset, it is powered only through its signal pins"
                                                          : "unclear";
    Serial.printf("supply: marker before=0x%02X after %u ms of all lines low=0x%02X %s\n", before,
                  static_cast<unsigned>(all_lines_low_ms), after, verdict);
    write_register(register_sync_high, sync_high_reset_value);
}

bool version_is_valid(uint8_t version)
{
    return std::find(std::begin(expected_versions), std::end(expected_versions), version) != std::end(expected_versions);
}

void report_reads_in_receive_mode()
{
    strobe(strobe_receive);
    delay(5);
    int good_reads = 0, in_receive = 0;
    uint8_t last_bad_version = 0, last_bad_marcstate = 0;
    uint8_t marcstate_counts[32] = {};
    for (int read = 0; read < receive_mode_reads; read++) {
        const uint8_t version = read_status_register(status_register_version);
        const uint8_t marcstate = read_status_register(status_register_marcstate) & 0x1F;
        if (version_is_valid(version)) {
            good_reads++;
            marcstate_counts[marcstate]++;
        } else {
            last_bad_version = version;
            last_bad_marcstate = marcstate;
        }
        in_receive += marcstate == marcstate_receive ? 1 : 0;
        delay(20);
    }
    strobe(strobe_idle);
    delay(5);
    Serial.printf("receive mode: %d/%d reads ok, %d/%d in receive state", good_reads, receive_mode_reads, in_receive,
                  receive_mode_reads);
    Serial.print(", states on good reads:");
    for (int state = 0; state < 32; state++) {
        if (marcstate_counts[state]) {
            Serial.printf(" 0x%02X x%d", state, marcstate_counts[state]);
        }
    }
    if (good_reads < receive_mode_reads) {
        Serial.printf(", last bad version=0x%02X marcstate=0x%02X", last_bad_version, last_bad_marcstate);
    }
    const uint8_t idle_marcstate = read_status_register(status_register_marcstate) & 0x1F;
    Serial.printf("; back to idle: %s\n", idle_marcstate == marcstate_idle ? "ok" : "FAULT");
}

void report_pins_following(const char *gdo_name, uint8_t iocfg_register)
{
    write_register(iocfg_register, gdo_drive_low);
    delay(5);
    const uint32_t when_low = watched_pin_levels();
    write_register(iocfg_register, gdo_drive_high);
    delay(5);
    const uint32_t when_high = watched_pin_levels();
    const uint32_t following = ~when_low & when_high;
    Serial.printf("%s follows ESP32 pin(s):", gdo_name);
    bool any = false;
    for (size_t index = 0; index < std::size(esp32_pins_to_watch); index++) {
        if (following & (1u << index)) {
            Serial.printf(" %d", esp32_pins_to_watch[index]);
            any = true;
        }
    }
    Serial.println(any ? "" : " none");
    write_register(iocfg_register, gdo_drive_low);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    pinMode(csn_pin, OUTPUT);
    digitalWrite(csn_pin, HIGH);
    for (const int pin : esp32_pins_to_watch) {
        pinMode(pin, INPUT_PULLDOWN);
    }
    SPI.begin(sck_pin, miso_pin, mosi_pin, -1);
    strobe(strobe_reset);
    delay(10);
    Serial.printf("\ncc1101 wiring check: partnum=0x%02X version=0x%02X (expect 0x00, 0x14 or 0x04)\n",
                  read_status_register(status_register_partnum),
                  read_status_register(status_register_version));
    Serial.printf("iocfg2 reset value=0x%02X (expect 0x29)\n", read_register(register_iocfg2));
}

void loop()
{
    report_chip_select();
    Serial.printf("version=0x%02X (expect 0x14 or 0x04)\n", read_status_register(status_register_version));
    report_pins_following("GDO0", register_iocfg0);
    report_pins_following("GDO2", register_iocfg2);
    Serial.println("expected: GDO0 -> 21, GDO2 -> 22");
    strobe(strobe_reset);
    delay(10);
    report_supply();
    report_reads_in_receive_mode();
    strobe(strobe_reset);
    delay(10);
    delay(3000);
}
