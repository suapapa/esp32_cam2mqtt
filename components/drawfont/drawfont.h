#include <stdint.h>

#define FONTCOLOR_BLACK 0x00
#define FONTCOLOR_WHITE 0xff

void set_defaultfont(void);
void draw_string(uint8_t * buf, int w, int h, int x, int y, const char *str, uint8_t color);
