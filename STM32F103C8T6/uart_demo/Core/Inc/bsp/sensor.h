#ifndef __SENSOR_H
#define __SENSOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_CLEAR = 0,     // 未遮挡
    SENSOR_BLOCKED = 1    // 遮挡
} sensor_state_t;

typedef enum {
    SENSOR_IN = 0,
    SENSOR_OUT = 1
} sensor_id_t;

typedef struct {
    sensor_state_t state;     // 当前稳定状态
    uint8_t rising;           // 0->1 边沿（按“遮挡=1”语义）
    uint8_t falling;          // 1->0 边沿
    uint8_t i;  // cam是否需要识别
} sensor_event_t;

typedef struct {
    uint16_t debounce_ms;     // 去抖时间，建议 20~50ms
} sensor_cfg_t;

// 初始化：传入 g_ms 指针
void sensor_init(volatile uint32_t *ms_tick_ptr, const sensor_cfg_t *cfg);

// 主循环周期调用
void sensor_poll(void);

// 读取稳定状态（语义：BLOCKED/CLEAR）
sensor_state_t sensor_get_state(sensor_id_t id);

// 取事件（取完自动清零 rising/falling）
void sensor_get_event(sensor_id_t id, sensor_event_t *out);

#ifdef __cplusplus
}
#endif

#endif
