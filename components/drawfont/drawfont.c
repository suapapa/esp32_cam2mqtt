#include <stdint.h>
#include <string.h>

#include "SmallFont.h"

void draw_string(uint8_t * buf, int w, int h, int x, int y, const char *str)
{
	Font cfont;
	cfont.w = tft_SmallFont[0];
	cfont.h = tft_SmallFont[1];
	cfont.offset = tft_SmallFont[2];
	cfont.cnt = tft_SmallFont[3];
	cfont.font = &(tft_SmallFont[4]);

	int stl = strlen(str);
	buf = buf + x + (w / 8 * y);
	for (int i = 0; i < stl; i++) {
		char c = str[i];
		unsigned char *buf_font =
		    cfont.font + (c - cfont.offset * cfont.h);
		uint8_t *buf_draw = buf + i;

		for (int j = 0; j < cfont.h; j++) {
			*buf_draw |= *buf_font;
			buf_draw += w / 8;
		}
	}
}
