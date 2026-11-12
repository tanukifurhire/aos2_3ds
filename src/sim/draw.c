#include "draw.h"

#include <stdio.h>
#include <string.h>

#include "perf.h"

aos_drawlist aos_dl;

#define MAX_TEX 4096
#define TEX_NAME 24
static char names[MAX_TEX][TEX_NAME];
static int ntex;

/* Name -> id hash index (open addressing, slot = id + 1, 0 = empty). The ids are
 * assigned in first-use order exactly as before; the index only replaces the
 * linear strcmp scan (up to ~1.5M compares per frame in heavy scenes). */
#define TEX_HASH 8192 /* power of two, > 2 * MAX_TEX */
static int16_t tex_slot[TEX_HASH];

static uint32_t name_hash(const char *s)
{
	uint32_t h = 0x811c9dc5u;
	while (*s)
		h = (h ^ (uint8_t)*s++) * 0x01000193u;
	return h;
}

/* Call-site cache in front of the hash index: draw handlers pass string literals, so the same pointer asks
 * for the same name every frame. A hit is still verified by content (a reused buffer can't return a stale
 * id); it only skips hashing the name. Ids are assigned exactly as before (only known names hit). */
#define TEXP_CACHE 512
static struct {
	const char *p;
	int id;
} texp_cache[TEXP_CACHE];

static int tex_lookup(const char *name);

int aos_tex(const char *name)
{
	uint32_t c = ((uint32_t)(uintptr_t)name * 0x9e3779b1u) >> 23;
	if (texp_cache[c].p == name && !strcmp(names[texp_cache[c].id], name)) {
		AOS_PERF_ADD(tex_calls, 1);
		return texp_cache[c].id;
	}
	int id = tex_lookup(name);
	if (id >= 0) {
		texp_cache[c].p = name;
		texp_cache[c].id = id;
	}
	return id;
}

static int tex_lookup(const char *name)
{
	AOS_PERF_ADD(tex_calls, 1);
	uint32_t k = name_hash(name) & (TEX_HASH - 1);
	for (;; k = (k + 1) & (TEX_HASH - 1)) {
		int s = tex_slot[k];
		if (!s)
			break;
		AOS_PERF_ADD(tex_cmp, 1);
		if (!strcmp(names[s - 1], name))
			return s - 1;
	}
	if (ntex >= MAX_TEX || strlen(name) >= TEX_NAME)
		return -1;
	strcpy(names[ntex], name);
	tex_slot[k] = (int16_t)(ntex + 1);
	return ntex++;
}

const char *aos_tex_name(int id) { return id >= 0 && id < ntex ? names[id] : ""; }
int aos_tex_count(void) { return ntex; }

/* (fmt, frame) -> id cache in front of snprintf + aos_tex. Entries are checked
 * by format pointer AND content, so a reused buffer can't return a stale id;
 * an interned name never changes its id, so a hit is always the same answer. */
#define TEXF_CACHE 1024
#define TEXF_FMT 32
static struct {
	const char *fmt;
	int frame, id;
	char copy[TEXF_FMT];
} texf_cache[TEXF_CACHE];

int aos_texf(const char *fmt, int frame)
{
	uint32_t k = (((uint32_t)(uintptr_t)fmt >> 2) * 0x9e3779b1u ^ (uint32_t)frame * 0x85ebca6bu) >> 22;
	if (texf_cache[k].fmt == fmt && texf_cache[k].frame == frame && !strcmp(texf_cache[k].copy, fmt))
		return texf_cache[k].id;
	char buf[TEX_NAME];
	snprintf(buf, sizeof buf, fmt, frame);
	int id = aos_tex(buf);
	if (strlen(fmt) < TEXF_FMT) {
		texf_cache[k].fmt = fmt;
		texf_cache[k].frame = frame;
		texf_cache[k].id = id;
		strcpy(texf_cache[k].copy, fmt);
	}
	return id;
}

void aos_draw_begin(void)
{
	aos_dl.n = 0;
	aos_dl.dropped = 0;
}

aos_dcmd *aos_draw(int tex, int bank, float x, float y, float z)
{
	static aos_dcmd sink;
	if (tex < 0 || aos_dl.n >= AOS_MAX_DCMD) {
		aos_dl.dropped++;
		return &sink; /* callers may still write fields; nothing is drawn */
	}
	aos_dcmd *c = &aos_dl.cmd[aos_dl.n++];
	memset(c, 0, sizeof *c);
	c->tex = (int16_t)tex;
	c->bank = (int8_t)bank;
	c->blend = 1;
	c->x = x;
	c->y = y;
	c->z = z;
	c->sx = c->sy = 1.0f;
	c->rgba = 0xffffffffu;
	return c;
}

void aos_draw_tri(const float xy[6], const uint32_t rgba[3], int blend)
{
	aos_dcmd *c = aos_draw(aos_tex("$tri"), AOS_BANK_COMMON, (xy[0] + xy[2] + xy[4]) / 3.0f, (xy[1] + xy[3] + xy[5]) / 3.0f, 0);
	for (int i = 0; i < 6; i++)
		c->tri[i] = xy[i];
	for (int i = 0; i < 3; i++)
		c->triRgba[i] = rgba[i];
	c->blend = (uint8_t)blend;
}
