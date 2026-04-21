#ifndef __PARKING_CLOUD_H
#define __PARKING_CLOUD_H

#include <stdint.h>

#include "parking_db.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PARKING_CLOUD_RES_NONE = 0,
    PARKING_CLOUD_RES_ACTIVE = 1,
    PARKING_CLOUD_RES_STALE = 2,
} parking_cloud_res_state_t;

void parking_cloud_init(volatile uint32_t *ms_tick_ptr);
void parking_cloud_poll(void);
void parking_cloud_force_status(void);
void parking_cloud_on_rx_byte(uint8_t byte);
parking_cloud_res_state_t parking_cloud_lookup_reserved_plate(const char *plate);
void parking_cloud_consume_reserved_plate(const char *plate);
void parking_cloud_publish_enter(const char *plate, uint32_t in_ms, db_slot_type_t slot_type);
void parking_cloud_publish_exit(const char *plate, uint32_t in_ms, uint32_t out_ms, uint32_t fee_cents, db_slot_type_t slot_type);

#ifdef __cplusplus
}
#endif

#endif
