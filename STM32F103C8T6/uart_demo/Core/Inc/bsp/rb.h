#ifndef __RB_H
#define __RB_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buf;
    uint16_t size;          // 必须是 2 的幂
    volatile uint16_t head; // 写指针
    volatile uint16_t tail; // 读指针
} rb_t;

void rb_init(rb_t *rb, uint8_t *storage, uint16_t size_pow2);
uint16_t rb_count(const rb_t *rb);
uint16_t rb_free(const rb_t *rb);

int rb_push_isr(rb_t *rb, uint8_t b);  // ISR 用：满了返回 0
int rb_pop(rb_t *rb, uint8_t *out);    // 主循环用：空了返回 0

#endif
