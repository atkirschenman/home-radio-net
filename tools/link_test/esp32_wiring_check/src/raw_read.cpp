// acronym  meaning
// csn      chip select, active low
// sck      SPI clock
// mosi     SPI data from the ESP32 to the CC1101
// miso     SPI data from the CC1101 to the ESP32
//
// one CC1101 register read, clocked by hand (SPI mode 0, MSB first)
//
//   CSN   ‾‾‾\______________________________________________/‾‾‾
//   MISO  ---\_ ready  [ status byte  ]   [ register value ]
//   MOSI         [ address 0xF1     ]   [ 0x00 (dummy)    ]
//   SCK          _/‾\_/‾\_ ... 8 pulses _/‾\_/‾\_ ... 8 pulses
//
//   ESP32 pin   D5 = CSN   D18 = SCK   D23 = MOSI   D19 = MISO
//
// address byte: bit 7 = read, bit 6 = burst (needed for status registers 0x30-0x3D)
//
//   register  address  value on a CC1101
//   PARTNUM   0xF0     0x00
//   VERSION   0xF1     0x14 (or 0x04)
//   IOCFG2    0x80     0x29 after reset
//
// command test: every byte the chip receives answers with its status byte
//
//   status bit  7       6-4                                   3-0
//               0=ready state 0 idle, 1 receiving, 2 sending   fifo bytes
//
//   command  byte  meaning
//   SNOP     0xBD  no operation, just report state
//   SRX      0x34  start receiving
//   SIDLE    0x36  go idle
#include <Arduino.h>

namespace
{

constexpr int csn_pin = 5, sck_pin = 18, mosi_pin = 23, miso_pin = 19;
constexpr uint32_t half_clock_us = 50;
constexpr uint32_t chip_ready_timeout_us = 5000;
constexpr uint32_t pause_between_reads_ms = 2000;
constexpr uint8_t strobe_receive = 0x34;
constexpr uint8_t strobe_idle = 0x36;
constexpr uint8_t strobe_no_operation = 0xBD;

struct register_to_read {
    const char *name;
    uint8_t address;
    uint8_t expected;
};

constexpr register_to_read registers[] = {{"VERSION", 0xF1, 0x14}, {"PARTNUM", 0xF0, 0x00}, {"IOCFG2", 0x80, 0x29}};

void print_bits(uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        Serial.print((value >> bit) & 1);
    }
}

uint8_t clock_byte(uint8_t out)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; bit--) {
        digitalWrite(mosi_pin, (out >> bit) & 1);
        delayMicroseconds(half_clock_us);
        digitalWrite(sck_pin, HIGH);
        in = static_cast<uint8_t>(in << 1 | digitalRead(miso_pin));
        delayMicroseconds(half_clock_us);
        digitalWrite(sck_pin, LOW);
    }
    return in;
}

void read_register(const register_to_read &reg)
{
    Serial.printf("\nread %s\n", reg.name);
    Serial.printf("  MISO before CSN low: %d (1 = released, as expected)\n", digitalRead(miso_pin));
    digitalWrite(csn_pin, LOW);
    const uint32_t start = micros();
    while (digitalRead(miso_pin) == HIGH && micros() - start < chip_ready_timeout_us) {
    }
    const bool ready = digitalRead(miso_pin) == LOW;
    Serial.printf("  CSN low: MISO %s\n", ready ? "went low (chip ready)" : "stayed high: chip NOT ready");

    const uint8_t status = clock_byte(reg.address);
    Serial.print("  sent    0x");
    Serial.printf("%02X = ", reg.address);
    print_bits(reg.address);
    Serial.print("   received status 0x");
    Serial.printf("%02X = ", status);
    print_bits(status);
    Serial.printf("   (state %u, fifo %u)\n", (status >> 4) & 0x7, status & 0x0F);

    const uint8_t value = clock_byte(0x00);
    Serial.print("  sent    0x00 = 00000000   received value  0x");
    Serial.printf("%02X = ", value);
    print_bits(value);
    Serial.printf("   %s (expected 0x%02X)\n", value == reg.expected ? "OK" : "WRONG", reg.expected);

    digitalWrite(csn_pin, HIGH);
}

const char *state_name(uint8_t status)
{
    constexpr const char *names[] = {"IDLE", "RECEIVING", "TRANSMITTING", "FAST TX READY",
                                     "CALIBRATING", "SETTLING", "RX FIFO OVERFLOW", "TX FIFO UNDERFLOW"};
    return names[(status >> 4) & 0x7];
}

uint8_t send_command(const char *name, uint8_t command)
{
    digitalWrite(csn_pin, LOW);
    const uint32_t start = micros();
    while (digitalRead(miso_pin) == HIGH && micros() - start < chip_ready_timeout_us) {
    }
    const uint8_t status = clock_byte(command);
    digitalWrite(csn_pin, HIGH);
    Serial.printf("  sent    0x%02X = ", command);
    print_bits(command);
    Serial.printf(" %-17s received status 0x%02X = ", name, status);
    print_bits(status);
    Serial.printf("   state %s\n", state_name(status));
    return status;
}

void check_command_is_heard()
{
    Serial.println("\ncommand test: tell the chip to start receiving, then ask for its state");
    send_command("(ask state)", strobe_no_operation);
    send_command("(start receiving)", strobe_receive);
    delay(2);
    const bool receiving = ((send_command("(ask state)", strobe_no_operation) >> 4) & 0x7) == 1;
    send_command("(go idle)", strobe_idle);
    delay(2);
    const bool idle_again = ((send_command("(ask state)", strobe_no_operation) >> 4) & 0x7) == 0;
    Serial.printf("  result: %s\n", receiving && idle_again
                                         ? "the chip HEARS commands (state went RECEIVING, then IDLE)"
                                         : "the chip did NOT act on the commands");
}

} // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(csn_pin, OUTPUT);
    pinMode(sck_pin, OUTPUT);
    pinMode(mosi_pin, OUTPUT);
    pinMode(miso_pin, INPUT_PULLUP);
    digitalWrite(csn_pin, HIGH);
    digitalWrite(sck_pin, LOW);
    digitalWrite(mosi_pin, LOW);
}

void loop()
{
    for (const register_to_read &reg : registers) {
        read_register(reg);
    }
    check_command_is_heard();
    Serial.println("\n--------");
    delay(pause_between_reads_ms);
}
