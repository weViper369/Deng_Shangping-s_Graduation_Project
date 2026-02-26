#include "parking_db.h"
#include "billing.h"
#include <string.h>

static db_record_t g_db[DB_MAX_RECORD];
db_parking_t g_parking_cfg = {
    .capacity = 20,
};
// static db_parking_t db_parking;

void db_init(void)
{
    memset(g_db, 0, sizeof(g_db));
}

static int plate_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return (strncmp(a, b, DB_PLATE_MAX) == 0);
}

int db_find_active(const char *plate)
{
    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (g_db[i].in_use && g_db[i].active && plate_eq(g_db[i].plate, plate))
            return i;
    }
    return -1;
}

static int db_find_writable_slot(void)
{
    // 1) 优先找从未使用的空槽
    for (int i = 0; i < DB_MAX_RECORD; i++)
        if (!g_db[i].in_use)
            return i;

    // 2) 没有空槽：复用最老的“已出场(非active)”记录
    int best = -1;
    uint32_t best_time = 0xFFFFFFFF; // 越小越老

    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (g_db[i].in_use && g_db[i].active == 0)
        {
            // 用 out_ms 作为“完成时间”；如果 out_ms==0，退化用 in_ms
            uint32_t t = (g_db[i].out_ms != 0) ? g_db[i].out_ms : g_db[i].in_ms;
            if (t < best_time)
            {
                best_time = t;
                best = i;
            }
        }
    }
    return best; // 可能为 -1
}

db_ret_t db_enter(const char *plate, uint32_t in_ms)
{
    if (!plate || plate[0] == '\0') return DB_ERR_NOT_FOUND;

    // 重复入场保护
    if (db_find_active(plate) >= 0) return DB_ERR_DUP;

    if (db_count_active() >= g_parking_cfg.capacity)
        return DB_ERR_PARK_FUL;

    // 找空记录槽（写入一条新记录）
    // ★找可写槽（空槽优先；否则覆盖最老已出场记录）
    int idx = db_find_writable_slot();
    if (idx < 0)
        return DB_ERR_FULL; // 极端：capacity 设置不合理（> DB_MAX_RECORD）等

    // 写入（覆盖也没关系：它已是 inactive 历史）
    g_db[idx].in_use = 1;
    g_db[idx].active = 1;

    strncpy(g_db[idx].plate, plate, DB_PLATE_MAX - 1);
    g_db[idx].plate[DB_PLATE_MAX - 1] = '\0';

    g_db[idx].in_ms = in_ms;
    g_db[idx].out_ms = 0;
    g_db[idx].fee_cents = 0;

    return DB_OK;    
}

db_ret_t db_preview_exit(const char *plate, uint32_t out_ms,uint32_t *duration_s, uint32_t *fee_cents)
{
    int idx = db_find_active(plate);
    if (idx < 0) return DB_ERR_NOT_FOUND;

    const db_record_t *r = &g_db[idx];

    uint32_t dur_s = 0;
    if (out_ms >= r->in_ms) dur_s = (out_ms - r->in_ms) / 1000u;

    uint32_t fee = billing_calc_fee_cents(dur_s);

    if (duration_s) *duration_s = dur_s;
    if (fee_cents) *fee_cents = fee;
    return DB_OK;
}

db_ret_t db_commit_exit(const char *plate, uint32_t out_ms,uint32_t *duration_s, uint32_t *fee_cents)
{
    int idx = db_find_active(plate);
    if (idx < 0) return DB_ERR_NOT_FOUND;

    db_record_t *r = &g_db[idx];
    r->out_ms = out_ms;
    r->active = 0;

    uint32_t dur_s = 0;
    if (out_ms >= r->in_ms) dur_s = (out_ms - r->in_ms) / 1000u;

    uint32_t fee = billing_calc_fee_cents(dur_s);
    r->fee_cents = fee;

    if (duration_s) *duration_s = dur_s;
    if (fee_cents) *fee_cents = fee;
    return DB_OK;
}

int db_count_active(void)
{
    int n = 0;
    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (g_db[i].in_use && g_db[i].active) 
            n++;
    }
    return n;
}

int db_capacity(void)
{
    return g_parking_cfg.capacity;
}
const db_record_t* db_get(int idx)
{
    if (idx < 0 || idx >= DB_MAX_RECORD) return 0;
    if (!g_db[idx].in_use) return 0;
    return &g_db[idx];
}
