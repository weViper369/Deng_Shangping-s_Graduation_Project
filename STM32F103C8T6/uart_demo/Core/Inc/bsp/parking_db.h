#ifndef __PARKING_DB_H
#define __PARKING_DB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DB_PLATE_MAX  32
#define DB_MAX_RECORD 50

typedef enum {
    DB_OK = 0,
    DB_ERR_PARK_FUL = -1,
    DB_ERR_DUP = -2,
    DB_ERR_NOT_FOUND = -3,
    DB_ERR_FULL = -4,
} db_ret_t;

typedef enum {
    DB_SLOT_NORMAL = 0,
    DB_SLOT_RESERVED = 1,
} db_slot_type_t;

typedef struct {
    uint8_t in_use;
    uint8_t active;
    uint8_t slot_type;
    char plate[DB_PLATE_MAX];
    uint32_t in_ms;
    uint32_t out_ms;
    uint32_t fee_cents;
} db_record_t;

typedef struct {
    uint8_t normal_capacity;
    uint8_t reserved_capacity;
} db_parking_t;

void db_init(void);
db_ret_t db_enter(const char *plate, uint32_t in_ms, db_slot_type_t slot_type);
int db_find_active(const char *plate);
db_ret_t db_preview_exit(const char *plate, uint32_t out_ms, uint32_t *duration_s, uint32_t *fee_cents);
db_ret_t db_commit_exit(const char *plate, uint32_t out_ms, uint32_t *duration_s, uint32_t *fee_cents);
int db_count_active(void);
int db_count_active_by_type(db_slot_type_t slot_type);
int db_capacity(void);
int db_capacity_by_type(db_slot_type_t slot_type);
const db_record_t *db_get(int idx);

#ifdef __cplusplus
}
#endif

#endif
