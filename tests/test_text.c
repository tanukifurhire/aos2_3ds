/* Host tests of the menu text + scene core (src/app/strings.c, bmfont.c,
 * scene_core.c): "< KEY >" parser, BMFont v3 parser/layout on the real font
 * files (original and repacked), word wrap, menu input repeat, transitions,
 * scene switching. Run from the repo root (tests/run_tests.sh). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/app/bmfont.h"
#include "../src/app/scene.h"
#include "../src/app/scene_core.h"
#include "../src/app/text.h"

static int fails;
#define CHECK(c)                                                          \
	do {                                                                  \
		if (!(c)) {                                                       \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
			fails++;                                                      \
		}                                                                 \
	} while (0)

static uint8_t *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	*n = (size_t)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(*n);
	if (fread(b, 1, *n, f) != *n) {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

static void test_kv(void)
{
	const char *src = "\xef\xbb\xbf\r\n< A >\r\nline one\r\nline two\r\n\r\n< B > \r\nx\r\n\r\ny\r\n\r\n\r\n<C>\r\n< EMPTY >\r\n\r\n";
	kv_table *t = kv_parse(src, strlen(src));
	CHECK(t);
	CHECK(kv_count(t) == 4);
	CHECK(!strcmp(kv_get(t, "A"), "line one\nline two"));
	CHECK(!strcmp(kv_get(t, "B"), "x\n\ny"));
	CHECK(!strcmp(kv_get(t, "C"), ""));
	CHECK(!strcmp(kv_get(t, "EMPTY"), ""));
	CHECK(kv_get(t, "Z") == NULL);
	CHECK(kv_index(t, "B") == 1);
	CHECK(!strcmp(kv_key_at(t, 3), "EMPTY"));
	kv_free(t);
}

static void test_strings(void)
{
	CHECK(strings_init("extracted/script", LANG_EN));
	CHECK(!strcmp(tr("MENU_MAINMENU_ARCADE"), "Arcade"));
	CHECK(!strcmp(tr_id(0x8b), "Arcade"));
	CHECK(!strcmp(tr_id(0x93), "Exit"));
	CHECK(!strcmp(tr("NO_SUCH_KEY"), "NO_SUCH_KEY"));
	CHECK(!strcmp(announce_by_id(0), "Encounter random opponents in a series of battles."));
	CHECK(announce_id_of("ANNOUNCE_SMENU_DIF") == 9);  /* modes.md: ANNOUNCE 9 = SMENU_DIF */
	CHECK(announce_id_of("ANNOUNCE_SMENU_MATCH") == 10);
	CHECK(announce_id_of("ANNOUNCE_SMENU_TRAINING") == 11);
	CHECK(announce_id_of("ANNOUNCE_SMENU_MANUAL") == 12);
	CHECK(strcmp(tr_id(0x24a), "MENULABEL_MAINMENU") != 0); /* menu_label.txt loaded */
	printf("  MAINMENU label '%s'\n", tr_id(0x24a));
	/* language switch: sp has its own text, falls back to en for missing files */
	CHECK(strings_init("extracted/script", LANG_SP));
	CHECK(strcmp(tr("MENU_MAINMENU_EXIT"), "MENU_MAINMENU_EXIT") != 0);
	printf("  sp Exit = '%s'\n", tr("MENU_MAINMENU_EXIT"));
	CHECK(strings_init("extracted/script", LANG_EN));
}

static void count_emit(void *ctx, const bm_glyph *g, float x, float y, float w, float h)
{
	(void)g, (void)x, (void)y, (void)w, (void)h;
	(*(int *)ctx)++;
}

static void test_fonts(void)
{
	size_t n;
	uint8_t *b = slurp("extracted/font/en/menu_768.fnt", &n);
	CHECK(b);
	bm_font f;
	CHECK(b && bm_parse(&f, b, n));
	free(b);
	CHECK(f.line_height == 28 && f.base == 23); /* modes_menu_fonts.tsv */
	CHECK(f.pages == 1 && !strcmp(f.page_name[0], "menu_768_0.png"));
	const bm_glyph *A = bm_glyph_of(&f, 'A');
	CHECK(A && A->w > 0);
	CHECK(!bm_glyph_of(&f, 0x4e00));
	float w1 = bm_line_width(&f, "Arcade", 1.0f);
	CHECK(w1 > 50 && w1 < 200);
	int lines;
	float w2 = bm_text_width(&f, "Arcade\nTraining", 1.0f, &lines);
	CHECK(lines == 2 && w2 >= w1);
	int cnt = 0;
	bm_layout(&f, "A B\nC", 10, 0, 1, 1, 1, count_emit, &cnt);
	CHECK(cnt >= 3 && cnt <= 4); /* the space may have a blank bitmap */
	char out[256];
	int nl = bm_wrap(&f, "one two three four five six seven", 1.0f, 120, out, sizeof out);
	CHECK(nl > 1);
	for (char *l = out; l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		char line[256];
		size_t k = strcspn(l, "\n");
		memcpy(line, l, k);
		line[k] = 0;
		CHECK(bm_line_width(&f, line, 1.0f) <= 120 || !strchr(line, ' '));
		CHECK(line[0] != ' ');
	}
	bm_free(&f);

	/* repacked fonts (tools/aos2pack fonts.go): optional if the asset build hasn't run */
	static const char *const packed[] = {"system", "menu", "menubig", "menulabel", "menusubtitle", "winquote", "dialogue"};
	static const int lh[] = {16, 18, 20, 12, 14, 16, 16};
	for (int i = 0; i < 7; i++) {
		char p[96];
		snprintf(p, sizeof p, "build/romfs/font/%s.fnt", packed[i]);
		b = slurp(p, &n);
		if (!b) {
			printf("  (skip %s: run ./build.sh)\n", p);
			continue;
		}
		CHECK(bm_parse(&f, b, n));
		free(b);
		CHECK(f.line_height == lh[i]);
		for (int c = 'A'; c <= 'Z'; c++)
			CHECK(bm_glyph_of(&f, (uint32_t)c));
		CHECK(f.scale_w <= 1024 && f.scale_h <= 1024);
		for (int g = 0; g < f.nglyphs; g++)
			CHECK(f.glyphs[g].x + f.glyphs[g].w <= f.scale_w && f.glyphs[g].y + f.glyphs[g].h <= f.scale_h);
		if (i == 1) {
			float w = bm_line_width(&f, "Training", 1.0f);
			printf("  menu 'Training' = %.0f px at 18 px line (port item pitch 120)\n", (double)w);
			CHECK(w < 120); /* port carousel pitch (menu.c) */
		}
		bm_free(&f);
	}
}

static void test_utf8(void)
{
	const char *s = "a\xc3\xa9\xe3\x80\x80\xff";
	CHECK(utf8_next(&s) == 'a');
	CHECK(utf8_next(&s) == 0xe9);
	CHECK(utf8_next(&s) == 0x3000);
	CHECK(utf8_next(&s) == 0xfffd);
	CHECK(utf8_next(&s) == 0);
}

static void test_input(void)
{
	menu_input mi;
	memset(&mi, 0, sizeof mi);
	int pressed = 0;
	for (int f = 0; f < 60; f++) {
		minput_feed(&mi, MB_DOWN);
		if (mi.st[MI_DOWN] == MIN_PRESSED)
			pressed++;
		if (f == 0)
			CHECK(mi.st[MI_DOWN] == MIN_PRESSED && mi.nr[MI_DOWN] == MIN_PRESSED);
		if (f >= 1 && f < 20)
			CHECK(mi.st[MI_DOWN] == MIN_HELD);
		if (f == 20 || f == 25)
			CHECK(mi.st[MI_DOWN] == MIN_PRESSED && mi.nr[MI_DOWN] == MIN_HELD);
	}
	CHECK(pressed == 9); /* frame 0, then 20,25,...,55 */
	minput_feed(&mi, 0);
	CHECK(mi.st[MI_DOWN] == MIN_RELEASED);
	minput_feed(&mi, MB_A);
	CHECK(mi.st[MI_OK] == MIN_PRESSED);
	minput_feed(&mi, MB_A);
	CHECK(mi.st[MI_OK] == MIN_HELD); /* buttons never repeat */
	/* confirm/cancel (FUN_0061c7b0/7e0) via the global */
	memset(&g_min, 0, sizeof g_min);
	minput_feed(&g_min, MB_START);
	CHECK(min_confirm() && !min_cancel());
	minput_feed(&g_min, MB_B);
	CHECK(min_cancel() && !min_confirm());
	min_flush();
	CHECK(!min_cancel());
}

static void test_fade(void)
{
	scene_fade(1);
	int n = 0;
	while (scene_fade_busy() && n < 100) {
		scene_fade_update();
		n++;
	}
	CHECK(n == 21); /* 20 frames to 1.0, off one update later */
	scene_fade(5);
	CHECK(scene_fade_alpha() == 1.0f);
	for (int i = 0; i < 10; i++)
		scene_fade_update();
	CHECK(fabsf(scene_fade_alpha() - 0.5f) < 1e-4f);
	scene_fade(2);
	n = 0;
	while (scene_fade_busy() && n < 200) {
		scene_fade_update();
		n++;
	}
	CHECK(n >= 100 && n <= 102);
}

static int log_[32], nlog;
static void a_init(void) { log_[nlog++] = 1; }
static void a_update(void) { log_[nlog++] = 2; if (scene_frame() == 2) scene_goto(7); }
static void a_exit(void) { log_[nlog++] = 3; }
static void b_init(void) { log_[nlog++] = 4; }
static void b_update(void) { log_[nlog++] = 5; scene_goto(SCENE_ARC_SELECT); }
static void cs_update(void) { log_[nlog++] = 6; scene_goto(SCENE_QUIT); }

static void test_scenes(void)
{
	static const scene_def a = {.name = "a", .init = a_init, .update = a_update, .exit = a_exit};
	static const scene_def b = {.name = "b", .init = b_init, .update = b_update};
	static const scene_def cs = {.name = "cs", .update = cs_update};
	scene_register(1, &a);
	scene_register(7, &b);
	scene_register(SCENE_COMING_SOON, &cs);
	scene_core_start(1);
	int frames = 0;
	do {
		scene_core_begin_frame();
		scene_core_update();
		frames++;
	} while (scene_core_end_frame() && frames < 20);
	/* a: init, 3 updates (goto on frame 2), exit; b: init, update -> unregistered 8 -> coming soon */
	static const int want[] = {1, 2, 2, 2, 3, 4, 5, 6};
	CHECK(nlog == 8);
	for (int i = 0; i < 8 && i < nlog; i++)
		CHECK(log_[i] == want[i]);
	CHECK(scene_requested() == SCENE_QUIT);
	CHECK(frames == 5);
}

static void test_config(void)
{
	config_path = "build/tests/test_config.ini";
	config_defaults(&g_config);
	CHECK(g_config.required_wins == 1 && g_config.time_limit == 1 && g_config.bgm_volume == 5);
	CHECK(config_timer_seconds(3) == 999);
	g_config.bgm_volume = 7;
	g_config.tournament = true;
	config_save();
	config_defaults(&g_config);
	config_load();
	CHECK(g_config.bgm_volume == 7 && g_config.tournament);
}

int main(void)
{
	test_kv();
	test_strings();
	test_fonts();
	test_utf8();
	test_input();
	test_fade();
	test_scenes();
	test_config();
	if (fails) {
		printf("test_text: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_text: ok\n");
	return 0;
}
