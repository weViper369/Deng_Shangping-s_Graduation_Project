#ifndef __BILLING_H
#define __BILLING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t free_s;        // 免费秒数
    uint32_t unit_s;        // 计费单位秒数（向上取整）
    uint32_t unit_fee_cents;// 每单位费用（分）
} billing_cfg_t;

void billing_set_cfg(const billing_cfg_t *cfg);
uint32_t billing_calc_fee_cents(uint32_t duration_s);

#ifdef __cplusplus
}
#endif
#endif 

