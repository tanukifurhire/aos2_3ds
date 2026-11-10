/* conv_font on top of the app's BMFont parser (src/app/bmfont.h, Part A).
 * Glyph lookup without fallbacks, as the original (a missing glyph adds
 * nothing); the renderer must draw with the same font. */
#ifndef AOS_MODES_CONV_BM_H
#define AOS_MODES_CONV_BM_H

#include "../app/bmfont.h"
#include "conv.h"

static inline bool conv_bm_glyph(void *ctx, uint32_t cp, conv_glyph *g)
{
	const bm_glyph *b = bm_glyph_of((const bm_font *)ctx, cp);
	if (!b)
		return false;
	g->yoff = b->yoff;
	g->xadv = b->xadv;
	g->h = b->h;
	return true;
}

static inline int conv_bm_kerning(void *ctx, uint32_t a, uint32_t b) { return bm_kerning((const bm_font *)ctx, a, b); }

/* Original fonts: has_outline = info.outline > 0. Part A's repacked fonts
 * fold the outline into xadvance and store outline 0. */
static inline conv_font conv_font_from_bm(const bm_font *f)
{
	conv_font c = {(void *)f, conv_bm_glyph, conv_bm_kerning, f->base, 0.0f, f->outline > 0, f->outline};
	return c;
}

#endif
