/* Progress save: game.sys + game.rkg images in one versioned file (save.h). */
#include "save.h"

#include "../input/aos_input.h" /* AOS_ACT_* / AOS_BTN_* constants only */

_Static_assert(AOS_SAVE_BINDS == AOS_ACT_COUNT, "binds block");

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

void aos_save_rank_defaults(aos_save *s) /* FUN_00792a30, game.rkg missing */
{
	for (int d = 0; d < 3; d++)
		for (int i = 0; i < AOS_RANK_ENTRIES; i++) {
			aos_rank_entry *e = &s->rank[d][i];
			e->score = 10000 - 1000 * i;
			for (int k = 0; k < AOS_RANK_NAME; k++)
				e->name[k] = i;
			e->character = (i % 2 == 0) ? 10 : 3; /* local_110: Suguri on even rows, Sora on odd */
		}
}

void aos_save_defaults(aos_save *s) /* FUN_007c4450 + FUN_007c4480 (game.sys missing) */
{
	memset(s, 0, sizeof *s);
	s->version = 0x5a2;
	for (int c = 3; c <= 8; c++) /* DAT_0087b72f..b734 */
		s->charOpen[c] = 1;
	for (int c = 10; c <= 15; c++) /* DAT_0087b736..b73b */
		s->charOpen[c] = 1;
	static const uint8_t stages[] = {5, 6, 7, 8, 9, 11, 15};      /* DAT_0087b744..b748, b74a, b74e */
	static const uint8_t bgms[] = {11, 12, 13, 15, 16, 17, 18};    /* DAT_0087b75e..b760, b762..b765 */
	for (size_t i = 0; i < sizeof stages; i++)
		s->stageOpen[stages[i]] = 1;
	for (size_t i = 0; i < sizeof bgms; i++)
		s->bgmOpen[bgms[i]] = 1;
	aos_save_rank_defaults(s);
	aos_save_controls_defaults(s);
	s->dirty = 1; /* FUN_007c4480 writes the fresh file (FUN_007c47f0) */
}

void aos_save_controls_defaults(aos_save *s)
{
	/* = aos_binds_default (header constants only: save.c links without aos_input.c) */
	static const uint8_t def[AOS_SAVE_BINDS] = {
		[AOS_ACT_DASH] = AOS_BTN_L,     [AOS_ACT_SHIELD] = AOS_BTN_R,    [AOS_ACT_WEAPON_A] = AOS_BTN_A,
		[AOS_ACT_WEAPON_B] = AOS_BTN_B, [AOS_ACT_SPECIAL_A] = AOS_BTN_X, [AOS_ACT_SPECIAL_B] = AOS_BTN_Y,
	};
	memcpy(s->binds, def, sizeof s->binds);
	memset(s->hyperLayout, 0xff, sizeof s->hyperLayout);
}

void aos_save_achieve(aos_save *s, int id)
{
	if (id >= 0 && id < 32 && !(s->achievements & (1u << id))) {
		s->achievements |= 1u << id;
		s->dirty = 1;
	}
}

/* ---- encoding ---- */

static void put16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		b[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static uint32_t get32(const uint8_t *b)
{
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

#define PAYLOAD_V1 (AOS_SAVE_SYS_BYTES + AOS_SAVE_RKG_BYTES + 4)
#define PAYLOAD (PAYLOAD_V1 + AOS_SAVE_CTL_BYTES)

static void ctl_put(const aos_save *s, uint8_t *b)
{
	memset(b, 0, AOS_SAVE_CTL_BYTES);
	b[0] = 1;
	b[1] = AOS_SAVE_BINDS;
	memcpy(b + 2, s->binds, AOS_SAVE_BINDS);
	memcpy(b + 2 + AOS_SAVE_BINDS, s->hyperLayout, sizeof s->hyperLayout);
}

/* a row is kept only if it is a permutation with the centre (4) on the centre, else default (0xff) */
static int layout_ok(const uint8_t *p)
{
	unsigned seen = 0;
	for (int k = 0; k < AOS_SAVE_HT_SLOTS; k++) {
		if (p[k] >= AOS_SAVE_HT_SLOTS || (seen & (1u << p[k])))
			return 0;
		seen |= 1u << p[k];
	}
	return p[AOS_SAVE_HT_SLOTS - 1] == AOS_SAVE_HT_SLOTS - 1;
}

static void ctl_get(aos_save *s, const uint8_t *b)
{
	if (b[0] != 1 || b[1] != AOS_SAVE_BINDS)
		return; /* unknown block version: defaults */
	unsigned used = 0; /* = aos_binds_valid */
	int ok = 1;
	for (int a = 0; a < AOS_SAVE_BINDS; a++) {
		if (b[2 + a] >= AOS_BTN_COUNT || (used & (1u << b[2 + a])))
			ok = 0;
		else
			used |= 1u << b[2 + a];
	}
	if (ok)
		memcpy(s->binds, b + 2, AOS_SAVE_BINDS);
	const uint8_t *l = b + 2 + AOS_SAVE_BINDS;
	for (int c = 0; c < AOS_SAVE_CHARS; c++)
		if (layout_ok(l + c * AOS_SAVE_HT_SLOTS))
			memcpy(s->hyperLayout[c], l + c * AOS_SAVE_HT_SLOTS, AOS_SAVE_HT_SLOTS);
}

static void sys_put(const aos_save *s, uint8_t *b) /* layout of DAT_0087b728 */
{
	memset(b, 0, AOS_SAVE_SYS_BYTES);
	put32(b + 0x00, (uint32_t)s->version);
	memcpy(b + 0x04, s->charOpen, AOS_SAVE_CHARS);
	memcpy(b + 0x17, s->stageOpen, AOS_SAVE_STAGES);
	memcpy(b + 0x2b, s->bgmOpen, AOS_SAVE_BGMS);
	put32(b + 0x44, (uint32_t)s->wins);
	for (int d = 0; d < 3; d++)
		put32(b + 0x48 + 4 * d, (uint32_t)s->arcadeClears[d]);
	put32(b + 0x54, (uint32_t)s->storyClears);
	memcpy(b + 0x58, s->arcadeClear, 3 * AOS_SAVE_CHARS);
	memcpy(b + 0x91, s->storyClear, AOS_SAVE_CHARS);
}

static void sys_get(aos_save *s, const uint8_t *b)
{
	s->version = (int32_t)get32(b + 0x00);
	memcpy(s->charOpen, b + 0x04, AOS_SAVE_CHARS);
	memcpy(s->stageOpen, b + 0x17, AOS_SAVE_STAGES);
	memcpy(s->bgmOpen, b + 0x2b, AOS_SAVE_BGMS);
	s->wins = (int32_t)get32(b + 0x44);
	for (int d = 0; d < 3; d++)
		s->arcadeClears[d] = (int32_t)get32(b + 0x48 + 4 * d);
	s->storyClears = (int32_t)get32(b + 0x54);
	memcpy(s->arcadeClear, b + 0x58, 3 * AOS_SAVE_CHARS);
	memcpy(s->storyClear, b + 0x91, AOS_SAVE_CHARS);
}

static void rkg_put(const aos_save *s, uint8_t *b)
{
	for (int d = 0; d < 3; d++)
		for (int i = 0; i < AOS_RANK_ENTRIES; i++) {
			const aos_rank_entry *e = &s->rank[d][i];
			uint8_t *p = b + (size_t)(d * AOS_RANK_ENTRIES + i) * 0x24;
			put32(p, (uint32_t)e->score);
			for (int k = 0; k < AOS_RANK_NAME; k++)
				put32(p + 4 + 4 * k, (uint32_t)e->name[k]);
			put32(p + 0x20, (uint32_t)e->character);
		}
}

static void rkg_get(aos_save *s, const uint8_t *b)
{
	for (int d = 0; d < 3; d++)
		for (int i = 0; i < AOS_RANK_ENTRIES; i++) {
			aos_rank_entry *e = &s->rank[d][i];
			const uint8_t *p = b + (size_t)(d * AOS_RANK_ENTRIES + i) * 0x24;
			e->score = (int32_t)get32(p);
			for (int k = 0; k < AOS_RANK_NAME; k++)
				e->name[k] = (int32_t)get32(p + 4 + 4 * k);
			e->character = (int32_t)get32(p + 0x20);
		}
}

size_t aos_save_encode(const aos_save *s, uint8_t *buf, size_t cap)
{
	if (cap < AOS_SAVE_FILE_BYTES)
		return 0;
	uint8_t *p = buf + 16;
	sys_put(s, p);
	rkg_put(s, p + AOS_SAVE_SYS_BYTES);
	put32(p + AOS_SAVE_SYS_BYTES + AOS_SAVE_RKG_BYTES, s->achievements);
	ctl_put(s, p + PAYLOAD_V1);
	memcpy(buf, "A2SV", 4);
	put16(buf + 4, AOS_SAVE_FORMAT);
	put16(buf + 6, 0x5a2);
	put32(buf + 8, PAYLOAD);
	put32(buf + 12, crc32(p, PAYLOAD));
	return AOS_SAVE_FILE_BYTES;
}

int aos_save_decode(aos_save *s, const uint8_t *buf, size_t len)
{
	aos_save_defaults(s);
	if (len < 16 || memcmp(buf, "A2SV", 4) != 0 || get16(buf + 4) < 1)
		return 0;
	uint32_t n = get32(buf + 8);
	if (n < PAYLOAD_V1 || len - 16 < n || crc32(buf + 16, n) != get32(buf + 12))
		return 0;
	const uint8_t *p = buf + 16;
	sys_get(s, p);
	rkg_get(s, p + AOS_SAVE_SYS_BYTES);
	s->achievements = get32(p + AOS_SAVE_SYS_BYTES + AOS_SAVE_RKG_BYTES);
	if (get16(buf + 4) >= 2 && n >= PAYLOAD) /* format 1 (no controls block): the defaults stay */
		ctl_get(s, p + PAYLOAD_V1);
	s->dirty = 0;
	return 1;
}

/* ---- files ---- */

int aos_save_load_file(aos_save *s, const char *path)
{
	uint8_t buf[AOS_SAVE_FILE_BYTES + 256];
	FILE *f = fopen(path, "rb");
	if (!f) { /* interrupted write: the old file was removed, the new one not yet renamed */
		char tmp[512];
		if (snprintf(tmp, sizeof tmp, "%s.tmp", path) < (int)sizeof tmp)
			f = fopen(tmp, "rb");
	}
	if (!f) {
		aos_save_defaults(s);
		return 0;
	}
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	return aos_save_decode(s, buf, n);
}

int aos_save_write_file(const aos_save *s, const char *dir, const char *path)
{
	uint8_t buf[AOS_SAVE_FILE_BYTES];
	size_t n = aos_save_encode(s, buf, sizeof buf);
	char tmp[512];
	if (!n || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
		return 0;
	if (dir) {
#ifdef _WIN32
		_mkdir(dir);
#else
		mkdir(dir, 0777); /* exists already -> EEXIST, ignored; parent sdmc:/3ds always exists */
#endif
	}
	FILE *f = fopen(tmp, "wb");
	if (!f)
		return 0;
	int ok = fwrite(buf, 1, n, f) == n;
	ok &= fclose(f) == 0;
	if (!ok) {
		remove(tmp);
		return 0;
	}
	remove(path); /* rename() does not replace an existing file on Windows nor (FSUSER_RenameFile) on 3DS;
	               * if we die between the two, load falls back to the .tmp */
	return rename(tmp, path) == 0;
}
