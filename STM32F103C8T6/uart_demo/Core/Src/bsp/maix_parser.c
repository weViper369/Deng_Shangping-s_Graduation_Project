#include "maix_parser.h"
#include "crc8_maxim.h"

enum {
    ST_WAIT_AA = 0,
    ST_WAIT_55,
    ST_VER,
    ST_TYPE,
    ST_SEQ,
    ST_LEN_L,
    ST_LEN_H,
    ST_PAYLOAD,
    ST_CRC
};

void maix_parser_init(maix_parser_t *p)
{
    p->frames_ok = 0;
    p->frames_bad_crc = 0;
    p->frames_drop = 0;
    p->bytes_in = 0;

    p->state = ST_WAIT_AA;
    p->len = 0;
    p->idx = 0;
}

static void drop_and_resync(maix_parser_t *p)
{
    p->frames_drop++;
    p->state = ST_WAIT_AA;
    p->len = 0;
    p->idx = 0;
}

int maix_parser_feed(maix_parser_t *p, uint8_t byte, maix_frame_t *out)
{
    p->bytes_in++;

    switch (p->state)
    {
    case ST_WAIT_AA:
        if (byte == 0xAA) p->state = ST_WAIT_55;
        break;

    case ST_WAIT_55:
        if (byte == 0x55) p->state = ST_VER;
        else p->state = ST_WAIT_AA;
        break;

    case ST_VER:
        p->ver = byte;
        p->buf[0] = byte;              // VER
        p->idx = 1;
        p->state = ST_TYPE;
        break;

    case ST_TYPE:
        p->type = byte;
        p->buf[p->idx++] = byte;       // TYPE
        p->state = ST_SEQ;
        break;

    case ST_SEQ:
        p->seq = byte;
        p->buf[p->idx++] = byte;       // SEQ
        p->state = ST_LEN_L;
        break;

    case ST_LEN_L:
        p->len = byte;
        p->buf[p->idx++] = byte;       // LEN_L
        p->state = ST_LEN_H;
        break;

    case ST_LEN_H:
        p->len |= (uint16_t)((uint16_t)byte << 8);
        p->buf[p->idx++] = byte;       // LEN_H

        if (p->len > MAIX_MAX_PAYLOAD)
        {
            drop_and_resync(p);
            break;
        }
        p->state = (p->len == 0) ? ST_CRC : ST_PAYLOAD;
        break;

    case ST_PAYLOAD:
        // buf 当前 idx 指向 payload 区开头后的位置
        p->buf[p->idx++] = byte;
        if ((uint16_t)(p->idx - 5u) >= p->len) // 5=VER,TYPE,SEQ,LEN_L,LEN_H
        {
            p->state = ST_CRC;
        }
        break;

    case ST_CRC:
    {
        uint8_t calc = crc8_maxim(p->buf, (size_t)(5u + p->len)); // VER..PAYLOAD
        if (calc == byte)
        {
            p->frames_ok++;
            out->ver = p->ver;
            out->type = p->type;
            out->seq = p->seq;
            out->len = p->len;
            out->payload = &p->buf[5]; // payload 起始
            p->state = ST_WAIT_AA;
            return 1;
        }
        else
        {
            p->frames_bad_crc++;
            // CRC 错：直接丢弃并重新找 AA55
            p->state = ST_WAIT_AA;
        }
        break;
    }

    default:
        drop_and_resync(p);
        break;
    }

    return 0;
}
