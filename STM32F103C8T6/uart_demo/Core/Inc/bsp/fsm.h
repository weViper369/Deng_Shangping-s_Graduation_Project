#ifndef __FSM_H
#define __FSM_H
#include <stdint.h>

typedef enum
{
    S_IDLE_WAIT_PLATE = 0, 
    // 空闲状态：等待车牌识别事件
    // 条件：maix_link 给出 plate_event
    // 动作：调用 gate_open()，进入 S_GATE_OPENING

    S_GATE_OPENING,
    // 正在开闸（非阻塞）
    // 条件1：gate_is_open() == true → 进入 S_WAIT_CAR_BLOCKED
    // 条件2：开闸超时（>2s） → 保护性关闸，进入 S_GATE_CLOSING

    S_WAIT_CAR_BLOCKED_IN,
    // 等待车辆进入，触发入口对射“遮挡”
    // 条件1：检测到对射 blocked → 进入 S_WAIT_CAR_CLEAR
    // 条件2：长时间未检测到车辆（如10s）→ 自动关闸，进入 S_GATE_CLOSING

    S_WAIT_CAR_CLEAR_IN,
    // 车辆正在通过，等待对射恢复为“未遮挡”
    // 条件1：对射 clear → 进入 S_DELAY_BEFORE_CLOSE
    // 条件2：长时间持续遮挡（异常）→ 强制关闸，进入 S_GATE_CLOSING
    
    S_WAIT_CAR_BLOCKED_OUT,
    S_WAIT_CAR_CLEAR_OUT,
    
    S_DELAY_BEFORE_CLOSE,
    // 防误关延时（例如500ms）
    // 作用：避免车辆尾部刚通过时立即落杆
    // 条件：延时到达 → 调用 gate_close()，进入 S_GATE_CLOSING

    S_GATE_CLOSING,
    // 正在关闸（非阻塞）
    // 条件1：gate_is_closed() == true → 回到 S_IDLE_WAIT_PLATE
    // 条件2：关闸超时 → 直接回到 S_IDLE_WAIT_PLATE（保护）
    S_WAIT_PAY_CONFIRM,
    // 等待缴费确认（出场）
} fsm_state_t;

typedef enum
{
    LANE_IN = 0,
    LANE_OUT = 1
}lane_t;

void fsm_init(volatile uint32_t *ms_tick_ptr);
void fsm_step(void);

// 给 FSM 注入事件（后面接真实 maix_link/sensor 时就用这些入口）
void fsm_on_plate(const char *plate, uint8_t conf,lane_t lane);
void fsm_on_ir_in_blocked(uint8_t blocked); // 1=遮挡，0=恢复
void fsm_on_ir_out_blocked(uint8_t blocked);

fsm_state_t fsm_get_state(void);
#endif
