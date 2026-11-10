/*
 * Menu text: string tables (script/<lang>/<name>.txt, "< KEY >" blocks) and the
 * BMFont renderer (modes.md A.1). Port API used by all menu parts.
 *
 * Fonts are the original BMFont v3 binary files, repacked by tools/aos2pack
 * (fonts.go): only the glyphs the shipped texts use, pre-scaled to the
 * 3DS on-screen size (see FONT_* comments) so scale 1.0 draws 1:1 texels,
 * pages as LA8 textures (lossless enough for the baked outlines; see
 * docs/STATUS.md for the memory numbers). romfs:/font/<name>.fnt + _<n>.t3x.
 *
 * Draw semantics follow FUN_005931d0: y is the vertical centre of the first
 * line, align 0 left / 1 centre / 2 right per line, lines split at '\n' only
 * (no word wrap; use text_wrap() for bottom-screen paragraphs, port-only).
 * Colours/outlines are baked into the glyphs: the colour argument multiplies
 * (white = as drawn by the PC with 0xffffffff).
 */
#ifndef AOS_APP_TEXT_H
#define AOS_APP_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Font families (modes_menu_fonts.tsv) packed for the port. Pixel sizes =
 * line height on screen at scale 1.0 (the PC at 768 lines/0.3125 would be
 * 8.75/16/5.6/7.5/10 px, illegible: modes.md A.7). */
typedef enum {
	FONT_SYSTEM,       /* help line, bottom screen (system_1080, 16 px line) */
	FONT_MENU,         /* main-menu items, config rows (menu_1080, 18 px line) */
	FONT_MENUBIG,      /* sub-menu row titles (menubig_1080, 20 px line) */
	FONT_MENULABEL,    /* header pill labels (menulabel_1080, 12 px line) */
	FONT_MENUSUBTITLE, /* *_SUBTITLE lines (menusubtitle_1080, 14 px line) */
	FONT_WINQUOTE,     /* result win quote (winquote_1080, 16 px line) — Part B */
	FONT_DIALOGUE,     /* conversation text (dialogue_1080, 16 px line) — Part C */
	FONT_COUNT
} font_id;

enum { TEXT_LEFT = 0, TEXT_CENTER = 1, TEXT_RIGHT = 2 };

/* ---------------------------------------------------------------- strings */
/* Languages = DAT_00881ec0 values. */
enum { LANG_EN = 0, LANG_JP, LANG_CHS, LANG_CHT, LANG_PTBR, LANG_SP, LANG_KO, LANG_RU, LANG_COUNT };
const char *lang_dir(int lang);     /* script dir: en, jp, chs, cht, ptbr, sp, ko, ru */
bool lang_available(int lang);      /* glyphs packed for it (en, ptbr, sp) */

/* A parsed "< KEY >" file: keys in file order, values = following lines up
 * to the next key, trailing blank lines/space trimmed, "\n" kept between lines. */
typedef struct kv_table kv_table;
kv_table *kv_parse(const char *data, size_t len); /* copies; NULL on OOM */
kv_table *kv_load(const char *path);              /* NULL if missing */
void kv_free(kv_table *t);
const char *kv_get(const kv_table *t, const char *key); /* NULL if absent */
int kv_count(const kv_table *t);
const char *kv_key_at(const kv_table *t, int i);
const char *kv_value_at(const kv_table *t, int i);
int kv_index(const kv_table *t, const char *key); /* -1 if absent */

/* Menu strings of the current language: menu.txt, menu_label.txt,
 * menu_subtitle.txt, menu_header.txt, announce.txt, characternames.txt,
 * bgm.txt, titles.txt, win_quote.txt (Part B may add files with
 * strings_add_file). Reloaded by strings_set_language. root = "romfs:/script"
 * on 3DS, extracted/script on the host. */
bool strings_init(const char *root, int lang);
void strings_exit(void);
bool strings_add_file(const char *name); /* e.g. "story_sora_01.txt" -> own table, see kv_load */
int strings_language(void);
/* Lookup in all loaded menu tables; returns the key itself if missing (never NULL). */
const char *tr(const char *key);
/* Menu text id (modes_menu_textids.tsv, FUN_00633e50) -> key, NULL if unknown id. */
const char *text_id_key(int id);
static inline const char *tr_id(int id) { const char *k = text_id_key(id); return k ? tr(k) : ""; }
/* Announce id (0-based order in announce.txt) -> text ("" if out of range). */
const char *announce_by_id(int id);
int announce_id_of(const char *key); /* -1 if absent */

/* ---------------------------------------------------------------- fonts */
/* Load / free the packed font pages (romfs:/font). Fonts are freed before a
 * match (text_fonts_unload) and reloaded lazily by the next draw. */
bool text_fonts_load(void);
void text_fonts_unload(void);
size_t text_fonts_bytes(void); /* linear memory of the loaded pages */

/* Width in pixels of the widest line / total height (line advance × lines). */
float text_width(font_id f, const char *utf8, float scale);
float text_height(font_id f, const char *utf8, float scale);
float text_line_height(font_id f, float scale); /* lineHeight·scale */
/* Draw (3DS: citro2d, current target). maxw > 0 squeezes horizontally to
 * fit like the text object ctor (FUN_00593430); 0 = no limit. rgba as
 * C2D_Color32 (0xAABBGGRR). */
void text_draw(font_id f, const char *utf8, float x, float y, int align, uint32_t rgba, float scale, float maxw);
/* PC-style 4 offset shadows (±1,±1, 0x20 alpha) + text: the help line draw. */
void text_draw_shadowed(font_id f, const char *utf8, float x, float y, int align, uint32_t rgba, float scale, float maxw);
/* Port-only: greedy word wrap at spaces to maxw pixels into out (adds '\n').
 * Returns the line count. */
int text_wrap(font_id f, const char *utf8, float scale, float maxw, char *out, size_t outsz);

#endif
