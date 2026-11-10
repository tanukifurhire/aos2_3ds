/* Textures of the select / VS / result / continue / loading screens (Part B,
 * docs/re/modes.md §B) — single t3x files packed by tools/aos2pack/seltex.go
 * (romfs:/gfx/m3_b_<name>_0.t3x) and the draw helpers the B scenes use.
 * Source rects are in ORIGINAL texture pixels (the PC's src rects); colours are
 * PC ARGB; blend 1 = alpha, 2 = additive (FUN_004637d0). 3DS only. */
#ifndef AOS_APP_SELGFX_H
#define AOS_APP_SELGFX_H

#include <stdbool.h>
#include <stddef.h>

#include "select.h"
#include "ui.h"

typedef struct {
	ui_tex t;
	float ox, oy, sc; /* crop origin + scale of the packed image */
	char key[48];     /* loaded name ("" = none) */
} bgfx;

/* Static textures (seltex.go selStatic), index = order there. */
typedef enum {
	BG_SLBGP, BG_CHARA00, BG_CHARA01, BG_CHARA02, BG_ICON00, BG_ICON01, BG_NAME0, BG_SELSYS,
	BG_LOCKB, BG_LOCKBB, BG_LOCKR, BG_LOCKRB, BG_STATBASE, BG_BAREMPTY, BG_BAR25, BG_BAR50, BG_BAR75,
	BG_BARFULL, BG_RATED, BG_RATEC, BG_RATEB, BG_RATEA, BG_RATES, BG_STAR, BG_STAGENAME, BG_LOADING,
	BG_RESSYSTEM, BG_RESSYSTEM2, BG_RESTAG, BG_RESCONT,
	BG_COUNT
} bg_id;

bool bg_load(bg_id id);          /* idempotent */
void bg_free(bg_id id);
void bg_free_all(void);          /* static + every dynamic slot registered with bgfx_track */
bgfx *bg_get(bg_id id);
size_t bg_bytes(void);           /* linear memory of everything loaded here */

/* Dynamic textures: name = romfs:/gfx/m3_b_<name>_0.t3x (e.g. "std_charaSuguriA_00").
 * Reloads only when the name changes. A slot is tracked for bg_free_all / bg_bytes
 * after its first load. */
bool bgfx_load(bgfx *g, const char *name, float ox, float oy, float sc);
void bgfx_free(bgfx *g);
bool bgfx_exists(const char *name);

/* Draw src (sx0,sy0)-(sx1,sy1) into the screen rect; mirror = flip horizontally. */
void bgfx_draw(const bgfx *g, float sx0, float sy0, float sx1, float sy1, float x0, float y0, float x1, float y1,
               unsigned argb, int blend, bool mirror);
/* Centre / half-extent form with rotation (radians, about the centre). */
void bgfx_draw_c(const bgfx *g, float sx0, float sy0, float sx1, float sy1, float cx, float cy, float hw, float hh,
                 float angle, unsigned argb, int blend, bool mirror);

/* Solid quad centred at (cx,cy), half extents, rotated (FUN_0045c7d0 with a flat cell). */
void sel_quad(float cx, float cy, float hw, float hh, float angle, unsigned argb);
/* Rotated quad given by its local rect (x0..x1, y0..y1) relative to the pivot (px,py). */
void sel_quad_local(float px, float py, float x0, float y0, float x1, float y1, float angle, unsigned argb);

/* Palette folder of a character + colour 0..2 that has packed result art
 * (charaXxxA/B/C, falls back to lower palettes; char 18 -> charaAdd). */
void sel_palette_folder(char *out, size_t n, int chara, int color);
/* Two-letter art prefix (sg, sr, ...) of an engine id ("" if none). */
const char *sel_char_prefix(int chara);

/* "NOW LOADING..." frame (Game Init FUN_005f95c0): black + loading.png row 0 at
 * (W-120s, H-40s). Loads the texture if needed. */
void sel_draw_loading(void);

/* Tile wipe (select.h sel_wipe) drawn like FUN_005622a0 types 0x11-0x14. */
void sel_wipe_draw(const sel_wipe *w);

/* VS Select pieces reused by the local-wireless select (netsel.c), defined in select_draw.c.
 * The player pieces draw from g_vs (portrait / wheel / standing art of player p, stage cards). */
void selx_textures(void);   /* load the VS Select's textures */
void selx_free(void);       /* free them + the select's 3D stage */
void selx_backdrop(void);   /* sl_bgp + the 3D stage */
void selx_frame(void);      /* letterbox + title */
void selx_stage_select(void); /* stage cards + stage name + BGM band (g_vs.ca0) */
void selx_portrait(int p);
void selx_wheel(int p);
void selx_standing(int p);
void selx_std(int p, int chara, int colopt); /* standing art of p (reloads on change) */
void selx_bottom_bg(void);
void selx_pill(float y, unsigned strip, const char *label, float a, float w);
void selx_stats(int id, float x, float y, float a);
void selx_icon(int id, float cx, float cy, float half, unsigned argb);
void selx_noise(int frame, float cx, float cy, unsigned argb);
void selx_name_plate(int id, float cx, float cy, float hw, float hh, unsigned argb);
void selx_ring(bool p2, float cx, float cy, float a, int ring);
void selx_stage_name(int id, float cx, float cy, float k, unsigned argb);
void selx_arrows(float xl, float xr, float y, unsigned argb);

#endif
