/* BMFont v3 binary parser + layout (portable; modes.md A.1.2-A.1.3).
 * Same format the PC loads with CFontLoaderBinaryFormat (FUN_004944a0). */
#ifndef AOS_APP_BMFONT_H
#define AOS_APP_BMFONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint32_t id;
	uint16_t x, y, w, h;
	int16_t xoff, yoff, xadv;
	uint8_t page;
} bm_glyph;

typedef struct {
	uint32_t first, second;
	int16_t amount;
} bm_kern;

typedef struct {
	int line_height, base, scale_w, scale_h, outline, pages;
	char page_name[4][64];
	bm_glyph *glyphs; /* sorted by id */
	int nglyphs;
	bm_kern *kerns;   /* sorted by (first, second) */
	int nkerns;
} bm_font;

bool bm_parse(bm_font *f, const uint8_t *data, size_t len); /* allocates; bm_free */
void bm_free(bm_font *f);
const bm_glyph *bm_glyph_of(const bm_font *f, uint32_t cp); /* NULL if absent */
int bm_kerning(const bm_font *f, uint32_t a, uint32_t b);

/* Decode one UTF-8 code point; advances *s. Returns 0 at the end. Invalid
 * bytes decode as U+FFFD. */
uint32_t utf8_next(const char **s);

/* Width of one line (up to '\n' or end) in font pixels × sx, with the
 * original's per-glyph advance rule (xadv·sx + kerning). */
float bm_line_width(const bm_font *f, const char *s, float sx);
/* Widest line (FUN_0047ca00) and line count. */
float bm_text_width(const bm_font *f, const char *s, float sx, int *lines);

/* Layout callback: one quad per visible glyph, in pixels relative to the
 * text origin (x of the line start after alignment, y = top of the line). */
typedef void (*bm_emit_fn)(void *ctx, const bm_glyph *g, float x, float y, float w, float h);
/* Lays out s like FUN_0047dbd0: x = anchor per align (0 left, 1 centre,
 * 2 right), y = top of the first line; line advance = base·sy. */
void bm_layout(const bm_font *f, const char *s, float x, float y, int align, float sx, float sy, bm_emit_fn emit,
               void *ctx);

/* Port-only greedy word wrap at spaces (no line wider than maxw unless a
 * single word is). Returns the line count. */
int bm_wrap(const bm_font *f, const char *s, float scale, float maxw, char *out, size_t outsz);

#endif
