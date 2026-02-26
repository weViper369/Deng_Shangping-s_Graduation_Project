#include "btn.h"
#include "main.h"
volatile uint8_t g_pay_btn_event = 0;
static uint8_t pressed_latch = 0;

static btn_t g_pay_btn =
{
    .pin = GPIO_PIN_12,
    .event_flag = &g_pay_btn_event,
    .last_ms = 0,
    .debounce_ms = 50
};

void btn_poll(void)
{
    if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET)
        pressed_latch = 0;
}

void btn_handle_exti(uint16_t GPIO_Pin, uint32_t now)
{
    if (GPIO_Pin != GPIO_PIN_12) return;
    
    // PB12: 下拉，按下=1
    GPIO_PinState level = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12);

    // 松开：清锁
    if (level == GPIO_PIN_RESET)
    {
        pressed_latch = 0;
        return;
    }

    // 按下：如果已经触发过就不再触发
    if (pressed_latch) return;

    // 时间消抖
    if ((uint32_t)(now - g_pay_btn.last_ms) < g_pay_btn.debounce_ms) return;
    printf("BTN IRQ\r\n");
    g_pay_btn.last_ms = now;
    pressed_latch = 1;          // 锁住，直到松开
    g_pay_btn_event = 1;        // 产生一次事件
}
