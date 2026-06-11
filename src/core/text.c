#include "text.h"
#include "../thirdparty/font8x8_basic.h"
#include <string.h>

void sr_text(sr_fb *fb, int x, int y, const char *s, uint8_t color)
{
    for (; *s; s++, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (ch > 127)
            ch = '?';
        const char *glyph = font8x8_basic[ch];
        for (int j = 0; j < 8; j++) {
            int yy = y + j;
            if (yy < 0 || yy >= SR_SCREEN_H)
                continue;
            uint8_t row = (uint8_t)glyph[j];
            for (int i = 0; i < 8; i++) {
                if (!(row & (1u << i)))
                    continue;
                int xx = x + i;
                if (xx >= 0 && xx < SR_SCREEN_W)
                    fb->px[yy * SR_SCREEN_W + xx] = color;
            }
        }
    }
}

int sr_text_width(const char *s)
{
    return (int)strlen(s) * 8;
}
