/* Minimal blocking TX-only UART log (no printf: saves ~2-5 KB of flash).
 * The project enables the USART clock and configures the TX pin first. */
#ifndef NODE_L0_LOG_H
#define NODE_L0_LOG_H

#include <stdint.h>

#include "stm32l0xx.h"

#ifdef __cplusplus
extern "C" {
#endif

void nl0_log_init(USART_TypeDef *usart, uint32_t baud);
void nl0_log_str(const char *s);
void nl0_log_u32(uint32_t v);
void nl0_log_i32(int32_t v);
void nl0_log_hex(uint32_t v, uint8_t digits);
void nl0_log_flush(void); /* wait for the last byte to leave, before Stop */

#ifdef __cplusplus
}
#endif

#endif
