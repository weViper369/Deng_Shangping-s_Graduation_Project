#ifndef __GATE_H
#define __GATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GATE_CLOSED = 0,
    GATE_OPEN   = 1,
    GATE_MOVING = 2
} gate_state_t;

typedef struct {
    uint16_t pwm_us_open;     // 开闸脉宽（us），例如 2000
    uint16_t pwm_us_close;    // 关闸脉宽（us），例如 1000
    uint16_t move_time_ms;    // 预估动作时间（ms），例如 600~1000
    uint8_t  invert;          // 1=交换开/关
} gate_cfg_t;

// 初始化：传入 1ms tick 指针（你的 g_ms），以及配置
void gate_init(volatile uint32_t *ms_tick_ptr, const gate_cfg_t *cfg);

// 周期调用（主循环），用于完成“动作到位”判定（非阻塞）
void gate_poll(void);

// 命令（非阻塞）：开始开/关闸
void gate_open(void);
void gate_close(void);

// 查询状态
gate_state_t gate_get_state(void);
uint8_t gate_is_open(void);
uint8_t gate_is_closed(void);

#ifdef __cplusplus
}
#endif

#endif 
