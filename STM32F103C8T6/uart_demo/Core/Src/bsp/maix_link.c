#include "maix_link.h"
#include "rb.h"
#include "maix_parser.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

// === 配置区 ===
#define RB_SIZE            1024u
#define PARSER_TIMEOUT_MS  100u   // 半包超时（防止卡死）
#define PLATE_MAX_BYTES    31u    // plate[] 最大可放 31 字节 + '\0'

// === 内部对象 ===
static uint8_t rb_mem[RB_SIZE];
static rb_t rb1;

static maix_parser_t parser;
static maix_frame_t  fr;

static maix_plate_event_t evt;
static maix_link_stats_t  st;

static volatile uint32_t *g_ms_ptr = 0;
static uint32_t last_byte_ms = 0;

static uint8_t g_tx_seq = 0;

void maix_link_send_req_ocr(uint8_t lane)
{
    uint8_t buf[2 + 1+1+1+2 + 2 + 1];
    uint16_t i = 0;

    buf[i++] = 0xAA;
    buf[i++] = 0x55;

    uint8_t ver  = 0x01;
    uint8_t type = 0x10;
    uint8_t seq  = g_tx_seq++;

    uint16_t len = 2;

    buf[i++] = ver;
    buf[i++] = type;
    buf[i++] = seq;

    buf[i++] = (uint8_t)(len & 0xFF);
    buf[i++] = (uint8_t)(len >> 8);

    buf[i++] = (lane ? 0x01 : 0x00); // LANE
    buf[i++] = 0x00;                // RESERVED

    // CRC: from VER to end of payload
    uint8_t crc = crc8_maxim(&buf[2], (uint16_t)(1+1+1+2+2));
    buf[i++] = crc;

    HAL_UART_Transmit(&huart1, buf, i, 100);
    printf("[TX] REQ_OCR lane=%u seq=%u crc=%02X\r\n", (unsigned)lane, (unsigned)seq, crc);
}

void maix_link_init(volatile uint32_t *ms_tick_ptr)
{
    g_ms_ptr = ms_tick_ptr;

    rb_init(&rb1, rb_mem, (uint16_t)sizeof(rb_mem));
    maix_parser_init(&parser);

    memset(&evt, 0, sizeof(evt));
    memset(&st, 0, sizeof(st));

    last_byte_ms = (g_ms_ptr ? *g_ms_ptr : 0);
}

void maix_link_reset_stats(void)
{
    memset(&st, 0, sizeof(st));
    // parser 统计也一起清
    parser.bytes_in = 0;
    parser.frames_ok = 0;
    parser.frames_bad_crc = 0;
    parser.frames_drop = 0;
}

void maix_link_get_stats(maix_link_stats_t *out)
{
    if (!out) return;
    // 聚合 parser 统计
    st.bytes_in      = parser.bytes_in;
    st.frames_ok     = parser.frames_ok;
    st.frames_bad_crc= parser.frames_bad_crc;
    st.frames_drop   = parser.frames_drop;
    *out = st;
}

void maix_link_on_rx_byte(uint8_t b)
{
    if (!rb_push_isr(&rb1, b))
        st.rb_overflow++;

    if (g_ms_ptr)
        last_byte_ms = *g_ms_ptr;
}

static void parser_timeout_check(void)
{
    if (!g_ms_ptr) return;
    uint32_t now = *g_ms_ptr;
    if (now - last_byte_ms > PARSER_TIMEOUT_MS)
    {
        // 超时就丢弃当前组帧，重新找 AA55
        parser.state = 0; // ST_WAIT_AA
        parser.len = 0;
        parser.idx = 0;
        last_byte_ms = now;
    }
}

void maix_link_poll(void)
{
    parser_timeout_check();

    uint8_t b;
    while (rb_pop(&rb1, &b))
    {
        if (maix_parser_feed(&parser, b, &fr))
        {
            // 只关心 TYPE=0x01（车牌识别结果）
            if (fr.type == 0x01 && fr.len >= 3)
            {
                uint8_t lane = fr.payload[0];
                uint8_t conf = fr.payload[1];
                uint8_t plen = fr.payload[2];
                printf("[OCR] rx type=0x01 len=%u b0=%u b1=%u b2=%u\r\n",fr.len, fr.payload[0], fr.payload[1], fr.payload[2]);
                if ((uint16_t)(3u + plen) <= fr.len)
                {
                    // 拷贝 plate（UTF-8 原样拷贝，不做编码转换）
                    uint8_t copy_len = plen;
                    if (copy_len > PLATE_MAX_BYTES) copy_len = PLATE_MAX_BYTES;

                    memcpy(evt.plate, &fr.payload[3], copy_len);
                    evt.plate[copy_len] = '\0';
                    evt.conf = conf;
                    evt.lane = lane;
                    evt.valid = 1;
                }
            }
        }
    }
}

int maix_link_get_plate(maix_plate_event_t *out)
{
    if (!out) return 0;
    if (!evt.valid) return 0;

    *out = evt;
    evt.valid = 0;
    return 1;
}
