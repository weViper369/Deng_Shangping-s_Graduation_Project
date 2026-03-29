#ifndef __PARKING_CLOUD_H
#define __PARKING_CLOUD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void parking_cloud_init(volatile uint32_t *ms_tick_ptr);
void parking_cloud_poll(void);
void parking_cloud_force_status(void);
void parking_cloud_publish_enter(const char *plate, uint32_t in_ms);
void parking_cloud_publish_exit(const char *plate, uint32_t in_ms, uint32_t out_ms, uint32_t fee_cents);

#ifdef __cplusplus
}
#endif

#endif
