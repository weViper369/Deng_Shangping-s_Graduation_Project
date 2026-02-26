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
    DB_ERR_DUP  = -2,   // 重复入场
    DB_ERR_NOT_FOUND = -3,
    DB_ERR_FULL = -4, // 
} db_ret_t;

typedef struct {
    uint8_t  in_use;                // 1=有效
    uint8_t  active;                // 1=在场
    char     plate[DB_PLATE_MAX];   // UTF-8
    uint32_t in_ms;
    uint32_t out_ms;
    uint32_t fee_cents;
} db_record_t;

typedef struct{
    uint8_t capacity; // 车位数量
}db_parking_t;

void db_init(void);

// 入场：创建在场记录
db_ret_t db_enter(const char *plate, uint32_t in_ms);

// 查询在场记录，返回 index（>=0）或 <0
int db_find_active(const char *plate);

// 仅预览：不改变记录
db_ret_t db_preview_exit(const char *plate,uint32_t out_ms,uint32_t *duration_s,uint32_t *fee_cents);

// 真正提交：改变记录（active=0, out_ms, fee）
db_ret_t db_commit_exit(const char *plate, uint32_t out_ms, uint32_t *duration_s,uint32_t *fee_cents);

// 在场车辆数
int db_count_active(void);
// 总车位数
int db_capacity(void);

// 可选：取记录（用于调试/显示）
const db_record_t* db_get(int idx);

#ifdef __cplusplus
}
#endif

#endif 

