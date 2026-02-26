#include "rb.h"

static inline uint16_t mask(const rb_t *rb) 
{ 
    return (uint16_t)(rb->size - 1); 
}

void rb_init(rb_t *rb, uint8_t *storage, uint16_t size_pow2)
{
    rb->buf = storage;
    rb->size = size_pow2;
    rb->head = 0;
    rb->tail = 0;
}

uint16_t rb_count(const rb_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & mask(rb));
}

uint16_t rb_free(const rb_t *rb)
{
    return (uint16_t)(rb->size - 1 - rb_count(rb));
}

int rb_push_isr(rb_t *rb, uint8_t b)
{
    uint16_t next = (uint16_t)((rb->head + 1) & mask(rb));
    if (next == rb->tail) return 0; // full
    rb->buf[rb->head] = b;
    rb->head = next;
    return 1;
}

int rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb->tail == rb->head) 
        return 0;
    *out = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1) & mask(rb));
    return 1;
}
