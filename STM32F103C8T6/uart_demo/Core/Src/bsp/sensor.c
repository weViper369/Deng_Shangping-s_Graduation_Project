#include "sensor.h"
#include "main.h"
#include <string.h>

#ifndef IR_BLOCK_LEVEL 
#define IR_BLOCK_LEVEL  0 
#endif

#define IR_IN_GPIO_Port     GPIOA
#define IR_IN_Pin           GPIO_PIN_0

#define IR_OUT_GPIO_Port    GPIOB
#define IR_OUT_Pin          GPIO_PIN_1

static inline uint8_t read_raw_in(void)
{
    return (uint8_t)(HAL_GPIO_ReadPin(IR_IN_GPIO_Port, IR_IN_Pin) ? 1 : 0);
}
static inline uint8_t read_raw_out(void)
{
    return (uint8_t)(HAL_GPIO_ReadPin(IR_OUT_GPIO_Port, IR_OUT_Pin) ? 1 : 0);
}

// raw -> blocked 语义
static inline uint8_t raw_to_blocked(uint8_t raw)
{
#if (IR_BLOCK_LEVEL == 1)
    return raw ? 1 : 0;
#else
    return raw ? 0 : 1;
#endif
}

typedef struct {
    // 去抖采样
    uint8_t  last_raw_blocked;
    uint32_t last_change_ms;

    // 稳定状态
    sensor_state_t stable;

    // 事件
    uint8_t rising;
    uint8_t falling;
} sensor_ch_t;

static volatile uint32_t *g_ms = 0;
static sensor_cfg_t cfg;

static sensor_ch_t ch_in;
static sensor_ch_t ch_out;

static void ch_init(sensor_ch_t *ch, uint8_t init_blocked, uint32_t now_ms)
{
    ch->last_raw_blocked = init_blocked;
    ch->last_change_ms = now_ms;
    ch->stable = init_blocked ? SENSOR_BLOCKED : SENSOR_CLEAR;
    ch->rising = 0;
    ch->falling = 0;
}

static void ch_update(sensor_ch_t *ch, uint8_t raw_blocked, uint32_t now_ms)
{
    // 1) 发现 raw 变化：记录变化时刻
    if (raw_blocked != ch->last_raw_blocked)
    {
        ch->last_raw_blocked = raw_blocked;
        ch->last_change_ms = now_ms;
        return;
    }

    // 2) raw 保持不变，超过去抖时间后，才更新 stable
    if ((now_ms - ch->last_change_ms) >= cfg.debounce_ms)
    {
        sensor_state_t new_stable = raw_blocked ? SENSOR_BLOCKED : SENSOR_CLEAR;
        if (new_stable != ch->stable)
        {
            // 产生边沿事件（按“blocked=1”的语义）
            if (ch->stable == SENSOR_CLEAR && new_stable == SENSOR_BLOCKED) ch->rising = 1;
            if (ch->stable == SENSOR_BLOCKED && new_stable == SENSOR_CLEAR) ch->falling = 1;

            ch->stable = new_stable;
        }
    }
}

void sensor_init(volatile uint32_t *ms_tick_ptr, const sensor_cfg_t *c)
{
    g_ms = ms_tick_ptr;

    cfg.debounce_ms = 30;
    if (c) cfg = *c;

    uint32_t now = (g_ms ? *g_ms : 0);

    uint8_t in_blk  = raw_to_blocked(read_raw_in());
    uint8_t out_blk = raw_to_blocked(read_raw_out());

    memset(&ch_in, 0, sizeof(ch_in));
    memset(&ch_out, 0, sizeof(ch_out));
    ch_init(&ch_in, in_blk, now);
    ch_init(&ch_out, out_blk, now);
}

void sensor_poll(void)
{
    if (!g_ms) return;
    uint32_t now = *g_ms;

    uint8_t in_blk  = raw_to_blocked(read_raw_in());
    uint8_t out_blk = raw_to_blocked(read_raw_out());

    ch_update(&ch_in, in_blk, now);
    ch_update(&ch_out, out_blk, now);
}

sensor_state_t sensor_get_state(sensor_id_t id)
{
    return (id == SENSOR_IN) ? ch_in.stable : ch_out.stable;
}

void sensor_get_event(sensor_id_t id, sensor_event_t *out)
{
    if (!out) return;

    sensor_ch_t *ch = (id == SENSOR_IN) ? &ch_in : &ch_out;

    out->state = ch->stable;
    out->rising = ch->rising;
    out->falling = ch->falling;

    // 取完清零
    ch->rising = 0;
    ch->falling = 0;
}
