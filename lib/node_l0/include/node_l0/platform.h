/* STM32L0 node platform (home-radio-net lib/node_l0; garden ADR-0004 D2).
 * Clocks, RTC 1 Hz tick + sub-second timestamps, Stop mode, IWDG, data EEPROM,
 * reset cause, busy-wait delay. Pins and peripherals that differ per board
 * (UART, SPI, sensors) stay in each project. */
#ifndef NODE_L0_PLATFORM_H
#define NODE_L0_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "node_l0/ee_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- clocks ------------------------------------------------------------ */
typedef enum {
    NL0_MSI_1M = 4, /* 1.048 MHz */
    NL0_MSI_2M = 5, /* 2.097 MHz (reset default) */
    NL0_MSI_4M = 6, /* 4.194 MHz */
} nl0_msi_t;

/* SYSCLK = MSI at the given range, core at 1.2 V (range 3), flash 0 wait states,
 * LSE started with low drive. Returns false if the LSE did not start within
 * ~2 s (the RTC then can't run; the caller reports it and resets). */
bool nl0_clock_init(nl0_msi_t msi);

/* ---- RTC ----------------------------------------------------------------- */
/* RTC on LSE, PREDIV_A = 0, PREDIV_S = 32767: 1 Hz calendar with a 30.5 µs
 * sub-second counter (weather ADR-0003 D6). Keeps a running RTC across resets
 * other than a backup-domain reset. */
bool nl0_rtc_init(void);

/* Periodic wakeup every `seconds` (1..65536) on EXTI line 20. */
void nl0_rtc_wakeup_start(uint32_t seconds);

/* True once per wakeup that happened since the last call. */
bool nl0_rtc_wakeup_take(void);

/* Time of day in 1/32768 s ticks (30.5 µs), from the RTC calendar + sub-second
 * counter. Wraps to 0 at midnight (NL0_RTC_TICKS_PER_DAY), so take intervals
 * with nl0_rtc_elapsed. Safe from thread and interrupt context. */
#define NL0_RTC_TICKS_PER_DAY (86400u * 32768u)
uint32_t nl0_rtc_ticks(void);

/* Ticks from `from` to `to`, correct across midnight (intervals < 1 day). */
static inline uint32_t nl0_rtc_elapsed(uint32_t from, uint32_t to)
{
    return to >= from ? to - from : to + (NL0_RTC_TICKS_PER_DAY - from);
}

/* Optional hook called from the RTC interrupt on each wakeup (weak, empty). */
void nl0_rtc_wakeup_hook(void);

/* ---- low power ---------------------------------------------------------- */
/* debug_in_stop: keep SWD alive in Stop (Debug builds, ADR-0002 T5). */
void nl0_lowpower_init(bool debug_in_stop);

/* Enter Stop (regulator in low-power mode, VREFINT off, fast wakeup) until
 * any enabled EXTI interrupt. Returns after the wakeup handler has run.
 * After wakeup VREFINT may not be ready yet: wait for it before using the ADC. */
void nl0_stop(void);

void nl0_delay_ms(uint32_t ms);

/* ---- watchdog ------------------------------------------------------------ */
/* IWDG on LSI (keeps running in Stop). timeout_ms is nominal at 37 kHz;
 * the real LSI is 26-56 kHz, so the real timeout is 0.66x to 1.42x. */
void nl0_iwdg_start(uint32_t timeout_ms);
void nl0_iwdg_kick(void);

/* ---- reset cause --------------------------------------------------------- */
/* Values match schema/rnet.toml [enums.reset_cause]. A brown-out sets the same
 * flag as power-on on the L0, so it reports as power_on. */
typedef enum {
    NL0_RESET_UNKNOWN = 0,
    NL0_RESET_POWER_ON = 1,
    NL0_RESET_BROWN_OUT = 2,
    NL0_RESET_PIN = 3,
    NL0_RESET_IWDG = 4,
    NL0_RESET_SOFTWARE = 5,
    NL0_RESET_LOWPOWER = 6,
} nl0_reset_cause_t;

/* Read once at boot: decodes RCC_CSR, then clears the flags. */
nl0_reset_cause_t nl0_reset_cause(void);

/* ---- data EEPROM --------------------------------------------------------- */
#define NL0_EEPROM_BASE 0x08080000u
#define NL0_EEPROM_SIZE 2048u

/* Backend for nl0_ee_ring_t over the real data EEPROM. Addresses are absolute. */
extern const nl0_ee_backend_t nl0_eeprom_backend;

#ifdef __cplusplus
}
#endif

#endif
