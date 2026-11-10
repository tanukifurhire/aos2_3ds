/* Story conversation scene: loader, layout, state machine (conv.h). */
#include "conv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ strings */
/* Growable byte string (the std::string of the original). */
typedef struct {
	char *p;
	int n, cap;
	bool oom;
} sb;

static void sb_reserve(sb *b, int need)
{
	if (b->oom || need + 1 <= b->cap)
		return;
	int cap = b->cap ? b->cap : 32;
	while (cap < need + 1)
		cap *= 2;
	char *p = realloc(b->p, (size_t)cap);
	if (!p) {
		b->oom = true;
		return;
	}
	b->p = p;
	b->cap = cap;
}
static void sb_add(sb *b, const char *s, int n)
{
	sb_reserve(b, b->n + n);
	if (b->oom)
		return;
	memmove(b->p + b->n, s, (size_t)n);
	b->n += n;
	b->p[b->n] = 0;
}
static void sb_set(sb *b, const char *s, int n)
{
	b->n = 0;
	sb_reserve(b, n);
	if (b->oom)
		return;
	memmove(b->p, s, (size_t)n);
	b->n = n;
	b->p[n] = 0;
}
static void sb_clear(sb *b)
{
	b->n = 0;
	if (b->p)
		b->p[0] = 0;
}
static void sb_free(sb *b)
{
	free(b->p);
	memset(b, 0, sizeof *b);
}
static char *dupn(const char *s, int n)
{
	char *p = malloc((size_t)n + 1);
	if (p) {
		memcpy(p, s, (size_t)n);
		p[n] = 0;
	}
	return p;
}

/* trim of " \t" (FUN_005859a0); returns start/len inside s */
static void trim_ws(const char *s, int n, int *a, int *len)
{
	int i = 0, j = n;
	while (i < j && (s[i] == ' ' || s[i] == '\t'))
		i++;
	while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t'))
		j--;
	*a = i;
	*len = j - i;
}

/* ------------------------------------------------------------ ids */
static const char *const story_chars[15] = {NULL,     "sora",   "alte", "tsih", "mira",  "sham", "nath",
                                            "starbreaker", "suguri", "saki", "iru",  "nanako", "kae", "kyoko",
                                            "hime"};

int conv_script_id(int index)
{
	if (index < 0 || index >= CONV_SCRIPT_COUNT)
		return -1;
	return (index / 4 + 1) * 10 + index % 4;
}

const char *conv_story_char(int id)
{
	if (id < 10 || id % 10 > 3 || id / 10 > 14)
		return NULL;
	return story_chars[id / 10];
}

bool conv_story_file(int id, char *buf, size_t n)
{
	const char *c = conv_story_char(id);
	if (!c)
		return false;
	snprintf(buf, n, "story_%s_0%d.txt", c, id % 10 + 1);
	return true;
}

void conv_res_name(int id, char *buf, size_t n) { snprintf(buf, n, "TEXT_CONVERSATION%03d", id); }

const char *conv_lang_dir(int lang)
{
	static const char *const d[CONV_LANG_COUNT] = {"en", "jp", "chs", "cht", "ptbr", "sp", "ko", "ru"};
	return lang >= 0 && lang < CONV_LANG_COUNT ? d[lang] : "en";
}

/* DAT_009b1054 (static init FUN_00456820; strings 0x81f45c..0x81f4fc) */
static const struct {
	const char *name;
	int id;
	const char *tex; /* FUN_004e43a0 */
} names[] = {
	{"ソラ", 3, "sora"},     {"アルテ", 4, "alte"},   {"ツィー", 5, "tsih"},   {"ミラ", 6, "mira"},
	{"シャム", 7, "sham"},   {"ナート", 8, "nath"},   {"スタブ", 9, "star"},   {"スグリ", 10, "suguri"},
	{"サキ", 11, "saki"},    {"イル", 12, "iru"},     {"ナナコ", 13, "nanako"}, {"カエ", 14, "kae"},
	{"キョウコ", 15, "kyoko"}, {"ヒメ", 16, "hime"},  {"スミカ", 17, "sumika"}, {"黒幕", 18, "sumikab"},
};

int conv_name_id(const char *s)
{
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (!strcmp(s, names[i].name))
			return names[i].id;
	return 0;
}

const char *conv_portrait_name(int id)
{
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (names[i].id == id)
			return names[i].tex;
	return "none";
}

/* ------------------------------------------------------------ utf-8 */
uint32_t conv_utf8(const char *str, int avail, int *len)
{
	/* FUN_00458e20 (reads past `avail` only as far as a NUL terminator) */
	const unsigned char *s = (const unsigned char *)str;
	unsigned char b = s[0];
	if (b < 0x80) {
		*len = 1;
		return b;
	}
	int want;
	uint32_t c;
	if ((b & 0xe0) == 0xc0) {
		c = b & 0x1f;
		want = c < 2 ? -1 : 2; /* overlong C0/C1 -> invalid */
	} else if ((b & 0xf0) == 0xe0) {
		want = 3;
		c = b & 0xf;
	} else if ((b & 0xf8) == 0xf0) {
		want = 4;
		c = b & 7;
	} else {
		want = -1;
		c = 0;
	}
	int i = 1;
	if (want >= 2)
		for (; i < want && i < avail; i++) {
			if ((s[i] & 0xc0) != 0x80)
				break;
			c = c * 64 + (s[i] & 0x3f);
		}
	if (i != want) {
		*len = 1;
		return CONV_CP_INVALID;
	}
	*len = want;
	return c;
}

/* ------------------------------------------------------------ line reader */
typedef struct {
	const char *d;
	size_t len, pos;
} lines;

/* std::getline: next '\n'-terminated line; false at EOF */
static bool next_line(lines *r, const char **s, int *n)
{
	if (r->pos >= r->len)
		return false;
	const char *a = r->d + r->pos;
	const char *e = memchr(a, '\n', r->len - r->pos);
	size_t l = e ? (size_t)(e - a) : r->len - r->pos;
	r->pos += l + (e ? 1 : 0);
	*s = a;
	*n = (int)l;
	return true;
}

/* The common line filter of FUN_005d6430 / FUN_005d7620: skip lines that are
 * all " \t" (find_first_not_of(" \t") == npos) or whose first non-blank code
 * point is '/'; cut trailing " \t\r\n". Returns false at EOF. *first = first
 * non-blank code point. */
static bool content_line(lines *r, const char **s, int *n, uint32_t *first)
{
	for (;;) {
		if (!next_line(r, s, n))
			return false;
		int i = 0;
		while (i < *n && ((*s)[i] == ' ' || (*s)[i] == '\t'))
			i++;
		if (i == *n)
			continue;
		int l;
		uint32_t c = conv_utf8(*s + i, *n - i, &l);
		if (c == '/')
			continue;
		int j = *n;
		while (j > 0 && (*s)[j - 1] && strchr(" \t\r\n", (*s)[j - 1]))
			j--;
		*n = j;
		*first = c;
		return true;
	}
}

/* ------------------------------------------------------------ script parser */
enum { MAX_TOK = 8 };
typedef struct {
	int n;
	sb t[MAX_TOK];
} toks;

/* '<' opens a token, '>' closes it, other code points inside are appended;
 * tokens are trimmed of " \t". A missing '>' still works. */
static void tokenize(toks *k, const char *s, int n)
{
	for (int i = 0; i < k->n; i++)
		sb_clear(&k->t[i]);
	k->n = 0;
	bool in = false;
	for (int i = 0; i < n;) {
		int l;
		uint32_t c = conv_utf8(s + i, n - i, &l);
		if (c == '<') {
			in = true;
			if (k->n < MAX_TOK)
				sb_clear(&k->t[k->n++]);
		} else if (c == '>') {
			in = false;
		} else if (in && k->n > 0) {
			sb_add(&k->t[k->n - 1], s + i, l);
		}
		i += l;
	}
	for (int i = 0; i < k->n; i++) {
		int a, len;
		trim_ws(k->t[i].p ? k->t[i].p : "", k->t[i].n, &a, &len);
		if (k->t[i].p)
			memmove(k->t[i].p, k->t[i].p + a, (size_t)len);
		k->t[i].n = len;
		if (k->t[i].p)
			k->t[i].p[len] = 0;
	}
}
static const char *tok(const toks *k, int i) { return i < k->n && k->t[i].p ? k->t[i].p : ""; }

static void act_free(conv_act *a)
{
	for (int i = 0; i < CONV_SLOTS; i++)
		free(a->text[i]);
	free(a->se);
	free(a->bg);
	free(a->bgm);
	memset(a, 0, sizeof *a);
}

void conv_script_free(conv_script *s)
{
	for (int i = 0; i < s->count; i++)
		act_free(&s->act[i]);
	free(s->act);
	memset(s, 0, sizeof *s);
}

static bool set_str(char **dst, const char *s, int n)
{
	char *p = dupn(s, n);
	if (!p)
		return false;
	free(*dst);
	*dst = p;
	return true;
}

/* text line (states 2/4): ＄ -> trimmed line, U+3000 -> " ", "[text NNNN]" ->
 * "NNNN", else unchanged (empty after the [ACT] memset) */
static bool text_line(char **dst, const char *s, int n, uint32_t first)
{
	if (first == 0xff04) {
		int a, l;
		trim_ws(s, n, &a, &l);
		return set_str(dst, s + a, l);
	}
	if (first == 0x3000)
		return set_str(dst, " ", 1);
	if (n >= 5 && !memcmp(s, "[text", 5)) {
		int a = 6 < n ? 6 : n, l = n - a < 4 ? n - a : 4; /* substr(6, 4) */
		return set_str(dst, s + a, l);
	}
	return true;
}

bool conv_script_parse(conv_script *s, const char *data, size_t len)
{
	memset(s, 0, sizeof *s);
	lines r = {data, len, 0};
	toks k;
	memset(&k, 0, sizeof k);
	conv_act cur;
	memset(&cur, 0, sizeof cur);
	int state = 0;
	bool ok = true;
	const char *ln;
	int n;
	uint32_t c;
	while (ok && content_line(&r, &ln, &n, &c)) {
		switch (state) {
		case 0:
			if (c == '[' && n > 4) {
				if (!memcmp(ln, "[ACT]", 5)) {
					act_free(&cur);
					state = 1;
				} else if (!memcmp(ln, "[EOF]", 5)) {
					goto done;
				}
			}
			break;
		case 1:
		case 3: {
			int slot = state == 3;
			if (c == '<') {
				tokenize(&k, ln, n);
				cur.name[slot] = conv_name_id(tok(&k, 0));
				/* band = strcmp(token1, "帯なし") != 0 (DAT_0081f274); a missing
				 * token compares as "" (the original indexes past the vector) */
				cur.band[slot] = strcmp(tok(&k, 1), "帯なし") != 0;
				/* case 1 also scans DAT_009b1054 for value 10 into a local: dead */
			}
			state++;
			break;
		}
		case 2:
		case 4:
			ok = text_line(&cur.text[state == 4], ln, n, c);
			state++;
			break;
		case 5:
			if (c == '<') {
				tokenize(&k, ln, n);
				ok = set_str(&cur.bg, tok(&k, 0), (int)strlen(tok(&k, 0))) &&
				     set_str(&cur.bgm, tok(&k, 1), (int)strlen(tok(&k, 1))) &&
				     set_str(&cur.se, tok(&k, 2), (int)strlen(tok(&k, 2)));
				cur.field3 = atoi(tok(&k, 3)); /* FUN_007e4d5d */
			}
			/* push (FUN_005da440) */
			for (int i = 0; ok && i < CONV_SLOTS; i++)
				if (!cur.text[i])
					ok = set_str(&cur.text[i], "", 0);
			if (ok && !cur.bg)
				ok = set_str(&cur.bg, "", 0);
			if (ok && !cur.bgm)
				ok = set_str(&cur.bgm, "", 0);
			if (ok && !cur.se)
				ok = set_str(&cur.se, "", 0);
			if (ok && s->count == s->cap) {
				int cap = s->cap ? s->cap * 2 : 32;
				conv_act *a = realloc(s->act, sizeof *a * (size_t)cap);
				if (!a)
					ok = false;
				else {
					s->act = a;
					s->cap = cap;
				}
			}
			if (ok) {
				s->act[s->count++] = cur;
				memset(&cur, 0, sizeof cur);
			}
			state = 0;
			break;
		}
	}
done:
	act_free(&cur);
	for (int i = 0; i < MAX_TOK; i++) {
		ok = ok && !k.t[i].oom;
		sb_free(&k.t[i]);
	}
	if (!ok)
		conv_script_free(s);
	return ok;
}

/* ------------------------------------------------------------ text files */
struct conv_text {
	int n, cap;
	char **key, **val;
};

void conv_text_free(conv_text *t)
{
	if (!t)
		return;
	for (int i = 0; i < t->n; i++) {
		free(t->key[i]);
		free(t->val[i]);
	}
	free(t->key);
	free(t->val);
	free(t);
}

const char *conv_text_get(const conv_text *t, const char *key)
{
	for (int i = 0; t && i < t->n; i++)
		if (!strcmp(t->key[i], key))
			return t->val[i];
	return NULL;
}

int conv_text_count(const conv_text *t) { return t ? t->n : 0; }

/* FUN_005d7b60: every backslash followed by 'n' becomes one '\n' */
static void unescape_nl(sb *v)
{
	int i = 0;
	while (i < v->n) {
		int l;
		uint32_t c = conv_utf8(v->p + i, v->n - i, &l);
		i += l;
		if (c == '\\' && i < v->n) {
			int l2;
			if (conv_utf8(v->p + i, v->n - i, &l2) == 'n') {
				v->p[i - 1] = '\n';
				memmove(v->p + i, v->p + i + l2, (size_t)(v->n - i - l2 + 1));
				v->n -= l2;
			}
		}
	}
}

conv_text *conv_text_parse(const char *data, size_t len)
{
	conv_text *t = calloc(1, sizeof *t);
	if (!t)
		return NULL;
	lines r = {data, len, 0};
	sb key = {0}, val = {0};
	sb_set(&key, "", 0);
	const char *ln;
	int n;
	uint32_t c;
	bool ok = true;
	while (ok && content_line(&r, &ln, &n, &c)) {
		if (c == '<') {
			const char *gt = memchr(ln, '>', (size_t)n);
			int a, l, from;
			if (gt) {
				/* key = trim(substr(1, gt-1)) of the line as read */
				int g = (int)(gt - ln);
				trim_ws(ln + 1, g - 1 < 0 ? 0 : g - 1, &a, &l);
				sb_set(&key, ln + 1 + a, l);
				from = g + 1;
			} else {
				from = 0; /* substr(npos + 1) = whole line; key kept (literal) */
			}
			trim_ws(ln + from, n - from, &a, &l);
			sb_set(&val, ln + from + a, l);
			unescape_nl(&val);
			if (key.oom || val.oom) {
				ok = false;
				break;
			}
			if (conv_text_get(t, key.p)) /* std::map insert keeps the first */
				continue;
			if (t->n == t->cap) {
				int cap = t->cap ? t->cap * 2 : 64;
				char **k2 = realloc(t->key, sizeof *k2 * (size_t)cap);
				if (k2)
					t->key = k2;
				char **v2 = realloc(t->val, sizeof *v2 * (size_t)cap);
				if (v2)
					t->val = v2;
				if (!k2 || !v2) {
					ok = false;
					break;
				}
				t->cap = cap;
			}
			char *kk = dupn(key.p, key.n), *vv = dupn(val.p, val.n);
			if (!kk || !vv) {
				free(kk);
				free(vv);
				ok = false;
				break;
			}
			t->key[t->n] = kk;
			t->val[t->n] = vv;
			t->n++;
		} else if (c == '[' && n > 4 && !memcmp(ln, "[EOF]", 5)) {
			break;
		}
	}
	sb_free(&key);
	sb_free(&val);
	if (!ok) {
		conv_text_free(t);
		return NULL;
	}
	return t;
}

int conv_script_resolve(conv_script *s, const conv_text *t, int *unresolved)
{
	int subs = 0, miss = 0;
	for (int i = 0; i < s->count; i++)
		for (int j = 0; j < CONV_SLOTS; j++) {
			char **p = &s->act[i].text[j];
			if ((*p)[0] != '0')
				continue;
			const char *v = conv_text_get(t, *p);
			if (!v) {
				miss++;
				continue;
			}
			if (!set_str(p, v, (int)strlen(v)))
				return -1;
			subs++;
		}
	if (unresolved)
		*unresolved = miss;
	return subs;
}

static char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	char *b = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t)n + 1)) != NULL) {
			if (fread(b, 1, (size_t)n, f) != (size_t)n) {
				free(b);
				b = NULL;
			} else {
				b[n] = 0;
				*len = (size_t)n;
			}
		}
	}
	fclose(f);
	return b;
}

bool conv_script_load(conv_script *s, const char *root, int id, int lang)
{
	char name[64], path[512];
	memset(s, 0, sizeof *s);
	conv_res_name(id, name, sizeof name);
	snprintf(path, sizeof path, "%s/res/%s.txt", root, name);
	size_t n = 0;
	char *d = read_file(path, &n);
	if (!d)
		return false;
	bool ok = conv_script_parse(s, d, n);
	free(d);
	if (!ok || !conv_story_file(id, name, sizeof name))
		return false;
	snprintf(path, sizeof path, "%s/%s/%s", root, conv_lang_dir(lang), name);
	d = read_file(path, &n);
	conv_text *t = d ? conv_text_parse(d, n) : NULL;
	free(d);
	if (!t || conv_script_resolve(s, t, NULL) < 0) {
		conv_text_free(t);
		conv_script_free(s);
		return false;
	}
	conv_text_free(t);
	return true;
}

int conv_script_portraits(const conv_script *s, int *ids, int max)
{
	int n = 0;
	for (int i = 0; i < s->count; i++)
		for (int j = 0; j < CONV_SLOTS; j++) {
			int id = s->act[i].name[j], k;
			for (k = 0; k < n && ids[k] != id; k++)
				;
			if (k == n && n < max)
				ids[n++] = id;
		}
	return n;
}

/* ------------------------------------------------------------ layout */
float conv_text_width(const conv_font *f, const char *s, int len, float sx)
{
	float cur = 0, best = 0;
	int i = 0;
	while (i < len) {
		int l;
		uint32_t c = conv_utf8(s + i, len - i, &l);
		i += l;
		if (c == '\n') {
			best = cur > best ? cur : best; /* maxss */
			cur = 0;
			continue;
		}
		conv_glyph g;
		if (!f->glyph(f->ctx, c, &g))
			continue;
		cur = (float)g.xadv * sx + cur;
		if (f->has_outline)
			cur = (float)(int)((float)f->outline * sx) + cur;
		if (i >= len)
			break;
		int l2;
		uint32_t nx = conv_utf8(s + i, len - i, &l2);
		cur = (float)f->kerning(f->ctx, c, nx) * sx + cur;
	}
	return cur > best ? cur : best;
}

static float sbw(const conv_font *f, const sb *b, float sx) { return conv_text_width(f, b->p ? b->p : "", b->n, sx); }
static bool ends_nl(const sb *b) { return b->n > 0 && b->p[b->n - 1] == '\n'; }

/* drop one leading ' ' / U+3000 of the pending word after a break */
static void strip_space(sb *w)
{
	if (w->n == 0)
		return;
	int l;
	uint32_t c = conv_utf8(w->p, w->n, &l);
	if (c == ' ' || c == 0x3000) {
		memmove(w->p, w->p + l, (size_t)(w->n - l + 1));
		w->n -= l;
	}
}

/* height of the wrapped text (FUN_0047e2c0 tail): top of the first line's
 * highest glyph + line advances + bottom of the last line */
static float wrapped_height(const conv_font *f, const sb *out, float sy)
{
	if (out->n < 1)
		return 0;
	float top = 9999.0f /* DAT_00853b9c */, lines = 0, bottom = 0;
	bool first = true;
	for (int i = 0; i < out->n;) {
		int l;
		uint32_t c = conv_utf8(out->p + i, out->n - i, &l);
		i += l;
		if (c == '\n') {
			first = false;
			lines = (float)(int)(((float)f->base + f->extra) * sy + 0.5f) + lines;
			bottom = 0;
			continue;
		}
		conv_glyph g;
		if (!f->glyph(f->ctx, c, &g))
			continue;
		float y = (float)g.yoff * sy;
		if (first && y < top)
			top = y;
		float b = (float)g.h * sy + y;
		if (b > bottom)
			bottom = b;
	}
	return top + lines + bottom;
}

char *conv_wrap(const conv_font *f, const char *text, int maxW, int maxH, float *psx, float *psy)
{
	const int len = (int)strlen(text);
	sb out = {0}, word = {0}, cand = {0};
	sb_set(&out, "", 0);
	sb_set(&word, "", 0);
	uint32_t quote = CONV_CP_INVALID; /* local_e0 */
	float indent = 0;                 /* local_dc: hanging indent */
	for (;;) {
		const float sx = *psx;
		if (len > 0) {
			int l;
			uint32_t c0 = conv_utf8(text, len, &l);
			conv_glyph g;
			if ((c0 == '"' || c0 == 0x300c) && (quote = c0, f->glyph(f->ctx, c0, &g)))
				indent = (float)g.xadv * sx;
		}
		float lineW = 0, wordW = 0, b8 = 0; /* local_ac, local_a8, local_b8 */
		int pos = 0;
		while (pos < len) {
			const int start = pos;
			int l;
			uint32_t c = conv_utf8(text + pos, len - pos, &l);
			pos += l;
			bool brk = false;
			if (c == '\n') {
				float a8 = wordW + lineW;
				if ((int)(a8 + 0.5f) > maxW) {
					if (out.n != 0 && !ends_nl(&out)) {
						sb_add(&out, "\n", 1);
						b8 = indent + 0.0f;
						strip_space(&word);
					}
					sb_add(&out, word.p, word.n);
					b8 = sbw(f, &word, sx) + b8;
				} else {
					sb_add(&out, word.p, word.n);
					b8 = a8;
				}
				sb_clear(&word);
				brk = true;
			} else if (c != ' ' && c != 0x3000 && pos < len) {
				/* ordinary code point (not the last one) */
				if (start != 0 && c == quote)
					indent = 0; /* the closing '"' ends the hanging indent */
				sb_set(&cand, word.p, word.n);
				sb_add(&cand, text + start, l);
				float w = sbw(f, &cand, sx);
				if ((int)(w + lineW + 0.5f) > maxW && (int)(w + 0.5f) >= maxW) {
					/* the word alone no longer fits a line: break inside it */
					if (lineW > 0.0f && out.n != 0 && !ends_nl(&out)) {
						sb_add(&out, "\n", 1);
						lineW = indent + 0.0f;
					}
					sb_add(&out, word.p, word.n);
					b8 = wordW + lineW;
					lineW = b8;
					if (out.n != 0 && !ends_nl(&out)) {
						sb_add(&out, "\n", 1);
						lineW = indent + 0.0f;
					}
					b8 = lineW;
					sb_set(&word, text + start, l);
				} else {
					sb_add(&word, text + start, l);
				}
				wordW = sbw(f, &word, sx);
			} else {
				/* ' ', U+3000, or the last code point: the pending word ends */
				sb_set(&cand, word.p, word.n);
				float cw = wordW;
				if (pos >= len) {
					sb_add(&cand, text + start, l);
					cw = sbw(f, &cand, sx);
				}
				if ((int)(cw + lineW + 0.5f) > maxW) {
					brk = true;
				} else {
					sb_add(&out, cand.p, cand.n);
					lineW = b8 = cw + lineW;
					sb_set(&word, text + start, l);
					wordW = sbw(f, &word, sx);
				}
			}
			if (brk) { /* LAB_0047ef26: new line, then the pending word */
				if (out.n != 0 && !ends_nl(&out)) {
					sb_add(&out, "\n", 1);
					b8 = indent + 0.0f;
					strip_space(&word);
				}
				sb_add(&out, word.p, word.n);
				b8 = sbw(f, &word, sx) + b8;
				lineW = b8;
				if (c == '\n') {
					sb_clear(&word);
					wordW = 0;
				} else {
					sb_set(&word, text + start, l);
					wordW = sbw(f, &word, sx);
					if (pos >= len) {
						if ((int)(wordW + lineW + 0.5f) > maxW)
							sb_add(&out, "\n", 1);
						sb_add(&out, word.p, word.n);
					}
				}
			}
		}
		float h = wrapped_height(f, &out, *psy);
		if (h - 0.0f <= (float)maxH || out.oom || word.oom || cand.oom)
			break;
		/* shrink and retry (DAT_00853b10 = 0.1); port guard: stop at <= 0.1 */
		if (*psx <= 0.1f || *psy <= 0.1f)
			break;
		*psy = *psy - 0.1f;
		*psx = *psx - 0.1f;
		sb_clear(&out);
		sb_clear(&word);
	}
	bool oom = out.oom || word.oom || cand.oom;
	sb_free(&word);
	sb_free(&cand);
	if (oom) {
		sb_free(&out);
		return NULL;
	}
	return out.p;
}

/* ------------------------------------------------------------ scene */
void conv_layout_original(conv_layout *l, const conv_font *font, float scale, int W, int H)
{
	memset(l, 0, sizeof *l);
	float sw = (float)W / 800.0f, sh = (float)H / 600.0f;
	l->s = sw < sh ? sw : sh;
	l->font = font;
	l->scale = scale;
	l->W = W;
	l->H = H;
	for (int i = 0; i < CONV_SLOTS; i++) {
		l->box_w[i] = (int)(540.0f * l->s); /* 0x81412c */
		l->box_h[i] = (int)(88.0f * l->s);  /* 0x817000 */
	}
}

unsigned conv_init(conv_scene *c, const conv_script *s, int script_id, const conv_layout *lay)
{
	memset(c, 0, sizeof *c);
	c->script = s;
	c->script_id = script_id;
	c->lay = *lay;
	c->state = CONV_ST_FADEIN;
	c->init_pending = true;
	for (int i = 0; i < CONV_SLOTS; i++) {
		c->sx[i] = c->sy[i] = lay->scale;
		c->full[i] = NULL;
	}
	return CONV_EV_FADE_IN; /* FUN_00561fc0(5) */
}

void conv_free(conv_scene *c)
{
	for (int i = 0; i < CONV_SLOTS; i++) {
		free(c->full[i]);
		c->full[i] = NULL;
	}
}

/* FUN_004e5190 */
static bool next_act(conv_scene *c, unsigned *ev)
{
	const conv_script *s = c->script;
	if (c->act >= s->count)
		return false;
	const conv_act *a = &s->act[c->act];
	if (c->act == 0 || strcmp(s->act[c->act - 1].bgm, a->bgm) != 0)
		*ev |= CONV_EV_BGM_FADE;
	if (strcmp(a->se, "なし") != 0) { /* FUN_004e6380 vs DAT_00816cb0 */
		c->se = a->se;
		*ev |= CONV_EV_SE;
	}
	for (int i = 0; i < CONV_SLOTS; i++) {
		if (!strcmp(a->text[i], "＄")) { /* DAT_00816cd8: keep the old text, dimmed */
			c->speak[i] = false;
		} else {
			c->speak[i] = true;
			float sx = c->lay.scale, sy = c->lay.scale;
			char *t = c->lay.font ? conv_wrap(c->lay.font, a->text[i], c->lay.box_w[i], c->lay.box_h[i], &sx, &sy)
			                      : dupn(a->text[i], (int)strlen(a->text[i]));
			if (!t) {
				c->oom = true;
				t = dupn("", 0);
			}
			free(c->full[i]);
			c->full[i] = t;
			c->len[i] = t ? (int)strlen(t) : 0;
			c->n[i] = 0;
			c->sx[i] = sx;
			c->sy[i] = sy;
		}
		c->band[i] = a->band[i];
		c->name[i] = a->name[i];
	}
	c->fade = 0;
	c->act++;
	return true;
}

static uint32_t a8(int a) { return (uint32_t)(a & 0xff) << 24; }

/* FUN_004e38e0 + FUN_004e5af0 + FUN_004e54b0 + FUN_004e3fc0 */
static void build_view(conv_scene *c)
{
	conv_view *v = &c->view;
	const conv_layout *L = &c->lay;
	const conv_script *s = c->script;
	const float sc = L->s;
	const int W = L->W, H = L->H;
	const int cur = c->act - 1, prev = c->act - 2;
	const int afade = (int)(c->fade * 255.0f); /* DAT_00853b94 */
	memset(v, 0, sizeof *v);
	v->clear_argb = 0xff0055aa;
	if (cur < 0)
		return;
	const conv_act *ca = &s->act[cur], *pa = prev >= 0 ? &s->act[prev] : NULL;

	/* backdrop fill (the textured-backdrop path is dead: DAT_00894138 empty) */
	bool bgchg = pa && strcmp(pa->bg, ca->bg) != 0;
	if (pa && bgchg) {
		int al = 255 - afade;
		if (!strcmp(pa->bg, "黒地"))
			v->fill_argb[v->nfill++] = a8(al) | 0x202020;
		if (!strcmp(pa->bg, "白地"))
			v->fill_argb[v->nfill++] = a8(al) | 0xffffff;
	}
	int al = (c->act == 1 || !bgchg) ? 255 : afade;
	if (!strcmp(ca->bg, "黒地"))
		v->fill_argb[v->nfill++] = a8(al) | 0x202020;
	else if (!strcmp(ca->bg, "白地"))
		v->fill_argb[v->nfill++] = a8(al) | 0xffffff;

	/* letterbox, 220 (0x817008) */
	v->bar_top = H / 2 - (int)(220.0f * sc);
	v->bar_bottom = H / 2 + (int)(220.0f * sc);

	for (int i = 0; i < CONV_SLOTS; i++) {
		conv_slot_view *o = &v->slot[i];
		/* band: centre y (y-up) -140s / +140s, half height 50s, tiles 256s */
		if (c->band[i]) {
			float f = 50.0f * sc, yc = (i == 0 ? -140.0f : 140.0f) * sc; /* 0x817010 / 0x817004 */
			o->band = true;
			o->tile_w = (int)(256.0f * sc); /* 0x814160 */
			o->tiles = o->tile_w > 0 ? W / o->tile_w + 1 : 0;
			o->noise = (c->frame / 4) % 8;
			o->y0 = (float)(H / 2) - (yc + f); /* screen y of the y-up top edge */
			o->y1 = (float)(H / 2) - (yc - f);
			o->v_top = 128.0f * sc + f; /* 0x814134 */
			o->v_bottom = 128.0f * sc - f;
			o->band_argb = 0xff404040;
		}
		/* portrait rect */
		int cy = H / 2 + (i == 0 ? 1 : -1) * (int)(140.0f * sc);
		cy = ((int)(50.0f * sc) + cy) - (int)(128.0f * sc);
		int cx = W / 2 + (i == 0 ? -1 : 1) * (int)(300.0f * sc); /* 0x8145f0 */
		int hw = (int)(292.0f * sc), hh = (int)(128.0f * sc);    /* 0x81700c, 0x814134 */
		int l = i == 0 ? cx + hw : cx - hw, r = i == 0 ? cx - hw : cx + hw;
		uint32_t rgb = c->speak[i] ? 0xffffff : 0x808080;
		if (c->act > 1 && pa->name[i] == ca->name[i]) {
			/* outgoing portrait: drawn only when the id did NOT change
			 * (0x4e5780: it = end() when the ids differ) */
			o->port[o->nport++] = (conv_portrait_draw){pa->name[i], l, cy - hh, r, cy + hh, a8(255 - afade) | rgb};
		}
		int pal = c->act == 1 ? afade : (pa->name[i] == ca->name[i] ? 255 : afade);
		o->port[o->nport++] = (conv_portrait_draw){c->name[i], l, cy - hh, r, cy + hh, a8(pal) | rgb};
		/* text */
		o->text = c->full[i] ? c->full[i] : "";
		o->text_len = c->n[i];
		if (i == 0) {
			o->text_y = H / 2 + (int)(140.0f * sc);
			o->text_x = W / 2 - (int)(150.0f * sc); /* 0x81414c */
		} else {
			o->text_y = H / 2 - (int)(140.0f * sc);
			o->text_x = W / 2 - (int)(350.0f * sc); /* 0x814be0 */
		}
		o->text_y -= (int)(30.0f * sc); /* 0x8140e8 */
		o->sx = c->sx[i];
		o->sy = c->sy[i];
		o->speaking = c->speak[i];
		o->text_argb = c->speak[i] ? 0xffffffff : 0xff808080;
	}
}

unsigned conv_frame(conv_scene *c, const conv_input *in, bool busy)
{
	unsigned ev = 0;
	if (c->state == CONV_ST_DONE)
		return 0;
	if (c->init_pending) {
		next_act(c, &ev);
		c->init_pending = false;
	}
	bool exit_now = false;
	if (c->state == CONV_ST_FADEIN) {
		if (!busy) {
			c->state = CONV_ST_TALK;
			c->f0 = 0;
		}
	} else if (c->state == CONV_ST_TALK) {
		/* typewriter: one UTF-8 code point per frame per speaking slot */
		for (int i = 0; i < CONV_SLOTS; i++)
			if (c->speak[i] && c->n[i] >= 0 && c->n[i] < c->len[i]) {
				int l;
				conv_utf8(c->full[i] + c->n[i], c->len[i] - c->n[i], &l); /* FUN_00458dd0 */
				c->n[i] += l;
			}
		/* FUN_0061c7b0 */
		bool confirm = in->ok == 3 || (in->attack == 3 && in->cancel != 3 && in->cancel != 2);
		if (confirm || (in->special > 1 && c->f0 % 5 == 0)) {
			bool done = true;
			for (int i = 0; i < CONV_SLOTS; i++)
				if (c->n[i] != c->len[i]) { /* shown != full */
					c->n[i] = c->len[i];
					done = false;
				}
			if (done) {
				if (!next_act(c, &ev)) {
					c->state = CONV_ST_FADEOUT;
					ev |= CONV_EV_FADE_OUT; /* FUN_00561fc0(1) */
				} else {
					c->state = CONV_ST_TALK;
					c->f0 = 0;
				}
			}
		}
		c->fade = c->fade + 0.05f; /* DAT_00813ef4 */
		if (1.0f < c->fade)
			c->fade = 1.0f;
	} else if (c->state == CONV_ST_FADEOUT && !busy) {
		exit_now = true;
	}
	if (exit_now) {
		c->state = CONV_ST_DONE;
		ev |= CONV_EV_EXIT;
	} else {
		build_view(c);
	}
	c->frame++;
	c->f0++;
	return ev;
}
