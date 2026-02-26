#ifndef __UI_H
#define __UI_H

#include <stdint.h>
#include "fsm.h"

void ui_init(void);
void ui_tick(uint32_t now_ms);  // 主循环周期调用（建议 5~10Hz）
void ui_on_in_plate(const char *plate, uint32_t now_ms);                         // 入场显示车牌
void ui_on_out_bill(const char *plate, uint32_t dur_s, uint32_t fee_cents, uint32_t now_ms);  // 废弃了
void ui_on_pay_wait(const char *plate, uint32_t dur_s,uint32_t fee_cents, uint32_t now_ms);
void ui_on_pay_ok(const char *plate, uint32_t fee_cents, uint32_t now_ms);// 出场显示车牌+费用+时间
void ui_on_error(const char *msg, const char *plate, uint32_t now_ms);
void ui_on_state(fsm_state_t st);

#endif
