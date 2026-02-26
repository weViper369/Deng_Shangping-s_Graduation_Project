#ifndef __SSD1306_H
#define __SSD1306_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== 你只需要改这里 ======
#define SSD1306_I2C_ADDR   (0x3C << 1)  // 常见 0x3C 或 0x3D
#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     64           // 128x32 就改成 32
// ===========================

void ssd1306_init(void);
void ssd1306_fill(uint8_t color);              // 0=黑 1=白
void ssd1306_update(void);

void ssd1306_draw_pixel(int x, int y, uint8_t color);
void ssd1306_draw_char(int x, int y, char c);  // 6x8
void ssd1306_draw_str(int x, int y, const char *s);
void oled_draw_16x16_hz(int x, int y, const uint8_t hz[32]);
void draw_plate_utf8(int x, int y, const char *plate);
int oled_draw_cn16(int x, int y, const uint16_t *ucs, int count);


#ifdef __cplusplus
}
#endif
#endif
