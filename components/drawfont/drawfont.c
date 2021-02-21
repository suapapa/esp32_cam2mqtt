#include <stdint.h>
#include <string.h>

#include "SmallFont.h"

void draw_string(uint8_t * buf, int w, int h, int x, int y, const char *str,
		 uint8_t color)
{
	Font cfont;
	cfont.w = tft_SmallFont[0];
	cfont.h = tft_SmallFont[1];
	cfont.offset = tft_SmallFont[2];
	cfont.cnt = tft_SmallFont[3];
	cfont.font = &(tft_SmallFont[4]);

	int stl = strlen(str);
	buf = buf + x + (w * y);
	for (int i = 0; i < stl; i++) {
		char c = str[i];
		unsigned char *buf_font =
		    cfont.font + ((c - cfont.offset) * 12);
		uint8_t *buf_draw = buf + (i * cfont.w);
		for (int j = 0; j < cfont.h; j++) {
			uint8_t *buf_draw_char = buf_draw;
			for (int k = 0; k < 8; k++) {
				if ((*buf_font) & (1 << (7 - k))) {
					*buf_draw_char = color;
				}
				buf_draw_char += 1;
			}
			buf_font += 1;
			buf_draw += w;
		}
	}
}
