/* PRD 4: the glyph strings the port composes, in the game's font (a main glyph is one byte, an extra one two: `01 xx`;
 * the digits are glyph 4 + d). A window keeps the pointer it is given, so callers keep their texts in static buffers;
 * every function here appends at buf[*p], never past `size` - 1, and keeps the text 0-ended. */
#include <stddef.h>

#include "definitive.h"

extern const unsigned char def_str_HuntRomanI[], def_str_HuntRomanV[], def_str_HuntRomanX[];

void def_glyph_put(unsigned char *buf, int *p, int size, const unsigned char *s) {
    while (s != NULL && *s != 0 && *p < size - 1) {
        buf[(*p)++] = *s++;
    }
    buf[*p] = 0;
}

void def_glyph_count(unsigned char *buf, int *p, int size, int n) {
    n = n < 0 ? 0 : n > 99 ? 99 : n;
    if (n >= 10 && *p < size - 2) {
        buf[(*p)++] = (unsigned char)(4 + n / 10);
    }
    if (*p < size - 1) {
        buf[(*p)++] = (unsigned char)(4 + n % 10);
    }
    buf[*p] = 0;
}

void def_glyph_roman(unsigned char *buf, int *p, int size, int n) {
    static const char *const units[10] = { "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" };
    const char *u;

    for (; n >= 10; n -= 10) {
        def_glyph_put(buf, p, size, def_str_HuntRomanX);
    }
    for (u = units[n < 0 ? 0 : n]; *u != 0; u++) {
        def_glyph_put(buf, p, size, *u == 'I' ? def_str_HuntRomanI : def_str_HuntRomanV);
    }
}
