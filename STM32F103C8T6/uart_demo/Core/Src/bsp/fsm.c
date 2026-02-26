#include "fsm.h"
#include "gate.h"
#include "parking_db.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "btn.h"

static volatile uint32_t *g_ms = 0;

static lane_t last_lane = LANE_OUT;
static lane_t gate_lane = LANE_OUT; // 当前开闸属于入口还是出口
static fsm_state_t st = S_IDLE_WAIT_PLATE;

static uint8_t  ir_in_blocked = 0;   // 模拟/真实入口对射状态
static uint8_t  has_plate = 0;
static char     last_plate[33];
static uint8_t  last_conf = 0;

static uint8_t  pending_pay = 0;
static uint32_t pending_out_ms = 0;
static uint32_t pending_fee_cents = 0;
static uint32_t pending_dur_s = 0;
static char     pending_plate[32];

static uint8_t ir_out_blocked = 0;
static uint32_t t_state = 0;


static void goto_state(fsm_state_t s)
{
    st = s;
    t_state = (g_ms ? *g_ms : 0);
    printf("[FSM] -> %d\r\n", (int)st);
}

void fsm_init(volatile uint32_t *ms_tick_ptr)
{
    g_ms = ms_tick_ptr;
    st = S_IDLE_WAIT_PLATE;
    ir_in_blocked = 0;
    has_plate = 0;
    memset(last_plate, 0, sizeof(last_plate));
    last_conf = 0;
    t_state = (g_ms ? *g_ms : 0);
    printf("FSM init\r\n");
}

fsm_state_t fsm_get_state(void) 
{ 
    return st; 
}

void fsm_on_plate(const char *plate, uint8_t conf,lane_t lane)
{
    if (!plate) return;
    strncpy(last_plate, plate, sizeof(last_plate)-1);
    last_plate[sizeof(last_plate)-1] = '\0';
    last_conf = conf;
    last_lane = lane;
    has_plate = 1;
    printf("[EVT] lane=%d plate=%s conf=%u\r\n", (int)lane, last_plate, last_conf);
}

void fsm_on_ir_in_blocked(uint8_t blocked)
{
    ir_in_blocked = blocked ? 1 : 0;
    printf("[EVT] IR_IN=%u\r\n", ir_in_blocked);
}

void fsm_on_ir_out_blocked(uint8_t blocked)
{
    ir_out_blocked = blocked ? 1 : 0;
    printf("[EVT] IR_OUT=%u\r\n", ir_out_blocked);
}

void fsm_step(void)
{
    uint32_t now = (g_ms ? *g_ms : 0);

    // gate 模块也要 poll
    gate_poll();

    switch (st)
    {
        case S_IDLE_WAIT_PLATE:
            if (has_plate)
            {
                has_plate = 0;
                if (last_lane == LANE_IN)
                {
                    db_ret_t r = db_enter(last_plate, now);
                    switch(r)
                    {
                        case DB_OK:
                            printf("[FSM] open gate for %s (%u)\r\n", last_plate, last_conf);
                            gate_lane = LANE_IN;
                            ui_on_in_plate(last_plate,now);
                            gate_open();
                            goto_state(S_GATE_OPENING);
                            break;
                        case DB_ERR_DUP:
                            printf("[FSM] ENTER REJECT: DUP plate=%s\r\n", last_plate);   
                            ui_on_error("DUP", last_plate, now);                     
                            break;
                        case DB_ERR_PARK_FUL:
                            printf("[FSM] enter REJECT: DB FULL\r\n");
                            ui_on_error("FULL", last_plate, now);
                            // 不开闸
                            break;
                        default:
                            printf("[FSM] enter REJECT: unknown err=%d\r\n", (int)r);
                            break;
        
                    }
                }
                else
                {
                    uint32_t dur_s = 0, fee = 0;
                    db_ret_t rr = db_preview_exit(last_plate, now, &dur_s, &fee);
                    if (rr == DB_OK)
                    {
                        pending_pay = 1;
                        pending_out_ms = now; // 记住出场时间戳
                        pending_dur_s = dur_s;
                        pending_fee_cents = fee;
                        strncpy(pending_plate, last_plate, sizeof(pending_plate)-1);
                        pending_plate[sizeof(pending_plate)-1] = '\0';

                        printf("[FSM] EXIT bill plate=%s dur=%lus fee=%lu cents\r\n",last_plate, (unsigned long)dur_s, (unsigned long)fee);
                        ui_on_pay_wait(pending_plate, pending_dur_s, pending_fee_cents, now);                        
                        goto_state(S_WAIT_PAY_CONFIRM);
                    }
                    else
                    {
                        printf("[FSM] EXIT REJECT: NOT FOUND plate=%s\r\n", last_plate);
                        ui_on_error("NOTFOUND", last_plate, now);
                    }
                }
            }
            break;

        case S_GATE_OPENING:
            if (gate_is_open()) 
            {
                if (gate_lane == LANE_IN)  
                    goto_state(S_WAIT_CAR_BLOCKED_IN);
                else                       
                    goto_state(S_WAIT_CAR_BLOCKED_OUT);           
            } 
            else if (now - t_state > 2000) 
            { // 动作超时保护
                printf("[FSM] open timeout -> close\r\n");
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;

        case S_WAIT_CAR_BLOCKED_IN:
            if (ir_in_blocked) 
            {
                goto_state(S_WAIT_CAR_CLEAR_IN);
            } 
            else if (now - t_state > 10000) 
            { // 等车进入超时
                printf("[FSM] wait blocked timeout -> close\r\n");
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;

        case S_WAIT_CAR_CLEAR_IN:
            if (!ir_in_blocked) 
            {
                goto_state(S_DELAY_BEFORE_CLOSE);
            } 
            else if (now - t_state > 20000) 
            { // 长时间遮挡保护
                printf("[FSM] wait clear timeout -> close\r\n");
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;

        case S_WAIT_CAR_BLOCKED_OUT:
            if (ir_out_blocked) 
            {
                goto_state(S_WAIT_CAR_CLEAR_OUT);
            } 
            else if (now - t_state > 10000) 
            {
                printf("[FSM] OUT wait blocked timeout -> close\r\n");
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;
        
        case S_WAIT_CAR_CLEAR_OUT:
            if (!ir_out_blocked) 
            {
                goto_state(S_DELAY_BEFORE_CLOSE);
            } 
            else if (now - t_state > 20000) 
            {
                printf("[FSM] OUT wait clear timeout -> close\r\n");
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;   

        case S_DELAY_BEFORE_CLOSE:
            if (now - t_state > 500) 
            {
                gate_close();
                goto_state(S_GATE_CLOSING);
            }
            break;

        case S_GATE_CLOSING:
            if (gate_is_closed()) 
            {
                goto_state(S_IDLE_WAIT_PLATE);           
            } 
            else if (now - t_state > 2000) 
            {
                printf("[FSM] close timeout -> idle\r\n");
                goto_state(S_IDLE_WAIT_PLATE);
            }
            break;

        case S_WAIT_PAY_CONFIRM:
            // 按键模拟支付成功
            if (pending_pay && g_pay_btn_event)
            {
                g_pay_btn_event = 0; // 重要：消费事件，立刻清零
        
                uint32_t dur_s=0, fee=0;
                db_ret_t cr = db_commit_exit(pending_plate, pending_out_ms, &dur_s, &fee);
        
                if (cr == DB_OK)
                {
                    printf("[FSM] PAY OK (btn) commit OK plate=%s fee=%lu cents\r\n",pending_plate, (unsigned long)fee);
                }
                else
                {
                    printf("[FSM] PAY OK but commit fail err=%d\r\n", (int)cr);
                }
        
                pending_pay = 0;
                gate_lane = LANE_OUT;
                ui_on_pay_ok(pending_plate, pending_fee_cents, now);
                gate_open();
                goto_state(S_GATE_OPENING);
            }
            break; 
    }
}
