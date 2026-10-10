/* node_l0 smoke image: proves the platform links for the L051 and sleeps in
 * Stop with a 1 Hz RTC wakeup and the IWDG running. No board pins used. */
#include "node_l0/platform.h"

int main(void)
{
    nl0_reset_cause_t cause = nl0_reset_cause();
    (void)cause;
    bool lse = nl0_clock_init(NL0_MSI_2M);
    nl0_delay_ms(2000); /* SWD attach window before the first Stop (ADR-0002 T5) */
    nl0_lowpower_init(true);
    if (!lse || !nl0_rtc_init()) {
        for (;;) {
        }
    }
    nl0_iwdg_start(4000);
    nl0_rtc_wakeup_start(1);

    nl0_ee_ring_t ring = {.be = &nl0_eeprom_backend, .base = NL0_EEPROM_BASE, .slots = 4, .words = 1};
    uint32_t boots = 0;
    nl0_ee_ring_load(&ring, &boots);
    boots++;
    nl0_ee_ring_store(&ring, &boots);

    for (;;) {
        nl0_stop();
        if (nl0_rtc_wakeup_take()) {
            nl0_iwdg_kick();
        }
    }
}
