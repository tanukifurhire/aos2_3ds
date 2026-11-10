/* Script text tables ("< KEY >" files, modes.md A.1.1). Portable. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"
#include "text_ids.h"

struct kv_table {
	char *buf;
	int n;
	const char **key, **val;
	int *sorted; /* indices sorted by key */
};

static const kv_table *sort_tab;
static int cmp_idx(const void *a, const void *b)
{
	return strcmp(sort_tab->key[*(const int *)a], sort_tab->key[*(const int *)b]);
}

static int is_key_line(char *l, char **k)
{
	size_t n = strlen(l);
	while (n && (l[n - 1] == ' ' || l[n - 1] == '\t'))
		l[--n] = 0;
	if (n < 3 || l[0] != '<' || l[n - 1] != '>')
		return 0;
	l[n - 1] = 0;
	char *s = l + 1;
	while (*s == ' ')
		s++;
	size_t m = strlen(s);
	while (m && s[m - 1] == ' ')
		s[--m] = 0;
	if (!m)
		return 0;
	for (char *p = s; *p; p++)
		if (*p == ' ' || *p == '<' || *p == '>')
			return 0;
	*k = s;
	return 1;
}

kv_table *kv_parse(const char *data, size_t len)
{
	kv_table *t = calloc(1, sizeof *t);
	if (!t)
		return NULL;
	t->buf = malloc(len + 1);
	if (!t->buf)
		goto oom;
	/* copy without '\r' and BOM */
	size_t o = 0, i = 0;
	if (len >= 3 && (unsigned char)data[0] == 0xef && (unsigned char)data[1] == 0xbb && (unsigned char)data[2] == 0xbf)
		i = 3;
	for (; i < len; i++)
		if (data[i] != '\r')
			t->buf[o++] = data[i];
	t->buf[o] = 0;
	int cap = 0;
	char *val = NULL, *vend = NULL;
	for (char *l = t->buf; l;) {
		char *nl = strchr(l, '\n');
		if (nl)
			*nl = 0;
		char *k;
		if (is_key_line(l, &k)) {
			if (t->n && vend)
				*vend = 0;
			if (t->n == cap) {
				cap = cap ? cap * 2 : 64;
				const char **nk = realloc(t->key, sizeof *nk * (size_t)cap);
				if (!nk)
					goto oom;
				t->key = nk;
				const char **nv = realloc(t->val, sizeof *nv * (size_t)cap);
				if (!nv)
					goto oom;
				t->val = nv;
			}
			t->key[t->n] = k;
			t->val[t->n] = "";
			t->n++;
			val = vend = NULL;
		} else if (t->n) {
			/* value lines: join with '\n', trailing blank lines dropped */
			size_t ll = strlen(l);
			while (ll && (l[ll - 1] == ' ' || l[ll - 1] == '\t'))
				l[--ll] = 0;
			if (!val) {
				if (ll) {
					val = l;
					vend = l + ll;
					t->val[t->n - 1] = val;
				}
			} else if (ll) {
				/* move this line right after the previous content (in place, buffer only shrinks) */
				*vend++ = '\n';
				memmove(vend, l, ll);
				vend += ll;
				*vend = 0;
			} else {
				/* blank line inside a value: keep it only if more text follows (handled lazily) */
				*vend++ = '\n';
				*vend = 0;
			}
		}
		l = nl ? nl + 1 : NULL;
		if (l && val && vend >= l) /* never happens: vend <= end of current line */
			break;
	}
	/* strip trailing newlines of every value */
	for (int k = 0; k < t->n; k++) {
		char *v = (char *)t->val[k];
		size_t n = strlen(v);
		while (n && v[n - 1] == '\n')
			v[--n] = 0;
	}
	t->sorted = malloc(sizeof *t->sorted * (size_t)(t->n ? t->n : 1));
	if (!t->sorted)
		goto oom;
	for (int k = 0; k < t->n; k++)
		t->sorted[k] = k;
	sort_tab = t;
	qsort(t->sorted, (size_t)t->n, sizeof *t->sorted, cmp_idx);
	return t;
oom:
	kv_free(t);
	return NULL;
}

kv_table *kv_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)(n > 0 ? n : 1));
	kv_table *t = NULL;
	if (b && fread(b, 1, (size_t)n, f) == (size_t)n)
		t = kv_parse(b, (size_t)n);
	free(b);
	fclose(f);
	return t;
}

void kv_free(kv_table *t)
{
	if (!t)
		return;
	free(t->buf);
	free(t->key);
	free(t->val);
	free(t->sorted);
	free(t);
}

int kv_index(const kv_table *t, const char *key)
{
	if (!t)
		return -1;
	int lo = 0, hi = t->n - 1;
	while (lo <= hi) {
		int m = (lo + hi) / 2, c = strcmp(key, t->key[t->sorted[m]]);
		if (!c)
			return t->sorted[m];
		if (c < 0)
			hi = m - 1;
		else
			lo = m + 1;
	}
	return -1;
}

const char *kv_get(const kv_table *t, const char *key)
{
	int i = kv_index(t, key);
	return i < 0 ? NULL : t->val[i];
}
int kv_count(const kv_table *t) { return t ? t->n : 0; }
const char *kv_key_at(const kv_table *t, int i) { return t && i >= 0 && i < t->n ? t->key[i] : NULL; }
const char *kv_value_at(const kv_table *t, int i) { return t && i >= 0 && i < t->n ? t->val[i] : NULL; }

/* ---------------------------------------------------------------- menu strings */

static const char *const langdirs[LANG_COUNT] = {"en", "jp", "chs", "cht", "ptbr", "sp", "ko", "ru"};
const char *lang_dir(int lang) { return lang >= 0 && lang < LANG_COUNT ? langdirs[lang] : "en"; }
bool lang_available(int lang) { return lang == LANG_EN || lang == LANG_PTBR || lang == LANG_SP; }

static const char *const menu_files[] = {"menu.txt", "menu_label.txt", "menu_subtitle.txt", "menu_header.txt",
                                         "announce.txt", "characternames.txt", "bgm.txt", "titles.txt",
                                         "win_quote.txt"};
#define NMENU ((int)(sizeof menu_files / sizeof menu_files[0]))
#define MAXTAB (NMENU + 16)
static kv_table *tabs[MAXTAB];
static int ntabs, cur_lang;
static kv_table *announce_tab;
static char root_dir[128];

static kv_table *load_one(const char *name)
{
	char path[256];
	snprintf(path, sizeof path, "%s/%s/%s", root_dir, lang_dir(cur_lang), name);
	kv_table *t = kv_load(path);
	if (!t && cur_lang != LANG_EN) { /* fall back to English for a missing file */
		snprintf(path, sizeof path, "%s/en/%s", root_dir, name);
		t = kv_load(path);
	}
	return t;
}

bool strings_init(const char *root, int lang)
{
	strings_exit();
	snprintf(root_dir, sizeof root_dir, "%s", root);
	cur_lang = lang >= 0 && lang < LANG_COUNT ? lang : LANG_EN;
	bool ok = true;
	for (int i = 0; i < NMENU; i++) {
		kv_table *t = load_one(menu_files[i]);
		if (!t) {
			ok = false;
			continue;
		}
		if (!strcmp(menu_files[i], "announce.txt"))
			announce_tab = t;
		tabs[ntabs++] = t;
	}
	return ok;
}

bool strings_add_file(const char *name)
{
	if (ntabs >= MAXTAB)
		return false;
	kv_table *t = load_one(name);
	if (t)
		tabs[ntabs++] = t;
	return t != NULL;
}

void strings_exit(void)
{
	for (int i = 0; i < ntabs; i++)
		kv_free(tabs[i]);
	ntabs = 0;
	announce_tab = NULL;
}

int strings_language(void) { return cur_lang; }

const char *tr(const char *key)
{
	for (int i = 0; i < ntabs; i++) {
		const char *v = kv_get(tabs[i], key);
		if (v)
			return v;
	}
	return key;
}

const char *text_id_key(int id)
{
	for (unsigned i = 0; i < sizeof text_ids / sizeof text_ids[0]; i++)
		if (text_ids[i].id == id)
			return text_ids[i].key;
	return NULL;
}

const char *announce_by_id(int id)
{
	const char *v = kv_value_at(announce_tab, id);
	return v ? v : "";
}

int announce_id_of(const char *key) { return kv_index(announce_tab, key); }
