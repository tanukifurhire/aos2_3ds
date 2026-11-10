/*
 * Story conversation scene, data + logic half (docs/re/modes.md §C).
 * Portable C, no citro2d: host-testable (tests/test_conv.c).
 *
 *   loader      FUN_005d6430 (ACT script parser), FUN_005d7620 + FUN_005d7b60
 *               (story_<char>_0N.txt "<NNNN> text" map, "\n" escapes),
 *               FUN_004e3d20 (text-ref substitution), DAT_009b1054 /
 *               FUN_00456820 (speaker name -> engine id), FUN_004e43a0 (id ->
 *               convMdl portrait), FUN_004e4630 (portraits of a script)
 *   layout      FUN_00593340 -> FUN_0047e2c0 (word wrap into the 540x88·s box,
 *               hanging indent after an opening quote, shrink-to-fit by 0.1
 *               steps) and FUN_0047ca00 (text width)
 *   scene       Init FUN_004e35e0, frame FUN_004e3a20 (typewriter, confirm,
 *               fast-forward), next act FUN_004e5190, Exit FUN_004e3730
 *   draw list   FUN_004e38e0 / FUN_004e5af0 / FUN_004e54b0 / FUN_004e3fc0 as a
 *               plain description (conv_view) for a renderer.
 *
 * Data on the 3DS (built by tools/aos2story, docs/FORMATS.md "Story"):
 *   <root>/res/TEXT_CONVERSATION<id>.txt   the exe resources, verbatim
 *   <root>/<lang>/story_<char>_0<n>.txt     the story text files, verbatim
 *   <root>/portrait/<name>.t3x              portraits, pre-cropped/scaled
 * root = "romfs:/story" on the 3DS; the host test reads extracted/.
 */
#ifndef AOS_MODES_CONV_H
#define AOS_MODES_CONV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ ids */
enum { CONV_SCRIPT_COUNT = 56, CONV_SLOTS = 2, CONV_NAME_COUNT = 19 };

/* Script id of index 0..55 (FUN_004e2600 order: 10..13, 20..23, ..., 140..143). */
int conv_script_id(int index);
/* Script ids are 10*k+n: k = 1..14 story character (Sora .. Hime), n = chapter
 * 0..3. "sora", "alte", ..., "hime" (FUN_004e49c0 table); NULL if invalid. */
const char *conv_story_char(int script_id);
/* "story_<char>_0<n+1>.txt" (FUN_004e49c0). false if the id is invalid. */
bool conv_story_file(int script_id, char *buf, size_t n);
/* "TEXT_CONVERSATION<id, 3 digits>" (FUN_004e4fa0). */
void conv_res_name(int script_id, char *buf, size_t n);
/* Language dirs = DAT_00881ec0 values 0..7 (FUN_00597910; same order as
 * LANG_* in src/app/text.h): en jp chs cht ptbr sp ko ru. */
enum { CONV_LANG_COUNT = 8 };
const char *conv_lang_dir(int lang);

/* Speaker name (Japanese, as in the scripts) -> engine id: 3..16 the story
 * characters, 17 スミカ, 18 黒幕 (Sumika 2nd); anything else 0. */
int conv_name_id(const char *utf8);
/* Engine id -> convMdl texture name (FUN_004e43a0): "sora".."sumikab";
 * 0 and unknown ids -> "none" (512², fully transparent: draw nothing). */
const char *conv_portrait_name(int id);

/* ------------------------------------------------------------- scripts */
/* One ACT record (the original's 0x88-byte struct; offsets in comments). */
typedef struct {
	int name[CONV_SLOTS];   /* +0x00/+0x04 engine id (slot 0 = left/bottom, 1 = right/top) */
	char *text[CONV_SLOTS]; /* +0x08/+0x20 line after substitution, "＄", " " or "" */
	uint8_t band[CONV_SLOTS]; /* +0x38/+0x39 noise band (token != "帯なし") */
	char *se;               /* +0x3c field 2: SE wav name in se.dat, or "なし" */
	char *bg;               /* +0x54 field 0: "なし", "黒地" (dark fill), "白地" */
	char *bgm;              /* +0x6c field 1: always "なし"; a change fades the BGM */
	int field3;             /* +0x84 field 3 (atoi), read by nobody */
} conv_act;

typedef struct {
	conv_act *act;
	int count, cap;
} conv_script;

/* Parse a TEXT_CONVERSATION resource exactly like FUN_005d6430. Returns false
 * only on out-of-memory (malformed input parses like the original). */
bool conv_script_parse(conv_script *s, const char *data, size_t len);
void conv_script_free(conv_script *s);

/* A story text file: "<NNNN> text" lines -> map (FUN_005d7620). */
typedef struct conv_text conv_text;
conv_text *conv_text_parse(const char *data, size_t len); /* NULL on OOM */
void conv_text_free(conv_text *t);
const char *conv_text_get(const conv_text *t, const char *key); /* NULL if absent */
int conv_text_count(const conv_text *t);

/* FUN_004e3d20: every record text starting with '0' is looked up and
 * replaced. Returns the number of substitutions, -1 on OOM. *unresolved
 * (optional) = refs starting with '0' that were not found (kept as "NNNN"). */
int conv_script_resolve(conv_script *s, const conv_text *t, int *unresolved);

/* Load + parse + resolve script `script_id` in language `lang` from root
 * (layout above). false if a file is missing or on OOM. */
bool conv_script_load(conv_script *s, const char *root, int script_id, int lang);

/* Unique name ids used by the script (FUN_004e4630; the portraits the scene
 * loads at Init and frees at Exit), in order of first use, 0 included.
 * Returns the count (<= CONV_NAME_COUNT). */
int conv_script_portraits(const conv_script *s, int *ids, int max);

/* --------------------------------------------------------------- layout */
/* Font metrics the layout needs (BMFont record of FUN_0047b3e0). The text
 * renderer of the app provides them (src/modes/conv_bm.h adapts bm_font). */
typedef struct {
	int16_t yoff, xadv;
	uint16_t h;
} conv_glyph;

typedef struct {
	void *ctx;
	bool (*glyph)(void *ctx, uint32_t cp, conv_glyph *g);    /* false: no glyph */
	int (*kerning)(void *ctx, uint32_t first, uint32_t second); /* 0 if none */
	int base;         /* common.base (record +8) */
	float extra;      /* extra line spacing (record +0xc, 0 for all fonts) */
	bool has_outline; /* record +0x28 */
	int outline;      /* info.outline px (record +0x2c) */
} conv_font;

#define CONV_CP_INVALID 0xffffffffu
/* UTF-8 decode as FUN_00458e20: returns CONV_CP_INVALID for a malformed
 * sequence (then *len = 1). */
uint32_t conv_utf8(const char *s, int avail, int *len);
/* FUN_0047ca00: width of the widest line of s[0..len) at scale sx. */
float conv_text_width(const conv_font *f, const char *s, int len, float sx);
/* FUN_0047e2c0 (param_7 = 0): wrap `text` into box_w x box_h; sx, sy enter
 * as the text scale and leave reduced by 0.1 steps until the wrapped text
 * fits box_h. Returns a malloc'd string (lines split by '\n'), NULL on OOM. */
char *conv_wrap(const conv_font *f, const char *text, int box_w, int box_h, float *sx, float *sy);

/* ---------------------------------------------------------------- scene */
/* Button states, core.md §2.2 encoding (FUN_0061bc70): 0 up, 1 released this
 * frame, 2 held, 3 pressed this frame. */
typedef struct {
	int ok;      /* menu OK            (3DS: A) */
	int cancel;  /* menu cancel        (3DS: B) — only blocks the attack path */
	int attack;  /* Attack/weapon A    (3DS: A) */
	int special; /* Special            (3DS: X, held = fast-forward) */
} conv_input;

/* Things the app must do (returned by conv_init / conv_frame). */
enum {
	CONV_EV_FADE_IN = 1u << 0,  /* FUN_00561fc0(5): fade from black, 20 frames */
	CONV_EV_FADE_OUT = 1u << 1, /* FUN_00561fc0(1): fade to black, 20 frames */
	CONV_EV_BGM_FADE = 1u << 2, /* FUN_004bee00(0,1,1): fade the playing BGM out (100 frames) */
	CONV_EV_SE = 1u << 3,       /* play conv.se (wav name in se.dat) now */
	CONV_EV_EXIT = 1u << 4,     /* Exit FUN_004e3730: free portraits, stepper(0) */
};

typedef struct {
	const conv_font *font;
	float scale;               /* text scale before shrink-to-fit */
	int box_w[CONV_SLOTS];     /* wrap box per slot (original: both (int)(540s)) */
	int box_h[CONV_SLOTS];     /* (original: both (int)(88s)) */
	int W, H;                  /* screen the view is laid out for */
	float s;                   /* min(W/800, H/600) */
} conv_layout;

/* The original layout for a W x H back buffer: s = min(W/800, H/600), box
 * 540s x 88s. scale = configured height / chosen font size (FUN_00594570),
 * e.g. 600/768 for the en 768 font at 800x600. */
void conv_layout_original(conv_layout *l, const conv_font *font, float scale, int W, int H);

/* Draw description of one frame (FUN_004e38e0), screen pixels, y down. */
typedef struct {
	int id;             /* name id -> conv_portrait_name(id) */
	int l, t, r, b;     /* dst rect; l > r = mirrored (slot 0). src = the whole
	                       pre-cropped portrait (PC src rect 0x1b8,0,0x647,0x1ff) */
	uint32_t argb;      /* α<<24 | 0xffffff (speaking) or | 0x808080 */
} conv_portrait_draw;

typedef struct {
	/* noise band FUN_004e3fc0 (if band): `tiles` quads of tile_w px starting at
	 * x 0, from y0 to y1; texture noise<noise> (systemMdl, 512²) addressed in
	 * texels: u 0..tile_w, v v_top at y0 .. v_bottom at y1 (i.e. normalised
	 * u/512, v/512); vertex colour argb; standard alpha blend (mode 1). */
	bool band;
	int tiles, tile_w, noise;
	float y0, y1, v_top, v_bottom;
	uint32_t band_argb;
	/* portraits, in draw order (outgoing then current) */
	int nport;
	conv_portrait_draw port[2];
	/* text (FUN_005931d0 align 0): shown = text[0..text_len) (lines split by
	 * '\n'); x left, y = vertical centre of the first line; scale sx/sy; no
	 * draw when text_len == 0 */
	const char *text;
	int text_len;
	int text_x, text_y;
	float sx, sy;
	uint32_t text_argb;
	bool speaking;
} conv_slot_view;

typedef struct {
	uint32_t clear_argb;        /* 0xff0055aa, then the stage (sequencer entry +8) */
	int nfill;                  /* backdrop fills over the stage, full screen */
	uint32_t fill_argb[2];
	int bar_top, bar_bottom;    /* letterbox (0,0,W,bar_top), (0,bar_bottom,W,H), 0xff000000 */
	conv_slot_view slot[CONV_SLOTS]; /* drawn slot 0 then slot 1 */
} conv_view;

enum { CONV_ST_FADEIN = 10, CONV_ST_TALK = 0x14, CONV_ST_FADEOUT = 0x5a, CONV_ST_DONE = 0x100 };

typedef struct {
	const conv_script *script;
	int script_id;
	conv_layout lay;
	int state;          /* DAT_008940e8 */
	int frame;          /* DAT_008940ec (band noise frame) */
	int f0;             /* DAT_008940f0 (fast-forward cadence, reset per act) */
	int act;            /* DAT_008940f4 = acts started (current act = act-1) */
	bool init_pending;  /* DAT_00885878 */
	char *full[CONV_SLOTS]; /* DAT_008941a0: laid-out text */
	int len[CONV_SLOTS];    /* DAT_008941d0 bytes */
	int n[CONV_SLOTS];      /* DAT_00894248 shown bytes (typewriter) */
	float sx[CONV_SLOTS], sy[CONV_SLOTS]; /* text object scale after wrap */
	bool speak[CONV_SLOTS]; /* DAT_00894262 */
	uint8_t band[CONV_SLOTS]; /* DAT_00894260 */
	int name[CONV_SLOTS];   /* DAT_00894250 */
	float fade;             /* DAT_00894264 act cross-fade 0..1 */
	const char *se;         /* SE of the last CONV_EV_SE */
	bool oom;
	conv_view view;
} conv_scene;

/* Init FUN_004e35e0 (the script must outlive the scene). Returns events
 * (CONV_EV_FADE_IN). Act 0 is set up by the first conv_frame. */
unsigned conv_init(conv_scene *c, const conv_script *s, int script_id, const conv_layout *lay);
/* One frame of FUN_004e3a20. transition_busy = the screen transition started
 * by FADE_IN / FADE_OUT is still running (DAT_0087e7c0). Returns events; when
 * CONV_EV_EXIT is set the scene is over (no view this frame), otherwise
 * c->view describes this frame. */
unsigned conv_frame(conv_scene *c, const conv_input *in, bool transition_busy);
void conv_free(conv_scene *c);

#endif
