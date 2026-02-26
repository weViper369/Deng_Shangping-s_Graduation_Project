#include "billing.h"

static billing_cfg_t g_cfg = {
    .free_s = 5u * 60u,
    .unit_s = 30u * 60u,
    .unit_fee_cents = 100u
};

void billing_set_cfg(const billing_cfg_t *cfg)
{
    if (!cfg) return;
    g_cfg = *cfg;
}

// 免费 5 分钟；之后每 30 分钟 100 分（1元），不足 30 分钟按 30 分钟算
uint32_t billing_calc_fee_cents(uint32_t duration_s)
{
    if (duration_s <= g_cfg.free_s) return 0;

    uint32_t bill_s = duration_s - g_cfg.free_s;

    // 向上取整
    uint32_t units = (bill_s + g_cfg.unit_s - 1u) / g_cfg.unit_s;
    return units * g_cfg.unit_fee_cents;
}
