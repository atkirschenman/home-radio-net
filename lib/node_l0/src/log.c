#include "node_l0/log.h"

#include "stm32l0xx_ll_usart.h"

static USART_TypeDef *log_usart;

void nl0_log_init(USART_TypeDef *usart, uint32_t baud)
{
    log_usart = usart;
    LL_USART_Disable(usart);
    LL_USART_SetTransferDirection(usart, LL_USART_DIRECTION_TX);
    LL_USART_ConfigCharacter(usart, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE, LL_USART_STOPBITS_1);
    LL_USART_SetBaudRate(usart, SystemCoreClock, LL_USART_OVERSAMPLING_16, baud);
    LL_USART_Enable(usart);
}

static void put(char c)
{
    if (!log_usart) {
        return;
    }
    while (!LL_USART_IsActiveFlag_TXE(log_usart)) {
    }
    LL_USART_TransmitData8(log_usart, (uint8_t)c);
}

void nl0_log_str(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            put('\r');
        }
        put(*s++);
    }
}

void nl0_log_u32(uint32_t v)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    nl0_log_str(&buf[i]);
}

void nl0_log_i32(int32_t v)
{
    if (v < 0) {
        put('-');
        nl0_log_u32(0u - (uint32_t)v);
    } else {
        nl0_log_u32((uint32_t)v);
    }
}

void nl0_log_hex(uint32_t v, uint8_t digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) {
        put(hex[(v >> (4 * i)) & 0xFu]);
    }
}

void nl0_log_flush(void)
{
    if (log_usart) {
        while (!LL_USART_IsActiveFlag_TC(log_usart)) {
        }
    }
}
