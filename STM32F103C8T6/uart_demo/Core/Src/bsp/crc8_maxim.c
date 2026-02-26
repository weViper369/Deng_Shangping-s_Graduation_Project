#include "crc8_maxim.h"

uint8_t crc8_maxim(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x01) crc = (uint8_t)((crc >> 1) ^ 0x8C);
            else           crc = (uint8_t)(crc >> 1);
        }
    }
    return crc;
}
