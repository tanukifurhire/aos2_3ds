#include "bmfont.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int cmp_glyph(const void *a, const void *b)
{
	uint32_t x = ((const bm_glyph *)a)->id, y = ((const bm_glyph *)b)->id;
	return x < y ? -1 : x > y;
}
static int cmp_kern(const void *a, const void *b)
{
	const bm_kern *x = a, *y = b;
	if (x->first != y->first)
		return x->first < y->first ? -1 : 1;
	return x->second < y->second ? -1 : x->second > y->second;
}

bool bm_parse(bm_font *f, const uint8_t *d, size_t len)
{
	memset(f, 0, sizeof *f);
	if (len < 4 || memcmp(d, "BMF", 3) != 0 || d[3] != 3)
		return false;
	size_t p = 4;
	bool common = false;
	while (p + 5 <= len) {
		int type = d[p];
		size_t sz = rd32(d + p + 1);
		p += 5;
		if (sz > len - p)
			goto fail;
		const uint8_t *b = d + p;
		switch (type) {
		case 1:
			if (sz >= 14)
				f->outline = b[13];
			break;
		case 2:
			if (sz < 10)
				goto fail;
			f->line_height = rd16(b);
			f->base = rd16(b + 2);
			f->scale_w = rd16(b + 4);
			f->scale_h = rd16(b + 6);
			f->pages = rd16(b + 8);
			common = true;
			break;
		case 3: {
			size_t i = 0;
			int n = 0;
			while (i < sz && n < 4) {
				size_t l = strnlen((const char *)b + i, sz - i);
				if (l >= sizeof f->page_name[0])
					l = sizeof f->page_name[0] - 1;
				memcpy(f->page_name[n], b + i, l);
				f->page_name[n][l] = 0;
				n++;
				i += strnlen((const char *)b + i, sz - i) + 1;
			}
			break;
		}
		case 4: {
			int n = (int)(sz / 20);
			bm_glyph *g = realloc(f->glyphs, sizeof *g * (size_t)(f->nglyphs + n));
			if (!g)
				goto fail;
			f->glyphs = g;
			for (int i = 0; i < n; i++) {
				const uint8_t *c = b + i * 20;
				bm_glyph *o = &g[f->nglyphs + i];
				o->id = rd32(c);
				o->x = rd16(c + 4);
				o->y = rd16(c + 6);
				o->w = rd16(c + 8);
				o->h = rd16(c + 10);
				o->xoff = (int16_t)rd16(c + 12);
				o->yoff = (int16_t)rd16(c + 14);
				o->xadv = (int16_t)rd16(c + 16);
				o->page = c[18];
			}
			f->nglyphs += n;
			break;
		}
		case 5: {
			int n = (int)(sz / 10);
			bm_kern *k = realloc(f->kerns, sizeof *k * (size_t)(f->nkerns + n));
			if (!k)
				goto fail;
			f->kerns = k;
			for (int i = 0; i < n; i++) {
				const uint8_t *c = b + i * 10;
				k[f->nkerns + i] = (bm_kern){rd32(c), rd32(c + 4), (int16_t)rd16(c + 8)};
			}
			f->nkerns += n;
			break;
		}
		default:
			break;
		}
		p += sz;
	}
	if (!common || f->nglyphs == 0)
		goto fail;
	qsort(f->glyphs, (size_t)f->nglyphs, sizeof *f->glyphs, cmp_glyph);
	if (f->nkerns)
		qsort(f->kerns, (size_t)f->nkerns, sizeof *f->kerns, cmp_kern);
	return true;
fail:
	bm_free(f);
	return false;
}

void bm_free(bm_font *f)
{
	free(f->glyphs);
	free(f->kerns);
	memset(f, 0, sizeof *f);
}

const bm_glyph *bm_glyph_of(const bm_font *f, uint32_t cp)
{
	bm_glyph key;
	key.id = cp;
	return bsearch(&key, f->glyphs, (size_t)f->nglyphs, sizeof key, cmp_glyph);
}

int bm_kerning(const bm_font *f, uint32_t a, uint32_t b)
{
	if (!f->nkerns)
		return 0;
	bm_kern key = {a, b, 0};
	const bm_kern *k = bsearch(&key, f->kerns, (size_t)f->nkerns, sizeof key, cmp_kern);
	return k ? k->amount : 0;
}

uint32_t utf8_next(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	uint32_t c = s[0];
	if (!c)
		return 0;
	int n = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
	if (n < 0) {
		*ps += 1;
		return 0xfffd;
	}
	if (n)
		c &= 0x3fu >> n;
	for (int i = 1; i <= n; i++) {
		if ((s[i] & 0xc0) != 0x80) {
			*ps += i;
			return 0xfffd;
		}
		c = c << 6 | (s[i] & 0x3fu);
	}
	*ps += n + 1;
	return c;
}

/* Glyph for cp with fallbacks for characters the packed set lacks. */
static const bm_glyph *glyph_fb(const bm_font *f, uint32_t cp)
{
	const bm_glyph *g = bm_glyph_of(f, cp);
	if (g)
		return g;
	if (cp == 0x3000 || cp == 0xa0)
		return bm_glyph_of(f, ' ');
	if (cp == 0x2019 || cp == 0x2018)
		return bm_glyph_of(f, '\'');
	if (cp == 0x201c || cp == 0x201d)
		return bm_glyph_of(f, '"');
	if (cp == 0x2026)
		return bm_glyph_of(f, '.');
	if (cp >= 'a' && cp <= 'z') /* menu fonts are capitals-only for some letters */
		return bm_glyph_of(f, cp - 32);
	return bm_glyph_of(f, '?');
}

float bm_line_width(const bm_font *f, const char *s, float sx)
{
	float w = 0;
	uint32_t prev = 0;
	while (*s && *s != '\n') {
		uint32_t cp = utf8_next(&s);
		if (cp == '\r')
			continue;
		const bm_glyph *g = glyph_fb(f, cp);
		if (!g)
			continue;
		if (prev)
			w += (float)bm_kerning(f, prev, cp) * sx;
		w += (float)g->xadv * sx;
		prev = cp;
	}
	return w;
}

float bm_text_width(const bm_font *f, const char *s, float sx, int *lines)
{
	float w = 0;
	int n = 1;
	for (;;) {
		float lw = bm_line_width(f, s, sx);
		if (lw > w)
			w = lw;
		const char *nl = strchr(s, '\n');
		if (!nl)
			break;
		s = nl + 1;
		n++;
	}
	if (lines)
		*lines = n;
	return w;
}

void bm_layout(const bm_font *f, const char *s, float x, float y, int align, float sx, float sy, bm_emit_fn emit,
               void *ctx)
{
	float adv = floorf((float)f->base * sy + 0.5f);
	for (;;) {
		float lw = bm_line_width(f, s, sx);
		float px = align == 1 ? floorf(x + 0.5f) + (float)(int)(lw * -0.5f) : align == 2 ? floorf(x + 0.5f) - (float)(int)lw : x;
		uint32_t prev = 0;
		while (*s && *s != '\n') {
			uint32_t cp = utf8_next(&s);
			if (cp == '\r')
				continue;
			const bm_glyph *g = glyph_fb(f, cp);
			if (!g)
				continue;
			if (prev)
				px += (float)bm_kerning(f, prev, cp) * sx;
			if (g->w && g->h)
				emit(ctx, g, px + (float)g->xoff * sx, y + (float)g->yoff * sy, (float)g->w * sx, (float)g->h * sy);
			px += (float)g->xadv * sx;
			prev = cp;
		}
		if (*s != '\n')
			break;
		s++;
		y += adv;
	}
}

int bm_wrap(const bm_font *fn, const char *s, float scale, float maxw, char *out, size_t outsz)
{
	if (!outsz)
		return 0;
	size_t o = 0, line_start = 0;
	int lines = 1;
	out[0] = 0;
	while (*s && o + 1 < outsz) {
		if (*s == '\n') {
			out[o++] = '\n';
			s++;
			lines++;
			line_start = o;
			continue;
		}
		const char *w = s; /* word = leading spaces + non-space run */
		while (*w == ' ')
			w++;
		const char *e = w;
		while (*e && *e != ' ' && *e != '\n')
			e++;
		if (o > line_start) {
			/* width of the line if the word is appended */
			size_t add = (size_t)(e - s);
			if (o + add + 1 < outsz) {
				memcpy(out + o, s, add);
				out[o + add] = 0;
				float lw = bm_line_width(fn, out + line_start, scale);
				out[o] = 0;
				if (lw > maxw) {
					out[o++] = '\n';
					lines++;
					line_start = o;
					s = w;
				}
			}
		}
		size_t add = (size_t)(e - s);
		if (o + add + 1 >= outsz)
			add = outsz - o - 1;
		memcpy(out + o, s, add);
		o += add;
		s += add;
		out[o] = 0;
		if (!add)
			break;
	}
	out[o] = 0;
	return lines;
}
