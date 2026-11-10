/*
 * Scene machine of the app (docs/re/modes.md §0.1, "port API").
 *
 * Modelled on the original main loop FUN_006325f0: one scene per frame,
 * selected by the current scene id (DAT_00885870). A scene's Init runs when
 * "init pending" (DAT_00885878) is set; leaving a scene = its Exit, which
 * writes the next id and remembers its own id as the previous scene
 * (DAT_00885874, several Inits branch on it). After the scene, every frame,
 * in this order (as the original):
 *     announce/help line update   FUN_00495040   (announce_*)
 *     SE lockouts                 FUN_007bcc00   (audio_se_frame)
 *     BGM manager                 FUN_004beeb0   (music_update)
 *     screen-transition update    FUN_00561c70   (scene_fade_*)
 *
 * Port mapping:
 *   - A scene registers a scene_def for an id (scene_register). The
 *     framework calls init() at the start of the first frame of the scene,
 *     then update() + the draw hooks every frame. A scene leaves by calling
 *     scene_goto(next) from update(): the framework then (after this frame's
 *     draw) calls exit(), sets prev = this id and runs next's init() at the
 *     start of the next frame — exactly the original's one-frame handover.
 *   - Drawing: the framework owns C3D_FrameBegin/End and both screens. It
 *     clears the top screen (clear_top colour) and the bottom screen, calls
 *     draw_top() with the top target bound (C2D_SceneBegin done) and
 *     draw_bottom() with the bottom target bound, then draws the transition
 *     (both screens) and the help line (bottom screen). A scene may bind
 *     other targets itself (render_world / panel_draw do) as long as it
 *     leaves its hook with the same target bound or doesn't care.
 *   - Blocking scenes: a scene with run() set gets the whole loop (legacy
 *     code with its own aptMainLoop: netplay lobby, input lab). The bottom
 *     screen is given back to the text console for its duration
 *     (scene_console()). When run() returns, exit() is called; if run() did
 *     not scene_goto(), the framework returns to the previous scene.
 *   - Everything here is presentation/app state: nothing touches the
 *     simulation except through match_setup -> the Battle scene.
 *
 * This header is portable (no libctru types) so menu logic can be
 * host-tested; 3DS-only helpers are at the end under __3DS__.
 */
#ifndef AOS_APP_SCENE_H
#define AOS_APP_SCENE_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------ scene ids */
/* Values = the original DAT_00885870 ids (modes.md §0.1). */
enum {
	SCENE_NONE = 0,
	SCENE_LOGO = 1,          /* A: daidai / shinden logos */
	SCENE_BATTLE = 2,        /* match (main.c; Game Init/Exit) */
	SCENE_RESULT = 3,        /* B: result + continue */
	SCENE_MENU = 4,          /* A: title, main menu, sub-menus, config */
	SCENE_CONVERSATION = 5,  /* C: story conversation */
	SCENE_VS_SELECT = 6,     /* B: match/training select (until then: port dev select) */
	SCENE_NET_SELECT = 7,    /* port: UDS lobby (blocking, netplay.c) */
	SCENE_ARC_SELECT = 8,    /* B: arcade/story character select */
	SCENE_TRANSIT = 9,       /* unused by the port */
	SCENE_MEMORY = 10,       /* unused (replays) */
	SCENE_ENDROLL = 11,      /* D: ending + staff roll */
	SCENE_QUIT = 12,         /* leave the app (HOME menu) */
	SCENE_ONLINE_SELECT = 13,/* unused (Steam lobbies) */
	/* port-only scenes (no original id) */
	SCENE_LAB = 20,          /* input lab (blocking, lab.c) */
	SCENE_DEV = 21,          /* dev / debug menu (versus setup, stage LOD, ...) */
	SCENE_COMING_SOON = 22,  /* placeholder for unimplemented entries */
	SCENE_MAX = 32
};

typedef struct scene_def {
	const char *name;
	void (*init)(void);        /* Init (DAT_00885878 set): may read scene_prev() */
	void (*update)(void);      /* once per frame, before drawing */
	void (*draw_top)(void);    /* top screen 400x240, target bound */
	void (*draw_bottom)(void); /* bottom screen 320x240, target bound */
	void (*exit)(void);        /* after the frame in which scene_goto() was called */
	void (*run)(void);         /* blocking scene (owns the loop); init/update/draw unused */
	uint32_t clear_top;        /* top clear colour, RGBA8 as C2D_Color32 (0 = black) */
	uint32_t clear_bottom;     /* bottom clear colour (0 = black) */
	/* optional: true when draw_bottom would draw the same pixels as on the previous frame; the frame then skips
	 * the bottom screen (no clear, no draw, no transfer: it keeps showing its last image; PERF.md) */
	bool (*bottom_same)(void);
} scene_def;

void scene_register(int id, const scene_def *def); /* def must stay valid */
const scene_def *scene_get(int id);

/* Request a scene switch (Exit then next Init at the next frame start).
 * The last call in a frame wins. scene_goto(SCENE_QUIT) leaves the app. */
void scene_goto(int next);
int scene_current(void);   /* DAT_00885870 */
int scene_prev(void);      /* DAT_00885874: id of the scene we came from */
uint32_t scene_frame(void);/* frames since the current scene's Init */

/* Main loop: runs scenes from `first` until SCENE_QUIT or the system asks to
 * close (aptMainLoop false). 3DS only. */
void scene_main(int first);

/* ------------------------------------------------------------ transitions */
/* FUN_00561fc0(type) / FUN_00561c70 / FUN_005622a0, same types and rates:
 *  1-4 to black (alpha 0->1), 5-8 from black, 9-12 to white, 13-16 from white;
 *  rate by type&3: 1 -> 0.05 (20 f), 2 -> 0.01, 3 -> 0.005, 0 -> 0.0025.
 * (Tile wipes 0x11-0x14 are only used around matches: hud.c draws them.)
 * The update runs after the scene each frame like the original; when the
 * transition is complete it is switched off one update later. */
void scene_fade(int type);
bool scene_fade_busy(void);   /* DAT_0087e7c0 != 0: a transition is on */
float scene_fade_alpha(void); /* current coverage 0..1 of black/white */

/* ------------------------------------------------------------ help line */
/* FUN_004953f0 / FUN_00495030. id = 0-based index of the ANNOUNCE_* key in
 * script/<lang>/announce.txt (modes.md A.1.1), shown with the menu options
 * {y 280, centre, forever}; types one code point per frame once the band is
 * open (alpha += 0.1/frame). Drawn by the framework on the bottom screen. */
void announce_show(int id);
void announce_show_text(const char *utf8); /* arbitrary text, same behaviour */
void announce_hide(void);
const char *announce_text(void);           /* current full text ("" if none) */

/* ------------------------------------------------------------ menu input */
/* FUN_0061c100 per-button states (modes.md A.2.1). */
enum { MIN_UP = 0, MIN_RELEASED = 1, MIN_HELD = 2, MIN_PRESSED = 3 };
/* Slots, same order as the original state[16]. */
enum {
	MI_RIGHT = 0, MI_LEFT, MI_UP, MI_DOWN,
	MI_OK,        /* 3DS A (OK bind) */
	MI_CANCEL,    /* 3DS B (Cancel bind) */
	MI_ALT_CANCEL,/* bit 0x80: unused on 3DS (A/B already are confirm/cancel) */
	MI_ALT_OK,    /* bit 0x10: START (title/logo "press start") */
	MI_X, MI_Y,   /* 3DS X / Y (0x20 / 0x40 slots) */
	MI_L, MI_R,   /* 3DS L / R (0x100 / 0x200 slots) */
	MI_START,     /* 0x400 pause */
	MI_SELECT,
	MI_TOUCH,     /* stylus down */
	MI_SLOT_COUNT = 16
};
/* Raw button bits of one frame (portable; the 3DS layer fills them). */
enum {
	MB_UP = 1 << 0, MB_DOWN = 1 << 1, MB_LEFT = 1 << 2, MB_RIGHT = 1 << 3,
	MB_A = 1 << 4, MB_B = 1 << 5, MB_X = 1 << 6, MB_Y = 1 << 7,
	MB_L = 1 << 8, MB_R = 1 << 9, MB_START = 1 << 10, MB_SELECT = 1 << 11,
	MB_TOUCH = 1 << 12
};
typedef struct {
	uint8_t st[MI_SLOT_COUNT];   /* with direction repeat (FUN_0061c100) */
	uint8_t nr[MI_SLOT_COUNT];   /* no repeat (FUN_0061bc70) */
	uint16_t held[MI_SLOT_COUNT];/* frames held */
	uint16_t prev;               /* previous raw bits */
	int touch_x, touch_y;        /* last stylus position (bottom screen) */
} menu_input;
/* Feed one frame of raw bits: direction repeat = pressed on the first frame,
 * held 1..19, then pressed every 5th frame from frame 20. */
void minput_feed(menu_input *mi, uint16_t bits);
extern menu_input g_min; /* fed once per frame by the framework */
static inline int min_state(int slot) { return g_min.st[slot]; }
static inline bool min_pressed(int slot) { return g_min.st[slot] == MIN_PRESSED; }
static inline bool min_pressed_nr(int slot) { return g_min.nr[slot] == MIN_PRESSED; }
/* FUN_0061c7b0 / FUN_0061c7e0 */
bool min_confirm(void);
bool min_cancel(void);
/* Swallow this frame's presses (e.g. after a scene switch). */
void min_flush(void);

/* ------------------------------------------------------------ match setup */
/* The setup globals of modes.md §0.2 (written by menus/selects/sequencer,
 * read by the Battle scene's Game Init). */
enum { MODE_ARCADE = 0, MODE_STORY = 1, MODE_MATCH = 2, MODE_TRAINING = 3 };
enum { SUB_CPU = 0, SUB_2P = 1, SUB_ONLINE = 3, SUB_REPLAY = 4 };
#define INPUT_CPU 10
typedef struct {
	int mode;         /* DAT_0087faa8 */
	int sub;          /* DAT_0087faac */
	int difficulty;   /* DAT_0087fab0, 0..2 */
	int input[2];     /* DAT_0087fab4/fab8 (INPUT_CPU = CPU) */
	int command[2];   /* DAT_0087fabc/fac0 hyper control 0 Standard, 1 Advanced */
	int chara[2];     /* DAT_0087fac4/fac8 engine ids 3..18 */
	int color[2];     /* DAT_0087facc/fad0 (3 = auto) */
	int handicap[2];  /* DAT_0087fad4/fad8 0..5 (2 = 9000 HP default, §0.5) */
	int stage;        /* DAT_0087fadc */
	int bgm;          /* DAT_0087fae0 */
	int pad[2];       /* DAT_0087fae8/faec controller ids */
	int rounds;       /* DAT_0087faf0 rounds to win */
	int timer;        /* DAT_00885974 round timer (180, 999 = infinite) */
	int menu_item;    /* DAT_009b1618 main-menu cursor that started this */
	bool new_match;   /* DAT_00885879 */
} match_setup;
extern match_setup g_setup;

/* ------------------------------------------------------------ config */
/* Config values that exist on 3DS (modes_menu_config.tsv); defaults =
 * FUN_004db370. Saved to sdmc:/3ds/aos2_3ds/config.ini by config_save(). */
typedef struct {
	int language;      /* DAT_00881ec0: 0 en (only en fonts are packed; 4 ptbr / 5 sp use them too) */
	int required_wins; /* DAT_00881f60 0..2 (rounds = v + 1) */
	int time_limit;    /* DAT_00881eac 0..3 = 90/180/270/infinite */
	int se_volume;     /* DAT_00881eaa 0..10 (mirrors audio_se_level) */
	int bgm_volume;    /* DAT_00881ea8 0..10 (mirrors music_level) */
	bool hitstun_meter;/* DAT_00881f64 */
	bool high_contrast;/* DAT_00881f65 */
	int p1_color;      /* DAT_00881f68 0..5 */
	int p2_color;      /* DAT_00881f6c 0..5 */
	bool tournament;   /* DAT_00881f70 */
	bool pause_hold;   /* DAT_00881f71 */
	bool pause_on_start; /* DAT_00881f72 */
	bool grey_bg;      /* DAT_00881f74 */
	bool armor_points; /* DAT_00881f75 */
	bool armor_percent;/* DAT_00881f76 */
	bool attack_ind;   /* DAT_00881f77 */
	bool attack_ind_mirror; /* DAT_00881f78 */
	/* port-only (dev) options */
	int city_lod;      /* stage_g.lod (stages 12/14 sprite presentation) */
	int net_delay;     /* UDS input delay 0..4 */
} app_config;
extern app_config g_config;
void config_defaults(app_config *c);
void config_load(void);
void config_save(void);
int config_timer_seconds(int time_limit); /* 90/180/270/999 */

/* ------------------------------------------------------------ hooks for B/C/D */
/* The menu leaves with scene_goto(SCENE_ARC_SELECT / SCENE_VS_SELECT / ...)
 * after filling g_setup like Menu Exit FUN_00642880. Going to an id nobody
 * registered shows SCENE_COMING_SOON ("coming soon", any button returns to
 * scene_prev()). Parts B/C/D add their scenes with scene_register() in
 * scenes_register_all() (main.c); registering an id again replaces it. */

#ifdef __3DS__
/* 3DS-only helpers (scene.c). */
struct C3D_RenderTarget_tag;
struct PrintConsole;
struct C3D_RenderTarget_tag *scene_top(void);    /* top screen target */
struct C3D_RenderTarget_tag *scene_bottom(void); /* bottom screen target (panel.c's) */
struct PrintConsole *scene_console(void);        /* the bottom text console */
/* Give the bottom screen to the console (for code that printf()s) / take it back. */
void scene_console_begin(void);
void scene_console_end(void);
/* Bottom-screen help line band, as the original FUN_00495170 draws it
 * (called by the framework; exposed for blocking scenes). */
void announce_draw_bottom(void);
#endif

#endif
