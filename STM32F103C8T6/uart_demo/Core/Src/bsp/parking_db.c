#include "parking_db.h"
#include "billing.h"

#include <string.h>

static db_record_t g_db[DB_MAX_RECORD];
db_parking_t g_parking_cfg = {
    .normal_capacity = 20,
    .reserved_capacity = 5,
};

void db_init(void)
{
    memset(g_db, 0, sizeof(g_db));
}

static int plate_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return strncmp(a, b, DB_PLATE_MAX) == 0;
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
    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (!g_db[i].in_use)
            return i;
    }

    int best = -1;
    uint32_t best_time = 0xFFFFFFFFu;

    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (g_db[i].in_use && g_db[i].active == 0)
        {
            uint32_t t = (g_db[i].out_ms != 0) ? g_db[i].out_ms : g_db[i].in_ms;
            if (t < best_time)
            {
                best_time = t;
                best = i;
            }
        }
    }

    return best;
}

db_ret_t db_enter(const char *plate, uint32_t in_ms, db_slot_type_t slot_type)
{
    if (!plate || plate[0] == '\0') return DB_ERR_NOT_FOUND;
    if (db_find_active(plate) >= 0) return DB_ERR_DUP;

    if (db_count_active_by_type(slot_type) >= db_capacity_by_type(slot_type))
        return DB_ERR_PARK_FUL;

    int idx = db_find_writable_slot();
    if (idx < 0)
        return DB_ERR_FULL;

    g_db[idx].in_use = 1;
    g_db[idx].active = 1;
    g_db[idx].slot_type = (uint8_t)slot_type;
    strncpy(g_db[idx].plate, plate, DB_PLATE_MAX - 1);
    g_db[idx].plate[DB_PLATE_MAX - 1] = '\0';
    g_db[idx].in_ms = in_ms;
    g_db[idx].out_ms = 0;
    g_db[idx].fee_cents = 0;

    return DB_OK;
}

db_ret_t db_preview_exit(const char *plate, uint32_t out_ms, uint32_t *duration_s, uint32_t *fee_cents)
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

db_ret_t db_commit_exit(const char *plate, uint32_t out_ms, uint32_t *duration_s, uint32_t *fee_cents)
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

int db_count_active_by_type(db_slot_type_t slot_type)
{
    int n = 0;
    for (int i = 0; i < DB_MAX_RECORD; i++)
    {
        if (g_db[i].in_use && g_db[i].active && g_db[i].slot_type == (uint8_t)slot_type)
            n++;
    }
    return n;
}

int db_capacity(void)
{
    return (int)g_parking_cfg.normal_capacity + (int)g_parking_cfg.reserved_capacity;
}

int db_capacity_by_type(db_slot_type_t slot_type)
{
    return (slot_type == DB_SLOT_RESERVED)
        ? (int)g_parking_cfg.reserved_capacity
        : (int)g_parking_cfg.normal_capacity;
}

const db_record_t *db_get(int idx)
{
    if (idx < 0 || idx >= DB_MAX_RECORD) return 0;
    if (!g_db[idx].in_use) return 0;
    return &g_db[idx];
}
