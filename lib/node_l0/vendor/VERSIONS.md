# Vendored third-party code (ADR-0002 T3)

Copied as plain files, unmodified, so a build needs no network. To update: re-copy from the new tag and change this table in the same commit.

| Directory | Upstream | Tag | Files taken | Licence |
|---|---|---|---|---|
| `cmsis/Include` | github.com/ARM-software/CMSIS_5 `CMSIS/Core/Include` | 5.9.0 | `core_cm0plus.h`, `cmsis_gcc.h`, `cmsis_compiler.h`, `cmsis_version.h`, `mpu_armv7.h` | Apache-2.0 (`cmsis/LICENSE.txt`) |
| `device/` | github.com/STMicroelectronics/cmsis_device_l0 | v1.9.4 | `stm32l0xx.h`, `stm32l051xx.h`, `system_stm32l0xx.h/.c`, `gcc/startup_stm32l051xx.s` | Apache-2.0 (`device/LICENSE.md`) |
| `ll/` | github.com/STMicroelectronics/stm32l0xx_hal_driver | v1.10.7 | `Inc/stm32l0xx_ll_*.h` (all but `usb`), `Src/stm32l0xx_ll_{adc,crc,dma,exti,gpio,i2c,lptim,lpuart,pwr,rcc,rtc,spi,tim,usart,utils}.c`. No HAL files | BSD-3-Clause (`ll/LICENSE.md`) |
| `../../../third_party/unity` | github.com/ThrowTheSwitch/Unity | v2.6.1 | `src/unity.c`, `unity.h`, `unity_internals.h` | MIT |
