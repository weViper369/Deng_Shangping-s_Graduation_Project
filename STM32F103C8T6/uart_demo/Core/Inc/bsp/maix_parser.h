#ifndef __MAIX_PARSER_H
#define __MAIX_PARSER_H

#include <stdint.h>

#define MAIX_MAX_PAYLOAD   256
#define MAIX_MAX_FRAME     (2u + 1u + 1u + 1u + 2u + MAIX_MAX_PAYLOAD + 1u)

typedef struct {
    // 统计
    uint32_t frames_ok;
    uint32_t frames_bad_crc;
    uint32_t frames_drop;
    uint32_t bytes_in;

    // 解析状态
    uint8_t  state;
    uint8_t  hdr2;          // 0x55
    uint8_t  ver;
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint16_t idx;

    // 缓冲：保存 VER..PAYLOAD 用于 CRC（不含 AA55）
    uint8_t  buf[1u + 1u + 1u + 2u + MAIX_MAX_PAYLOAD]; // VER,TYPE,SEQ,LEN_L,LEN_H,PAYLOAD
} maix_parser_t;

typedef struct {
    uint8_t  ver;
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    const uint8_t *payload; // 指向 parser 内部 buf 的 payload 区
} maix_frame_t;

void maix_parser_init(maix_parser_t *p);

// 喂一个字节：返回 1 表示得到完整帧（out 有效），否则 0
int maix_parser_feed(maix_parser_t *p, uint8_t byte, maix_frame_t *out);

#endif 
