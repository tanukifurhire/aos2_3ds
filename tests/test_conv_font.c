/* Packed dialogue fonts vs the story texts (Part C renderer, src/app/conv_scene.c):
 * every code point of every story line of a language has a glyph in the font the
 * 3DS scene draws it with (romfs/font/dialogue[_<lang>].fnt, tools/aos2pack
 * fonts.go / fonts_dialogue.go) whenever the original font of that language
 * (font/<dir>/dialogue_1080.fnt) has one (the PC draws nothing for a missing glyph,
 * e.g. U+3000 in the Latin/ko/ru fonts, and so does the port), and the lines wrap
 * into the bottom-screen boxes (272 x 96 slot 0, 304 x 96 slot 1) at scale 1
 * without shrink-to-fit.
 * usage: test_conv_font <romfs dir> <extracted dir>; a language whose packed font is
 * missing from the romfs is skipped. */
#include "../src/app/bmfont.h"
#include "../src/modes/conv.h"
#include "../src/modes/conv_bm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
		return NULL;
	}
	b[n] = 0;
	fclose(f);
	*len = (size_t)n;
	return b;
}

static bool load_font(bm_font *f, const char *path)
{
	size_t n;
	char *raw = slurp(path, &n);
	bool ok = raw && bm_parse(f, (const uint8_t *)raw, n);
	free(raw);
	return ok;
}

/* must match font_name() in src/app/conv_scene.c (lang = DAT_00881ec0 / LANG_*);
 * src_dir = the original's font folder (FUN_00597a50) */
static const char *const font_of[CONV_LANG_COUNT] = {"dialogue", "dialogue_jp", "dialogue_chs", "dialogue_cht",
                                                     "dialogue", "dialogue", "dialogue_ko", "dialogue_ru"};
static const char *const src_dir[CONV_LANG_COUNT] = {"en", "jp", "cn", "cn", "en", "en", "ko", "ru"};

int main(int argc, char **argv)
{
	const char *romfs = argc > 1 ? argv[1] : "build/romfs", *ext = argc > 2 ? argv[2] : "extracted";
	int fails = 0, checked = 0;
	for (int lang = 0; lang < CONV_LANG_COUNT; lang++) {
		char path[512];
		bm_font bm, orig;
		snprintf(path, sizeof path, "%s/font/%s.fnt", romfs, font_of[lang]);
		if (!load_font(&bm, path)) {
			printf("  %-4s %s missing: skipped\n", conv_lang_dir(lang), path);
			continue;
		}
		snprintf(path, sizeof path, "%s/font/%s/dialogue_1080.fnt", ext, src_dir[lang]);
		if (!load_font(&orig, path)) {
			printf("FAIL %s: original font missing\n", path);
			fails++;
			bm_free(&bm);
			continue;
		}
		conv_font cf = conv_font_from_bm(&bm);
		int missing = 0, absent = 0, lines = 0, maxlines = 0, shrunk = 0;
		for (int k = 0; k < CONV_SCRIPT_COUNT; k++) {
			int id = conv_script_id(k);
			char name[64];
			size_t n;
			conv_story_file(id, name, sizeof name);
			snprintf(path, sizeof path, "%s/script/%s/%s", ext, conv_lang_dir(lang), name);
			char *d = slurp(path, &n);
			if (!d) {
				printf("FAIL missing %s\n", path);
				fails++;
				continue;
			}
			conv_text *t = conv_text_parse(d, n);
			free(d);
			for (int e = 0; t && e < conv_text_count(t); e++) {
				char key[8];
				snprintf(key, sizeof key, "%04d", e);
				const char *s = conv_text_get(t, key);
				if (!s)
					continue;
				int len = (int)strlen(s);
				for (int i = 0; i < len;) {
					int l;
					uint32_t cp = conv_utf8(s + i, len - i, &l);
					i += l;
					if (cp == '\n' || cp == CONV_CP_INVALID)
						continue;
					if (!bm_glyph_of(&orig, cp)) {
						absent++; /* not in the PC font either: drawn as nothing on both */
					} else if (!bm_glyph_of(&bm, cp)) {
						if (missing++ < 5)
							printf("FAIL %s %s <%s>: no glyph U+%04X\n", font_of[lang], name, key, (unsigned)cp);
					}
				}
				for (int slot = 0; slot < 2; slot++) { /* wrap like next_act (FUN_004e5190) */
					float sx = 1, sy = 1;
					char *w = conv_wrap(&cf, s, slot ? 304 : 272, 96, &sx, &sy);
					if (!w)
						continue;
					int nl = 1;
					for (char *p = w; *p; p++)
						nl += *p == '\n';
					if (nl > maxlines)
						maxlines = nl;
					shrunk += sx < 1.0f;
					lines++;
					free(w);
				}
			}
			conv_text_free(t);
		}
		printf("  %-4s %-13s %4d glyphs: %d wrapped texts, max %d lines, %d shrunk, %d missing glyphs"
		       " (%d chars have no glyph in the PC font either)\n",
		       conv_lang_dir(lang), font_of[lang], bm.nglyphs, lines, maxlines, shrunk, missing, absent);
		fails += missing > 0;
		if (shrunk) {
			printf("FAIL %s: %d texts do not fit 96 px at scale 1\n", conv_lang_dir(lang), shrunk);
			fails++;
		}
		checked++;
		bm_free(&bm);
		bm_free(&orig);
	}
	printf("test_conv_font: %d languages checked, %s\n", checked, fails ? "FAILED" : "ok");
	return fails ? 1 : 0;
}
