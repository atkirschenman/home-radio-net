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

#define RTC_PREDIV_A 0u
#define RTC_PREDIV_S 32767u
#define LSE_TIMEOUT_LOOPS 2000000u /* ~2 s at 2 MHz, generous: LSE start is ~1 s worst case */
#define LSI_HZ 37000u

static volatile bool wakeup_flag;

/* ---- clocks ------------------------------------------------------------ */
bool nl0_clock_init(nl0_msi_t msi)
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    /* MSI first (range 5 at reset), then drop the core to 1.2 V: fine up to 4.2 MHz. */
    LL_RCC_MSI_SetRange((uint32_t)msi << RCC_ICSCR_MSIRANGE_Pos);
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
        for (uint32_t i = 0; i < LSE_TIMEOUT_LOOPS && !LL_RCC_LSE_IsReady(); i++) {
        }
    }
    return LL_RCC_LSE_IsReady() != 0u;
}

/* ---- RTC ----------------------------------------------------------------- */
bool nl0_rtc_init(void)
{
    if (!LL_RCC_LSE_IsReady()) {
        return false;
    }
    LL_PWR_EnableBkUpAccess();
    /* The RTC clock source can only be changed with a backup-domain reset. */
    if (LL_RCC_GetRTCClockSource() != LL_RCC_RTC_CLKSOURCE_LSE) {
        uint32_t csr = RCC->CSR;
        LL_RCC_ForceBackupDomainReset();
        LL_RCC_ReleaseBackupDomainReset();
        /* The reset also stops the LSE; restore its bits. */
        RCC->CSR = csr & ~RCC_CSR_RTCSEL_Msk;
        while (!LL_RCC_LSE_IsReady()) {
        }
        LL_RCC_SetRTCClockSource(LL_RCC_RTC_CLKSOURCE_LSE);
    }
    LL_RCC_EnableRTC();

    LL_RTC_DisableWriteProtection(RTC);
    if (LL_RTC_GetAsynchPrescaler(RTC) != RTC_PREDIV_A ||
        LL_RTC_GetSynchPrescaler(RTC) != RTC_PREDIV_S) {
        LL_RTC_EnableInitMode(RTC);
        while (!LL_RTC_IsActiveFlag_INIT(RTC)) {
        }
        LL_RTC_SetAsynchPrescaler(RTC, RTC_PREDIV_A);
        LL_RTC_SetSynchPrescaler(RTC, RTC_PREDIV_S);
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

    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_20);
    LL_EXTI_EnableRisingTrig_0_31(LL_EXTI_LINE_20);
    LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_20);
    NVIC_SetPriority(RTC_IRQn, 2);
    NVIC_EnableIRQ(RTC_IRQn);
}

bool nl0_rtc_wakeup_take(void)
{
    __disable_irq();
    bool f = wakeup_flag;
    wakeup_flag = false;
    __enable_irq();
    return f;
}

__attribute__((weak)) void nl0_rtc_wakeup_hook(void)
{
}

void RTC_IRQHandler(void)
{
    if (LL_RTC_IsActiveFlag_WUT(RTC)) {
        LL_RTC_ClearFlag_WUT(RTC);
        LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_20);
        wakeup_flag = true;
        nl0_rtc_wakeup_hook();
    }
}

static uint32_t bcd2(uint32_t v)
{
    return (v >> 4) * 10u + (v & 0x0Fu);
}

uint32_t nl0_rtc_ticks(void)
{
    /* Reading SSR locks TR/DR until DR is read, so SSR and TR are consistent.
     * Read twice and retry if the second changed underneath. */
    uint32_t ssr, tr, ssr2;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    do {
        ssr = RTC->SSR;
        tr = RTC->TR;
        (void)RTC->DR;
        ssr2 = RTC->SSR;
        (void)RTC->TR;
        (void)RTC->DR;
    } while (ssr2 > ssr); /* SSR counts down; a reload means a second ticked */
    __set_PRIMASK(primask);

    uint32_t h = bcd2((tr >> 16) & 0x3Fu);
    uint32_t m = bcd2((tr >> 8) & 0x7Fu);
    uint32_t s = bcd2(tr & 0x7Fu);
    uint32_t secs = h * 3600u + m * 60u + s; /* time of day; see header */
    return secs * 32768u + (RTC_PREDIV_S - ssr);
}

/* ---- low power ---------------------------------------------------------- */
void nl0_lowpower_init(bool debug_in_stop)
{
    LL_PWR_EnableUltraLowPower();
    LL_PWR_EnableFastWakeUp();
    LL_PWR_SetRegulModeLP(LL_PWR_REGU_LPMODES_LOW_POWER); /* LPSDSR: low-power regulator in Stop */
    LL_PWR_SetPowerMode(LL_PWR_MODE_STOP);
    if (debug_in_stop) {
        LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_DBGMCU);
        LL_DBGMCU_EnableDBGStopMode();
    }
}

void nl0_stop(void)
{
    LL_PWR_ClearFlag_WU();
    LL_LPM_EnableDeepSleep();
    __DSB();
    __WFI();
    LL_LPM_EnableSleep();
}

void nl0_delay_ms(uint32_t ms)
{
    /* SysTick as a down-counter polled per millisecond; no interrupt. */
    SysTick->LOAD = SystemCoreClock / 1000u - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms--) {
        while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)) {
        }
    }
    SysTick->CTRL = 0u;
}

/* ---- watchdog ------------------------------------------------------------ */
void nl0_iwdg_start(uint32_t timeout_ms)
{
    uint32_t reload = (timeout_ms * (LSI_HZ / 256u)) / 1000u;
    if (reload > 0xFFFu) {
        reload = 0xFFFu;
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

/* ---- reset cause --------------------------------------------------------- */
nl0_reset_cause_t nl0_reset_cause(void)
{
    nl0_reset_cause_t c = NL0_RESET_UNKNOWN;
    /* Every internal reset also pulses NRST, so PINRSTF is checked last. */
    if (LL_RCC_IsActiveFlag_LPWRRST()) {
        c = NL0_RESET_LOWPOWER;
    } else if (LL_RCC_IsActiveFlag_IWDGRST()) {
        c = NL0_RESET_IWDG;
    } else if (LL_RCC_IsActiveFlag_SFTRST()) {
        c = NL0_RESET_SOFTWARE;
    } else if (LL_RCC_IsActiveFlag_PORRST()) {
        c = NL0_RESET_POWER_ON;
    } else if (LL_RCC_IsActiveFlag_PINRST()) {
        c = NL0_RESET_PIN;
    }
    LL_RCC_ClearResetFlags();
    return c;
}

/* ---- data EEPROM --------------------------------------------------------- */
#define PEKEY1 0x89ABCDEFu
#define PEKEY2 0x02030405u
#define FLASH_ERRORS (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_NOTZEROERR | FLASH_SR_RDERR | FLASH_SR_FWWERR)

static uint32_t ee_read(void *ctx, uint32_t addr)
{
    (void)ctx;
    return *(volatile const uint32_t *)addr;
}

static bool ee_write(void *ctx, uint32_t addr, uint32_t word)
{
    (void)ctx;
    if (addr < NL0_EEPROM_BASE || addr + 4u > NL0_EEPROM_BASE + NL0_EEPROM_SIZE || (addr & 3u)) {
        return false;
    }
    if (*(volatile const uint32_t *)addr == word) {
        return true; /* no wear for an unchanged word */
    }
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = PEKEY1;
        FLASH->PEKEYR = PEKEY2;
    }
    FLASH->SR = FLASH_ERRORS; /* clear stale errors (write-1-to-clear) */
    *(volatile uint32_t *)addr = word; /* FIX = 0: erase only if needed */
    while (FLASH->SR & FLASH_SR_BSY) {
    }
    bool ok = (FLASH->SR & FLASH_ERRORS) == 0u;
    FLASH->SR = FLASH_ERRORS;
    FLASH->PECR |= FLASH_PECR_PELOCK;
    return ok;
}

const nl0_ee_backend_t nl0_eeprom_backend = {ee_read, ee_write, 0};
