/* Story conversations (src/modes/conv.c): all 56 scripts x 8 languages
 * through the loader, the layout and the scene state machine.
 * Needs extracted/ (script_res, script/<lang>, font/<dir>/dialogue_*.fnt)
 * and docs/re/modes_story_scripts.tsv. */
#include "../src/modes/conv.h"
#include "../src/modes/conv_bm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...)                                                                                                 \
	do {                                                                                                              \
		if (!(c)) {                                                                                                   \
			if (fails++ < 30) {                                                                                       \
				printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                           \
				printf(__VA_ARGS__);                                                                                  \
				printf("\n");                                                                                         \
			}                                                                                                         \
		}                                                                                                             \
	} while (0)

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
		return NULL;
	}
	b[n] = 0;
	fclose(f);
	if (len)
		*len = (size_t)n;
	return b;
}

/* ------------------------------------------------------------ TSV */
typedef struct {
	int script, act, name[2], band[2], field3;
	char text[2][16], bg[16], f1[16], se[32];
} row;
static row rows[1200];
static int nrows;

static void load_tsv(void)
{
	char *d = slurp("docs/re/modes_story_scripts.tsv", NULL);
	if (!d) {
		printf("missing docs/re/modes_story_scripts.tsv\n");
		exit(1);
	}
	for (char *ln = strtok(d, "\n"); ln; ln = strtok(NULL, "\n")) {
		if (ln[0] == '#' || !strncmp(ln, "script", 6))
			continue;
		char *f[12];
		int n = 0;
		for (char *p = ln; n < 12; n++) {
			f[n] = p;
			char *t = strchr(p, '\t');
			if (!t) {
				n++;
				break;
			}
			*t = 0;
			p = t + 1;
		}
		if (n < 12)
			continue;
		char *cr = strchr(f[11], '\r');
		if (cr)
			*cr = 0;
		row *r = &rows[nrows++];
		r->script = atoi(f[0]);
		r->act = atoi(f[1]);
		for (int s = 0; s < 2; s++) {
			char *nm = f[2 + 3 * s];
			r->name[s] = atoi(strchr(nm, '/') + 1);
			r->band[s] = strstr(f[3 + 3 * s], "=>band") != NULL;
			const char *t = f[4 + 3 * s];
			snprintf(r->text[s], sizeof r->text[s], "%s", !strcmp(t, "(space)") ? " " : t);
		}
		snprintf(r->bg, sizeof r->bg, "%s", f[8]);
		snprintf(r->f1, sizeof r->f1, "%s", f[9]);
		snprintf(r->se, sizeof r->se, "%s", f[10]);
		r->field3 = atoi(f[11]);
	}
	free(d);
}

static const row *find_row(int script, int act)
{
	for (int i = 0; i < nrows; i++)
		if (rows[i].script == script && rows[i].act == act)
			return &rows[i];
	return NULL;
}

/* ------------------------------------------------------------ fonts */
/* font dir FUN_00597a50: en jp cn(chs, cht) ko ru; ptbr/sp -> en */
static const char *font_dir(int lang)
{
	static const char *d[8] = {"en", "jp", "cn", "cn", "en", "en", "ko", "ru"};
	return d[lang];
}

typedef struct {
	bm_font bm;
	conv_font cf;
	int size; /* 1080/960/768 */
} tfont;

/* FUN_00594570: of the sizes that exist (load order 1080, 960, 768) keep the
 * last one >= Hw */
static bool load_font(tfont *t, int lang, int Hw)
{
	static const int sizes[3] = {1080, 960, 768};
	int pick = 0;
	for (int i = 0; i < 3; i++) {
		char p[256];
		snprintf(p, sizeof p, "extracted/font/%s/dialogue_%d.fnt", font_dir(lang), sizes[i]);
		FILE *f = fopen(p, "rb");
		if (!f)
			continue;
		fclose(f);
		if (!pick || sizes[i] >= Hw)
			pick = sizes[i];
	}
	if (!pick)
		return false;
	char p[256];
	snprintf(p, sizeof p, "extracted/font/%s/dialogue_%d.fnt", font_dir(lang), pick);
	size_t n;
	char *d = slurp(p, &n);
	if (!d || !bm_parse(&t->bm, (const uint8_t *)d, n)) {
		free(d);
		return false;
	}
	free(d);
	t->cf = conv_font_from_bm(&t->bm);
	t->size = pick;
	return true;
}

/* ------------------------------------------------------------ helpers */
/* drop '\n', ' ' and U+3000 (the only things the wrapper adds or removes) */
static void squash(const char *s, char *o)
{
	while (*s) {
		if (*s == '\n' || *s == ' ') {
			s++;
			continue;
		}
		if (!strncmp(s, "\xe3\x80\x80", 3)) {
			s += 3;
			continue;
		}
		*o++ = *s++;
	}
	*o = 0;
}

static int count_lines(const char *s)
{
	int n = 1;
	for (; *s; s++)
		n += *s == '\n';
	return n;
}

typedef struct {
	int lines_max, shrunk, texts;
	float min_scale;
} wrapstats;

static void wrap_check(const conv_font *f, const char *text, int bw, int bh, float scale, wrapstats *st)
{
	float sx = scale, sy = scale;
	char *o = conv_wrap(f, text, bw, bh, &sx, &sy);
	CHECK(o != NULL, "wrap oom");
	if (!o)
		return;
	static char a[8192], b[8192];
	squash(text, a);
	squash(o, b);
	CHECK(!strcmp(a, b), "wrap changed the text:\n  %s\n  %s", text, o);
	CHECK(sx == sy, "sx != sy");
	int l = count_lines(o);
	if (l > st->lines_max)
		st->lines_max = l;
	if (sx < scale) {
		st->shrunk++;
		if (sx < st->min_scale)
			st->min_scale = sx;
	}
	/* every line fits the box (unless a single code point is wider) */
	for (const char *p = o; *p;) {
		const char *e = strchr(p, '\n');
		int n = e ? (int)(e - p) : (int)strlen(p);
		float w = conv_text_width(f, p, n, sx);
		CHECK((int)(w + 0.5f) <= bw + 1 || n <= 4, "line too wide (%.1f > %d): %.*s", w, bw, n, p);
		p += n + (e ? 1 : 0);
	}
	st->texts++;
	free(o);
}

/* button state sequence for "pressed on frame f" */
static int btn(bool down, bool was)
{
	return down ? (was ? 2 : 3) : (was ? 1 : 0);
}

/* run one conversation; returns frames until EXIT. mode 0: hold Special;
 * mode 1: press A when everything is typed (and once mid-typing per act). */
typedef struct {
	int frames, se, bgm, fadeout, acts_started, act_frames_bad;
	char se_names[4][32];
	int se_act[4];
} runres;

static runres run_scene(const conv_script *s, int id, const conv_layout *L, int mode)
{
	runres r;
	memset(&r, 0, sizeof r);
	conv_scene c;
	unsigned ev = conv_init(&c, s, id, L);
	CHECK(ev == CONV_EV_FADE_IN, "init event");
	int busy = 20, last_act_frame = -1, last_act = 0;
	bool was = false;
	for (int f = 0; f < 100000; f++) {
		bool down = false;
		if (mode == 1) {
			bool typed = true;
			for (int i = 0; i < 2; i++)
				typed &= c.n[i] == c.len[i];
			down = !was && (typed || (c.f0 == 3 && c.state == CONV_ST_TALK));
		} else {
			down = true;
		}
		conv_input in = {0, 0, 0, 0};
		if (mode == 1)
			in.attack = in.ok = btn(down, was);
		else
			in.special = btn(down, was);
		was = down;
		int before = c.act;
		ev = conv_frame(&c, &in, busy > 0);
		if (busy > 0)
			busy--;
		if (ev & CONV_EV_SE) {
			if (r.se < 4) {
				snprintf(r.se_names[r.se], 32, "%s", c.se);
				r.se_act[r.se] = c.act - 1;
			}
			r.se++;
		}
		r.bgm += !!(ev & CONV_EV_BGM_FADE);
		if (ev & CONV_EV_FADE_OUT) {
			r.fadeout++;
			busy = 20;
		}
		if (c.act != before) {
			if (mode == 0 && before >= 1) {
				int d = f - last_act_frame;
				if (d != 10 && d != 5 && !(before == 1 && d > 10))
					r.act_frames_bad++;
			}
			last_act_frame = f;
			last_act = c.act;
		}
		if (ev & CONV_EV_EXIT) {
			r.frames = f + 1;
			break;
		}
		/* view sanity */
		for (int i = 0; i < 2; i++) {
			const conv_slot_view *v = &c.view.slot[i];
			CHECK(v->text_len >= 0 && v->text_len <= (int)strlen(v->text), "text_len");
			CHECK(v->nport >= 1 && v->nport <= 2, "nport");
		}
	}
	r.acts_started = last_act;
	CHECK(r.frames > 0, "script %d never exited", id);
	conv_free(&c);
	return r;
}

/* ------------------------------------------------------------ tests */
static void test_ids(void)
{
	char b[64];
	CHECK(conv_script_id(0) == 10 && conv_script_id(3) == 13 && conv_script_id(55) == 143, "script ids");
	CHECK(conv_story_file(10, b, sizeof b) && !strcmp(b, "story_sora_01.txt"), "%s", b);
	CHECK(conv_story_file(73, b, sizeof b) && !strcmp(b, "story_starbreaker_04.txt"), "%s", b);
	CHECK(conv_story_file(142, b, sizeof b) && !strcmp(b, "story_hime_03.txt"), "%s", b);
	CHECK(!conv_story_file(150, b, sizeof b) && !conv_story_file(14, b, sizeof b), "bad ids");
	conv_res_name(10, b, sizeof b);
	CHECK(!strcmp(b, "TEXT_CONVERSATION010"), "%s", b);
	CHECK(conv_name_id("ソラ") == 3 && conv_name_id("黒幕") == 18 && conv_name_id("なし") == 0 && conv_name_id("") == 0,
	      "name ids");
	CHECK(!strcmp(conv_portrait_name(9), "star") && !strcmp(conv_portrait_name(0), "none") &&
	          !strcmp(conv_portrait_name(18), "sumikab"),
	      "portraits");
}

static void test_parser_edges(void)
{
	/* comments, blank lines, missing '>', [text] and ＄ forms, [EOF] */
	const char *src = "/ comment\r\n\r\n[ACT]\r\n< カエ >< 帯あり \r\n＄ \r\n< ナート >< \r\n[text 0001]\r\n"
	                  "< 黒地 >< なし >< chain.wav >< 7 >\r\n  \t \r\n[ACT]\r\nnot a name line\r\n　\r\n"
	                  "< ソラ >< 帯なし >\r\n[text 0002]\r\n< なし >< なし >< なし >< -1 >\r\n[EOF]\r\n[ACT]\r\n";
	conv_script s;
	CHECK(conv_script_parse(&s, src, strlen(src)), "parse");
	CHECK(s.count == 2, "count %d", s.count);
	if (s.count == 2) {
		CHECK(s.act[0].name[0] == 14 && s.act[0].band[0] == 1 && !strcmp(s.act[0].text[0], "＄"), "act0 left");
		CHECK(s.act[0].name[1] == 8 && s.act[0].band[1] == 1 && !strcmp(s.act[0].text[1], "0001"), "act0 right");
		CHECK(!strcmp(s.act[0].bg, "黒地") && !strcmp(s.act[0].se, "chain.wav") && s.act[0].field3 == 7, "act0 f");
		CHECK(s.act[1].name[0] == 0 && s.act[1].band[0] == 0 && !strcmp(s.act[1].text[0], " "), "act1 left");
		CHECK(s.act[1].name[1] == 3 && s.act[1].band[1] == 0, "act1 right");
	}
	const char *txt = "\xef\xbb\xbf\r\n<0001> \"A\\nB\"\t\r\n/<0002> x\r\n<0002>  second \r\n<0001> dup\r\n[EOF]\r\n<0003> z\r\n";
	conv_text *t = conv_text_parse(txt, strlen(txt));
	CHECK(t && conv_text_count(t) == 2, "text count");
	CHECK(t && !strcmp(conv_text_get(t, "0001"), "\"A\nB\""), "escape: %s", t ? conv_text_get(t, "0001") : "");
	CHECK(t && !strcmp(conv_text_get(t, "0002"), "second"), "trim");
	int miss = -1;
	CHECK(conv_script_resolve(&s, t, &miss) == 2 && miss == 0, "resolve");
	CHECK(s.count == 2 && !strcmp(s.act[0].text[1], "\"A\nB\""), "resolved text");
	conv_text_free(t);
	conv_script_free(&s);
}

/* fixed-advance font for exact wrap expectations */
static bool mono_glyph(void *ctx, uint32_t cp, conv_glyph *g)
{
	(void)ctx;
	if (cp == CONV_CP_INVALID)
		return false;
	g->xadv = cp >= 0x3000 ? 20 : 10;
	g->yoff = 2;
	g->h = 16;
	return true;
}
static int no_kern(void *ctx, uint32_t a, uint32_t b)
{
	(void)ctx;
	(void)a;
	(void)b;
	return 0;
}

static void test_wrap_rules(void)
{
	conv_font m = {NULL, mono_glyph, no_kern, 16, 0, false, 0};
	float sx = 1, sy = 1;
	char *o = conv_wrap(&m, "aaa bbb ccc", 75, 999, &sx, &sy);
	CHECK(o && !strcmp(o, "aaa bbb\nccc"), "word wrap: %s", o);
	free(o);
	/* hanging indent after an opening quote: continuation lines start at 10 */
	sx = sy = 1;
	o = conv_wrap(&m, "\"aa bb cc\"", 65, 999, &sx, &sy);
	CHECK(o && !strcmp(o, "\"aa bb\ncc\""), "indent: %s", o);
	free(o);
	/* a run without spaces (CJK) breaks when the run alone reaches the width */
	sx = sy = 1;
	o = conv_wrap(&m, "あいうえおかき", 60, 999, &sx, &sy);
	CHECK(o && !strcmp(o, "あいう\nえおか\nき"), "cjk: %s", o);
	free(o);
	/* explicit \n kept, a blank line collapses (literal) */
	sx = sy = 1;
	o = conv_wrap(&m, "ab\n\ncd", 200, 999, &sx, &sy);
	CHECK(o && !strcmp(o, "ab\ncd"), "newline: %s", o);
	free(o);
	/* shrink to fit: 3 lines of 16 do not fit 40 px -> scale drops by 0.1 */
	sx = sy = 1;
	o = conv_wrap(&m, "a\nb\nc", 200, 40, &sx, &sy);
	CHECK(o && sx < 1.0f && fabsf(sx - sy) < 1e-6f, "shrink %f", sx);
	free(o);
	/* width: widest line, outline adds (int)(outline*sx) per glyph */
	conv_font mo = m;
	mo.has_outline = true;
	mo.outline = 2;
	CHECK(conv_text_width(&m, "ab\nabc", 6, 1.0f) == 30.0f, "width");
	CHECK(conv_text_width(&mo, "ab", 2, 0.5f) == 12.0f, "outline width");
}

int main(void)
{
	load_tsv();
	CHECK(nrows == 1146, "tsv rows %d", nrows);
	test_ids();
	test_parser_edges();
	test_wrap_rules();

	/* parse every script; compare with the RE table */
	static conv_script scripts[CONV_SCRIPT_COUNT];
	int total_acts = 0, se_total = 0;
	size_t res_bytes_max = 0;
	for (int k = 0; k < CONV_SCRIPT_COUNT; k++) {
		int id = conv_script_id(k);
		char name[64], path[256];
		conv_res_name(id, name, sizeof name);
		snprintf(path, sizeof path, "extracted/script_res/%s.txt", name);
		size_t n;
		char *d = slurp(path, &n);
		CHECK(d != NULL, "missing %s", path);
		if (!d)
			continue;
		if (n > res_bytes_max)
			res_bytes_max = n;
		CHECK(conv_script_parse(&scripts[k], d, n), "parse %s", name);
		free(d);
		const conv_script *s = &scripts[k];
		total_acts += s->count;
		for (int a = 0; a < s->count; a++) {
			const row *r = find_row(id, a);
			CHECK(r != NULL, "%d act %d not in tsv", id, a);
			if (!r)
				continue;
			const conv_act *ac = &s->act[a];
			for (int i = 0; i < 2; i++) {
				CHECK(ac->name[i] == r->name[i], "%d:%d name[%d] %d != %d", id, a, i, ac->name[i], r->name[i]);
				CHECK(ac->band[i] == r->band[i], "%d:%d band[%d]", id, a, i);
				CHECK(!strcmp(ac->text[i], r->text[i]), "%d:%d text[%d] '%s' != '%s'", id, a, i, ac->text[i],
				      r->text[i]);
			}
			CHECK(!strcmp(ac->bg, r->bg) && !strcmp(ac->bgm, r->f1) && !strcmp(ac->se, r->se) &&
			          ac->field3 == r->field3,
			      "%d:%d fields", id, a);
			se_total += strcmp(ac->se, "なし") != 0;
		}
		int ids[CONV_NAME_COUNT];
		int np = conv_script_portraits(s, ids, CONV_NAME_COUNT);
		CHECK(np >= 2 && np <= 4, "%d portraits %d", id, np);
	}
	CHECK(total_acts == 1146, "acts %d", total_acts);
	CHECK(se_total == 8, "se %d", se_total);

	/* every language: resolve every ref, wrap every line */
	size_t text_bytes_max = 0, conv_heap_max = 0;
	for (int lang = 0; lang < CONV_LANG_COUNT; lang++) {
		tfont pc, ds;
		bool have_pc = load_font(&pc, lang, 600), have_ds = load_font(&ds, lang, 240);
		CHECK(have_pc && have_ds, "font for %s", conv_lang_dir(lang));
		/* PC 800x600: scale = 600 / size. 3DS bottom-screen proposal: the
		 * dialogue font at 16 px line height, panels 304 / 272 x 96. */
		float pc_scale = have_pc ? 600.0f / (float)pc.size : 1, ds_scale = have_ds ? 16.0f / (float)ds.bm.line_height : 1;
		wrapstats st_pc = {0, 0, 0, 1}, st_ds = {0, 0, 0, 1};
		for (int k = 0; k < CONV_SCRIPT_COUNT; k++) {
			int id = conv_script_id(k);
			char name[64], path[256];
			conv_story_file(id, name, sizeof name);
			snprintf(path, sizeof path, "extracted/script/%s/%s", conv_lang_dir(lang), name);
			size_t n;
			char *d = slurp(path, &n);
			CHECK(d != NULL, "missing %s", path);
			if (!d)
				continue;
			if (n > text_bytes_max)
				text_bytes_max = n;
			conv_text *t = conv_text_parse(d, n);
			free(d);
			/* fresh copy of the script */
			char rp[256], rn[64];
			conv_res_name(id, rn, sizeof rn);
			snprintf(rp, sizeof rp, "extracted/script_res/%s.txt", rn);
			d = slurp(rp, &n);
			conv_script s;
			conv_script_parse(&s, d, n);
			free(d);
			int refs = 0;
			for (int a = 0; a < s.count; a++)
				for (int i = 0; i < 2; i++)
					refs += s.act[a].text[i][0] == '0';
			int miss = -1, subs = conv_script_resolve(&s, t, &miss);
			CHECK(subs == refs && miss == 0, "%s %s: %d/%d refs, %d unresolved", conv_lang_dir(lang), name, subs, refs,
			      miss);
			CHECK(conv_text_count(t) == refs, "%s %s: %d keys, %d refs", conv_lang_dir(lang), name, conv_text_count(t),
			      refs);
			size_t heap = sizeof(conv_act) * (size_t)s.count;
			for (int a = 0; a < s.count; a++)
				for (int i = 0; i < 2; i++) {
					const char *x = s.act[a].text[i];
					heap += strlen(x) + 1;
					CHECK(strcmp(x, "＄") == 0 || strcmp(x, " ") == 0 || strlen(x) > 0, "empty text");
					if (!strcmp(x, "＄"))
						continue;
					if (have_pc)
						wrap_check(&pc.cf, x, 540, 88, pc_scale, &st_pc);
					if (have_ds)
						wrap_check(&ds.cf, x, i ? 304 : 272, 96, ds_scale, &st_ds);
				}
			if (heap > conv_heap_max)
				conv_heap_max = heap;
			conv_text_free(t);
			/* scene run (3DS layout) for en + jp, both input modes */
			if (have_ds && (lang == 0 || lang == 1)) {
				conv_layout L;
				conv_layout_original(&L, &ds.cf, ds_scale, 400, 240);
				L.box_w[0] = 272;
				L.box_w[1] = 304;
				L.box_h[0] = L.box_h[1] = 96;
				for (int mode = 0; mode < 2; mode++) {
					runres r = run_scene(&s, id, &L, mode);
					CHECK(r.acts_started == s.count, "%d: %d/%d acts", id, r.acts_started, s.count);
					CHECK(r.bgm == 1 && r.fadeout == 1, "%d: bgm %d fadeout %d", id, r.bgm, r.fadeout);
					CHECK(r.act_frames_bad == 0, "%d: fast-forward cadence", id);
					int se_exp = 0;
					for (int a = 0; a < s.count; a++)
						se_exp += strcmp(s.act[a].se, "なし") != 0;
					CHECK(r.se == se_exp, "%d: se %d/%d", id, r.se, se_exp);
					for (int q = 0; q < r.se && q < 4; q++)
						CHECK(!strcmp(r.se_names[q], s.act[r.se_act[q]].se), "%d: se name", id);
				}
			}
			conv_script_free(&s);
		}
		printf("  %-4s font %s/%d: PC 540x88 @%.4f: %d texts, max %d lines, %d shrunk (min %.2f) | "
		       "3DS 272-304x96 @16px: max %d lines, %d shrunk (min %.2f)\n",
		       conv_lang_dir(lang), font_dir(lang), have_pc ? pc.size : 0, pc_scale, st_pc.texts, st_pc.lines_max,
		       st_pc.shrunk, st_pc.min_scale, st_ds.lines_max, st_ds.shrunk, st_ds.min_scale);
		if (have_pc)
			bm_free(&pc.bm);
		if (have_ds)
			bm_free(&ds.bm);
	}
	printf("  largest script resource %zu B, largest story text %zu B, resolved script heap <= %zu B\n", res_bytes_max,
	       text_bytes_max, conv_heap_max);

	/* typewriter / confirm rules and the view at 400x240 on script 010 (en) */
	{
		conv_script s;
		size_t n;
		char *d = slurp("extracted/script_res/TEXT_CONVERSATION010.txt", &n);
		conv_script_parse(&s, d, n);
		free(d);
		d = slurp("extracted/script/en/story_sora_01.txt", &n);
		conv_text *t = conv_text_parse(d, n);
		free(d);
		conv_script_resolve(&s, t, NULL);
		conv_text_free(t);
		conv_layout L;
		conv_layout_original(&L, NULL, 1.0f, 400, 240); /* no font: text unwrapped */
		conv_scene c;
		conv_init(&c, &s, 10, &L);
		conv_input none = {0, 0, 0, 0}, a = {3, 0, 3, 0}, atk_blocked = {0, 2, 3, 0};
		unsigned ev = conv_frame(&c, &none, true); /* frame 0: act 0 set up during the fade */
		CHECK(c.act == 1 && (ev & CONV_EV_BGM_FADE) && c.state == CONV_ST_FADEIN, "first frame");
		CHECK(c.n[1] == 0 && c.view.nfill == 1 && c.view.fill_argb[0] == 0xff202020, "act0 black fill");
		CHECK(c.view.bar_top == 32 && c.view.bar_bottom == 208, "letterbox %d %d", c.view.bar_top, c.view.bar_bottom);
		/* portraits at s = 0.4 (modes.md §C.5) */
		const conv_portrait_draw *p0 = &c.view.slot[0].port[c.view.slot[0].nport - 1];
		const conv_portrait_draw *p1 = &c.view.slot[1].port[c.view.slot[1].nport - 1];
		CHECK(p0->l == 196 && p0->r == -36 && p0->t == 94 && p0->b == 196, "slot0 rect %d %d %d %d", p0->l, p0->t,
		      p0->r, p0->b);
		CHECK(p1->l == 204 && p1->r == 436 && p1->t == -18 && p1->b == 84, "slot1 rect %d %d %d %d", p1->l, p1->t,
		      p1->r, p1->b);
		CHECK(c.view.slot[0].text_x == 140 && c.view.slot[0].text_y == 164 && c.view.slot[1].text_x == 60 &&
		          c.view.slot[1].text_y == 52,
		      "text anchors");
		CHECK((p0->argb >> 24) == 0, "act 0 portrait starts transparent");
		for (int f = 0; f < 19; f++)
			conv_frame(&c, &none, true);
		conv_frame(&c, &none, false); /* transition over -> talk, nothing typed yet */
		CHECK(c.state == CONV_ST_TALK && c.n[1] == 0, "talk");
		conv_frame(&c, &none, false);
		CHECK(c.n[1] == 1 && c.view.slot[1].text_len == 1, "typed 1");
		conv_frame(&c, &atk_blocked, false); /* attack while cancel held: no confirm */
		CHECK(c.n[1] == 2 && c.act == 1, "attack blocked by cancel");
		conv_frame(&c, &a, false); /* completes */
		CHECK(c.n[1] == c.len[1] && c.act == 1, "complete");
		conv_frame(&c, &a, false); /* advances to act 1 (Sora speaks, left) */
		CHECK(c.act == 2 && c.n[0] == 0 && c.speak[0] && c.fade == 0.05f, "advance");
		/* act 1: bg なし after 黒地 -> previous fill fades out */
		CHECK(c.view.nfill == 1 && (c.view.fill_argb[0] >> 24) == 255 - (int)(0.05f * 255.0f),
		      "bg cross-fade %08x", c.view.fill_argb[0]);
		CHECK(c.view.slot[0].band && c.view.slot[0].tiles == 4 && c.view.slot[0].tile_w == 102 &&
		          c.view.slot[0].y0 == 156.0f && c.view.slot[0].y1 == 196.0f,
		      "band slot0 %d %d %f %f", c.view.slot[0].tiles, c.view.slot[0].tile_w, c.view.slot[0].y0,
		      c.view.slot[0].y1);
		/* name changed 0 -> 3 on slot 0: only the new portrait, fading in */
		CHECK(c.view.slot[0].nport == 1 && c.view.slot[0].port[0].id == 3, "new portrait only");
		/* slot 1 unchanged (0 -> 0): outgoing + current */
		CHECK(c.view.slot[1].nport == 2 && (c.view.slot[1].port[1].argb >> 24) == 255, "same portrait twice");
		conv_free(&c);
		conv_script_free(&s);
	}

	/* romfs layout written by tools/aos2story (only after ./build.sh) */
	{
		FILE *f = fopen("build/romfs/story/res/TEXT_CONVERSATION143.txt", "rb");
		if (f) {
			fclose(f);
			conv_script s;
			CHECK(conv_script_load(&s, "build/romfs/story", 143, 1), "conv_script_load");
			CHECK(s.count == scripts[55].count && s.act[13].field3 == 200 && s.act[0].text[0][0] != '0',
			      "romfs script 143");
			conv_script_free(&s);
			printf("  romfs:/story layout loads (build/romfs/story)\n");
		}
	}

	for (int k = 0; k < CONV_SCRIPT_COUNT; k++)
		conv_script_free(&scripts[k]);
	if (fails) {
		printf("test_conv: %d failures\n", fails);
		return 1;
	}
	printf("test_conv: OK (56 scripts, 1146 acts, 8 languages)\n");
	return 0;
}
