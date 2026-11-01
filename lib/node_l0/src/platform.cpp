// acronym  meaning
// nl0      node_l0: STM32L0 node platform library
// msi      multi-speed internal oscillator
// lse      low-speed external oscillator (32.768 kHz crystal)
// lsi      low-speed internal oscillator (~37 kHz)
// iwdg     independent watchdog
// exti     extended interrupt and event controller
// nvic     nested vectored interrupt controller
// ssr      RTC sub-second register (counts down)
// tr, dr   RTC time and date registers
// rsf      RTC registers synchronization flag
// lpsdsr   low-power deep-sleep/sleep regulator bit
// bcd      binary-coded decimal
// pecr     flash program/erase control register
// pekeyr   flash PECR unlock key register
// csr      RCC control/status register
#include "node_l0/platform.h"

#include "stm32l0xx.h"
#include "stm32l0xx_ll_bus.h"
#include "stm32l0xx_ll_cortex.h"
#include "stm32l0xx_ll_exti.h"
#include "stm32l0xx_ll_iwdg.h"
#include "stm32l0xx_ll_pwr.h"
#include "stm32l0xx_ll_rcc.h"
#include "stm32l0xx_ll_rtc.h"
#include "stm32l0xx_ll_system.h"

namespace
{

constexpr uint32_t rtc_asynchronous_prescaler = 0;
constexpr uint32_t rtc_synchronous_prescaler = 32767;
constexpr uint32_t rtc_ticks_per_second = 32768;
constexpr uint32_t rtc_wakeup_exti_line = LL_EXTI_LINE_20;
constexpr uint32_t rtc_irq_priority = 2;
constexpr uint32_t lse_timeout_loops = 2000000;
constexpr uint32_t lsi_hz = 37000;
constexpr uint32_t iwdg_prescaler_divisor = 256;
constexpr uint32_t iwdg_reload_max = 0xFFF;
constexpr uint32_t milliseconds_per_second = 1000;
constexpr uint32_t seconds_per_minute = 60;
constexpr uint32_t seconds_per_hour = 3600;
constexpr uint32_t eeprom_unlock_key_1 = 0x89ABCDEFu;
constexpr uint32_t eeprom_unlock_key_2 = 0x02030405u;
constexpr uint32_t flash_errors = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                                  FLASH_SR_NOTZEROERR | FLASH_SR_RDERR | FLASH_SR_FWWERR;

volatile bool wakeup_pending;

void select_lse_as_rtc_clock()
{
    const uint32_t csr_before_backup_reset = RCC->CSR;
    LL_RCC_ForceBackupDomainReset();
    LL_RCC_ReleaseBackupDomainReset();
    RCC->CSR = csr_before_backup_reset & ~RCC_CSR_RTCSEL_Msk;
    while (!LL_RCC_LSE_IsReady()) {
    }
    LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSE);
}

uint32_t bcd_to_binary(uint32_t value)
{
    return (value >> 4) * 10u + (value & 0x0Fu);
}

// RTC shadow registers after Stop (RM0377 26.4.8):
//
//   Stop ──wake──► SSR/TR/DR hold the pre-Stop time ──2 RTCCLK (61 us)──► RSF set, time current
//                  reading here returns stale time       read only from here on
void wait_for_rtc_shadow_registers()
{
    LL_RTC_DisableWriteProtection(RTC);
    LL_RTC_ClearFlag_RS(RTC);
    LL_RTC_EnableWriteProtection(RTC);
    while (!LL_RTC_IsActiveFlag_RS(RTC)) {
    }
}

uint32_t eeprom_read(void *, uint32_t address)
{
    return *reinterpret_cast<volatile const uint32_t *>(address);
}

bool is_eeprom_word(uint32_t address)
{
    return address >= NL0_EEPROM_BASE && address + 4u <= NL0_EEPROM_BASE + NL0_EEPROM_SIZE &&
           (address & 3u) == 0u;
}

void unlock_eeprom()
{
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = eeprom_unlock_key_1;
        FLASH->PEKEYR = eeprom_unlock_key_2;
    }
}

//   PECR.FIX = 0: the word is erased only if needed before it is programmed
bool eeprom_write(void *, uint32_t address, uint32_t word)
{
    if (!is_eeprom_word(address)) {
        return false;
    }
    if (eeprom_read(nullptr, address) == word) {
        return true;
    }
    unlock_eeprom();
    FLASH->SR = flash_errors;
    *reinterpret_cast<volatile uint32_t *>(address) = word;
    while (FLASH->SR & FLASH_SR_BSY) {
    }
    const bool written = (FLASH->SR & flash_errors) == 0u;
    FLASH->SR = flash_errors;
    FLASH->PECR |= FLASH_PECR_PELOCK;
    return written;
}

} // namespace

bool nl0_clock_init(nl0_msi_t msi)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    LL_RCC_MSI_SetRange(static_cast<uint32_t>(msi) << RCC_ICSCR_MSIRANGE_Pos);
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_0);
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE3);
    while (LL_PWR_IsActiveFlag_VOS()) {
    }
    LL_RCC_SetClkAfterWakeFromStop(LL_RCC_STOP_WAKEUPCLOCK_MSI);
    SystemCoreClockUpdate();

    LL_PWR_EnableBkUpAccess();
    if (!LL_RCC_LSE_IsReady()) {
        LL_RCC_LSE_SetDriveCapability(LL_RCC_LSEDRIVE_LOW);
        LL_RCC_LSE_Enable();
        for (uint32_t i = 0; i < lse_timeout_loops && !LL_RCC_LSE_IsReady(); i++) {
        }
    }
    return LL_RCC_LSE_IsReady() != 0u;
}

bool nl0_rtc_init(void)
{
    if (!LL_RCC_LSE_IsReady()) {
        return false;
    }
    LL_PWR_EnableBkUpAccess();
    if (LL_RCC_GetRTCClockSource() != LL_RCC_RTC_CLKSOURCE_LSE) {
        select_lse_as_rtc_clock();
    }
    LL_RCC_EnableRTC();

    LL_RTC_DisableWriteProtection(RTC);
    if (LL_RTC_GetAsynchPrescaler(RTC) != rtc_asynchronous_prescaler ||
        LL_RTC_GetSynchPrescaler(RTC) != rtc_synchronous_prescaler) {
        LL_RTC_EnableInitMode(RTC);
        while (!LL_RTC_IsActiveFlag_INIT(RTC)) {
        }
        LL_RTC_SetAsynchPrescaler(RTC, rtc_asynchronous_prescaler);
        LL_RTC_SetSynchPrescaler(RTC, rtc_synchronous_prescaler);
        LL_RTC_SetHourFormat(RTC, LL_RTC_HOURFORMAT_24HOUR);
        LL_RTC_DisableInitMode(RTC);
    }
    LL_RTC_EnableWriteProtection(RTC);
    return true;
}

void nl0_rtc_wakeup_start(uint32_t seconds)
{
    LL_RTC_DisableWriteProtection(RTC);
    LL_RTC_WAKEUP_Disable(RTC);
    while (!LL_RTC_IsActiveFlag_WUTW(RTC)) {
    }
    LL_RTC_WAKEUP_SetClock(RTC, LL_RTC_WAKEUPCLOCK_CKSPRE);
    LL_RTC_WAKEUP_SetAutoReload(RTC, seconds - 1u);
    LL_RTC_ClearFlag_WUT(RTC);
    LL_RTC_EnableIT_WUT(RTC);
    LL_RTC_WAKEUP_Enable(RTC);
    LL_RTC_EnableWriteProtection(RTC);

    LL_EXTI_EnableIT_0_31(rtc_wakeup_exti_line);
    LL_EXTI_EnableRisingTrig_0_31(rtc_wakeup_exti_line);
    LL_EXTI_ClearFlag_0_31(rtc_wakeup_exti_line);
    NVIC_SetPriority(RTC_IRQn, rtc_irq_priority);
    NVIC_EnableIRQ(RTC_IRQn);
}

bool nl0_rtc_wakeup_take(void)
{
    __disable_irq();
    const bool woke = wakeup_pending;
    wakeup_pending = false;
    __enable_irq();
    return woke;
}

__attribute__((weak)) void nl0_rtc_wakeup_hook(void)
{
}

extern "C" void RTC_IRQHandler(void)
{
    if (LL_RTC_IsActiveFlag_WUT(RTC)) {
        LL_RTC_ClearFlag_WUT(RTC);
        LL_EXTI_ClearFlag_0_31(rtc_wakeup_exti_line);
        wakeup_pending = true;
        nl0_rtc_wakeup_hook();
    }
}

// Consistent read (reading SSR locks TR/DR until DR is read):
//
//   SSR ─► TR ─► DR ─► SSR again ─► TR ─► DR
//   retry while the second SSR > the first (SSR counts down; a reload = a second ticked)
//
//   TR bits   21:16 hours (BCD)   14:8 minutes (BCD)   6:0 seconds (BCD)
uint32_t nl0_rtc_ticks(void)
{
    uint32_t sub_second = 0;
    uint32_t time = 0;
    uint32_t sub_second_again = 0;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    do {
        sub_second = RTC->SSR;
        time = RTC->TR;
        (void)RTC->DR;
        sub_second_again = RTC->SSR;
        (void)RTC->TR;
        (void)RTC->DR;
    } while (sub_second_again > sub_second);
    __set_PRIMASK(primask);

    const uint32_t hours = bcd_to_binary((time >> 16) & 0x3Fu);
    const uint32_t minutes = bcd_to_binary((time >> 8) & 0x7Fu);
    const uint32_t seconds = bcd_to_binary(time & 0x7Fu);
    const uint32_t seconds_of_day = hours * seconds_per_hour + minutes * seconds_per_minute + seconds;
    return seconds_of_day * rtc_ticks_per_second + (rtc_synchronous_prescaler - sub_second);
}

void nl0_lowpower_init(bool debug_in_stop)
{
    LL_PWR_EnableUltraLowPower();
    LL_PWR_EnableFastWakeUp();
    LL_PWR_SetPowerMode(LL_PWR_MODE_STOP);
    if (debug_in_stop) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_DBGMCU);
        LL_DBGMCU_EnableDBGStopMode();
    }
}

// Regulator during WFI (RM0377 6.3): LPSDSR also applies to Sleep, where it is
// only allowed at <= 131 kHz, so it is set for Stop alone.
//
//   mode    SLEEPDEEP  LPSDSR  used by
//   Stop    1          1       nl0_stop
//   Sleep   0          0       naps at 2 MHz (SysTick)
void nl0_stop(void)
{
    LL_PWR_ClearFlag_WU();
    LL_PWR_SetRegulModeLP(LL_PWR_REGU_LPMODES_LOW_POWER);
    LL_LPM_EnableDeepSleep();
    __DSB();
    __WFI();
    LL_LPM_EnableSleep();
    LL_PWR_SetRegulModeLP(LL_PWR_REGU_LPMODES_MAIN);
    wait_for_rtc_shadow_registers();
}

void nl0_delay_ms(uint32_t ms)
{
    SysTick->LOAD = SystemCoreClock / milliseconds_per_second - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {
        }
    }
    SysTick->CTRL = 0u;
}

void nl0_iwdg_start(uint32_t timeout_ms)
{
    uint32_t reload = (timeout_ms * (lsi_hz / iwdg_prescaler_divisor)) / milliseconds_per_second;
    if (reload > iwdg_reload_max) {
        reload = iwdg_reload_max;
    }
    if (reload == 0u) {
        reload = 1u;
    }
    LL_IWDG_Enable(IWDG);
    LL_IWDG_EnableWriteAccess(IWDG);
    LL_IWDG_SetPrescaler(IWDG, LL_IWDG_PRESCALER_256);
    LL_IWDG_SetReloadCounter(IWDG, reload);
    while (!LL_IWDG_IsReady(IWDG)) {
    }
    LL_IWDG_ReloadCounter(IWDG);
}

void nl0_iwdg_kick(void)
{
    LL_IWDG_ReloadCounter(IWDG);
}

// RCC_CSR flags, checked in this order (every internal reset also pulses NRST):
//
//   order  flag       cause
//   1      LPWRRSTF   NL0_RESET_LOWPOWER
//   2      IWDGRSTF   NL0_RESET_IWDG
//   3      SFTRSTF    NL0_RESET_SOFTWARE
//   4      PORRSTF    NL0_RESET_POWER_ON
//   5      PINRSTF    NL0_RESET_PIN
//   none              NL0_RESET_UNKNOWN
nl0_reset_cause_t nl0_reset_cause(void)
{
    nl0_reset_cause_t cause = NL0_RESET_UNKNOWN;
    if (LL_RCC_IsActiveFlag_LPWRRST()) {
        cause = NL0_RESET_LOWPOWER;
    } else if (LL_RCC_IsActiveFlag_IWDGRST()) {
        cause = NL0_RESET_IWDG;
    } else if (LL_RCC_IsActiveFlag_SFTRST()) {
        cause = NL0_RESET_SOFTWARE;
    } else if (LL_RCC_IsActiveFlag_PORRST()) {
        cause = NL0_RESET_POWER_ON;
    } else if (LL_RCC_IsActiveFlag_PINRST()) {
        cause = NL0_RESET_PIN;
    }
    LL_RCC_ClearResetFlags();
    return cause;
}

const nl0_ee_backend_t nl0_eeprom_backend = {eeprom_read, eeprom_write, nullptr};
