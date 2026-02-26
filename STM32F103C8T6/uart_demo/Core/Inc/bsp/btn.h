#ifndef __BTN_H
#define __BTN_H

#include <stdint.h>

typedef struct
{
    uint16_t pin;
    uint8_t *event_flag;
    uint32_t last_ms;
    uint32_t debounce_ms;
} btn_t;

extern volatile uint8_t g_pay_btn_event;
void btn_poll(void);
void btn_handle_exti(uint16_t GPIO_Pin, uint32_t now);

#endif
