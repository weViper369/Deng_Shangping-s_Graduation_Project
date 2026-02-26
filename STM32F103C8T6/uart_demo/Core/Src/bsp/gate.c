#include "gate.h"
#include "tim.h"       // 用到 htim3
#include <string.h>

#ifndef TIM_CHANNEL_1
#define TIM_CHANNEL_1 0x00000000U
#endif

#define GATE_TIM_HANDLE   htim3
#define GATE_TIM_CHANNEL  TIM_CHANNEL_1

// TIM3配置：1MHz 计数（1 tick = 1us），ARR=19999（20ms）
// 那么 CCR 直接填 us 值：1000/1500/2000
static inline void gate_set_pwm_us(uint16_t us)
{
    __HAL_TIM_SET_COMPARE(&GATE_TIM_HANDLE, GATE_TIM_CHANNEL, us);
}

static volatile uint32_t *g_ms = 0;
static gate_cfg_t cfg;
static gate_state_t st = GATE_CLOSED;

static uint8_t target_open = 0;      // 1=目标开，0=目标关
static uint32_t move_start_ms = 0;

static uint16_t map_open_us(void)
{
    return cfg.invert ? cfg.pwm_us_close : cfg.pwm_us_open;
}
static uint16_t map_close_us(void)
{
    return cfg.invert ? cfg.pwm_us_open : cfg.pwm_us_close;
}

void gate_init(volatile uint32_t *ms_tick_ptr, const gate_cfg_t *c)
{
    g_ms = ms_tick_ptr;
    memset(&cfg, 0, sizeof(cfg));

    // 默认值
    cfg.pwm_us_open  = 2000;
    cfg.pwm_us_close = 1000;
    cfg.move_time_ms = 800;
    cfg.invert       = 0;

    if (c) cfg = *c;

    // 上电默认关闸
    gate_set_pwm_us(map_close_us());
    st = GATE_CLOSED;
    target_open = 0;
    move_start_ms = (g_ms ? *g_ms : 0);
}

void gate_open(void)
{
    gate_set_pwm_us(map_open_us());
    st = GATE_MOVING;
    target_open = 1;
    move_start_ms = (g_ms ? *g_ms : 0);
}

void gate_close(void)
{
    gate_set_pwm_us(map_close_us());
    st = GATE_MOVING;
    target_open = 0;
    move_start_ms = (g_ms ? *g_ms : 0);
}

void gate_poll(void)
{
    if (st != GATE_MOVING) return;
    if (!g_ms) return;

    uint32_t now = *g_ms;
    if (now - move_start_ms >= cfg.move_time_ms)
    {
        st = target_open ? GATE_OPEN : GATE_CLOSED;
    }
}

gate_state_t gate_get_state(void) { return st; }
uint8_t gate_is_open(void)   { return st == GATE_OPEN; }
uint8_t gate_is_closed(void) { return st == GATE_CLOSED; }
