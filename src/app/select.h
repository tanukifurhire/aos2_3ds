/*
 * Part B scenes, portable logic (docs/re/modes.md §B; host-tested by tests/test_select.c):
 *   ArcSelect (scene 8)  FUN_0079df00 / FUN_0079e590 / FUN_0079e4b0
 *   VS Select (scene 6)  FUN_0079acc0 / FUN_0079c110 / FUN_007a2a20 / FUN_0079bfc0
 *   Result + continue (scene 3)  FUN_00796030 / FUN_00796ef0 / FUN_00796ca0, rank FUN_00617740,
 *                        win quote FUN_00617110 (called from Game Exit: result_capture)
 *   Pause menu in a match  FUN_0068ce30 (open) / FUN_0068f4a0 (menu), offline branch
 * Drawing is in select_draw.c / result_draw.c (3DS). The scenes leave through the flow
 * glue (modes_flow.h): ArcSelect -> modes_flow_start, Battle -> modes_flow_game_exit,
 * Result -> modes_flow_result_exit; VS Select fills g_setup and goes to the Battle.
 */
#ifndef AOS_APP_SELECT_H
#define AOS_APP_SELECT_H

#include <stdbool.h>
#include <stdint.h>

#include "../modes/save.h"
#include "../modes/score.h"
#include "../sim/sim.h"

/* ---------------------------------------------------------------- data */
#define SEL_POOL_MAX 17
#define SEL_RANDOM 1 /* VS pool entry "???" (resolved at the VS screen, FUN_007ab0c0) */

/* ArcSelect Init: SB, Nath, Sham, Mira, Tsih, Alte, Sora, Suguri, Saki, Iru, Nanako, Kae,
 * Kyoko, Hime (+ Sumika in arcade), each gated by its unlock flag. */
int sel_pool_arcade(int mode, const aos_save *sv, int out[SEL_POOL_MAX]);
/* VS Select Init: the same order + Sumika if unlocked + Random (1) last. */
int sel_pool_vs(const aos_save *sv, int out[SEL_POOL_MAX]);
/* VS stage list (stages.md §2.2): 5,13,12,11,9,10,8,7,6, 1 (random), 15..19, gated. */
int sel_stage_list(const aos_save *sv, int out[16]);
int sel_index_of(const int *list, int n, int id); /* -1 if absent */

typedef struct {
	int diff;                    /* 0 Easy, 1 Medium, 2 Hard (text 0x1eb + diff) */
	int rank[4];                 /* melee, projectile, mobility, meter gain: 0..4 = D..S */
} sel_stats;
sel_stats sel_char_stats(int id); /* FUN_007a49d0 switch (modes_select_charstats.tsv) */
bool sel_has_sp(int id);          /* chars with an SP colour (FUN_007a2880) */
int sel_color_resolve(int id, int opt); /* FUN_007a2880: option 0 Auto/1 P1/2 P2/3 SP -> palette 0..2 */
const char *sel_char_key(int id); /* "SORA", "STARBREAKER", ... (CHARACTER_* / WIN_* keys) */
int sel_sp_text(int id);          /* text id of the SP colour name (0x1e2..0x1e9) */

/* ---------------------------------------------------------------- tile wipes */
/* FUN_00561fc0 types 0x11 (close, +0.02/f) / 0x13 (open, -0.025/f); tile (c,r) waits c+r frames. */
typedef struct {
	int type; /* 0 off */
	bool done;
	float a[15][20];
	int delay[15][20];
} sel_wipe;
void sel_wipe_start(sel_wipe *w, int type);
void sel_wipe_update(sel_wipe *w);

/* ---------------------------------------------------------------- ArcSelect (8) */
enum { ARC_COMMAND = 100, ARC_CHAR = 200, ARC_CONFIRMED = 900, ARC_CANCELLED = 0x3b6 };
typedef struct {
	int sub, t_sub, frame;   /* 6a30, 6a34, 6a38 */
	int pool[SEL_POOL_MAX], n;
	int cursor;              /* 6ac8 (kept between visits unless coming from the menu) */
	int cmd;                 /* 6ae8: 0 Standard, 1 Advanced */
	float cb8, cd0, cc0, ca8, d74;
	int ring;                /* 9988: ring rotation, degrees*1 (+2/frame, wraps at 3600) */
	int leave;               /* port: frames since the exit transition finished (loading frame) */
} arcsel_state;
extern arcsel_state g_arc;
void arcsel_init(void);
void arcsel_update(void);
static inline int arcsel_char(void) { return g_arc.n ? g_arc.pool[g_arc.cursor] : 10; }

/* ---------------------------------------------------------------- VS Select (6) */
enum { VS_CHARS = 100, VS_STAGE = 200, VS_WHITE = 300, VS_VS = 0x136, VS_CONFIRMED = 900, VS_CANCELLED = 0x3b6 };
enum { SLOT_CPU = -1, SLOT_CHOOSE = 0, SLOT_OPTIONS = 1, SLOT_READY = 2 };
enum { ROW_DELAY = 0, ROW_HANDICAP = 1, ROW_COLOR = 2, ROW_INPUT = 3, ROW_ACCEPT = 4 };
typedef struct {
	int sub, t_sub, frame;
	int pool[SEL_POOL_MAX], n;
	int cursor[2];           /* 6ac8 / 6acc */
	int row[2];              /* 6ad0 / 6ad4 */
	int slot[2];             /* 8c40 slot state */
	int ctrl[2];             /* 8c48: slot driven by controller i (-1 none) */
	int handicap[2];         /* 8c58 0..5 (2 = 100%) */
	int color[2];            /* 8c60 0 Auto, 1 P1, 2 P2, 3 SP */
	int command[2];          /* 8c68 hyper control */
	bool en_hcp[2], en_col[2], en_cmd[2]; /* 8c82 / 8c84 / 8c86 */
	int stages[16], nstages, stage_cur; /* 8dc8 list, 6ad8 cursor */
	int stage;               /* resolved stage (fadc) after the confirm */
	int chara[2];            /* fac4 / fac8 (Random resolved at the VS screen) */
	float cd0[2], cd8[2], cc8[2], cc0[2], ce0[2], d74[2];
	float ca8, ca0, d80, d7c, cbc, ca4;
	int ring[2];
	int leave;
	bool stage_swapped;      /* port: 3D stage replaced (state 300 end) */
} vssel_state;
extern vssel_state g_vs;
void vssel_init(void);
void vssel_update(void);

/* ---------------------------------------------------------------- Result (3) */
enum { RES_WON = 100, RES_LOST = 0x6e, RES_CONTINUE = 0x122, RES_LEAVING = 900 };
typedef struct {
	/* Game Exit facts (result_capture) */
	int winner;              /* 9b6930: 0 P1, 1 P2 */
	int chara[2], color[2], input[2];
	int rounds;              /* DAT_00885a70 + 1 */
	modes_result r;          /* 9b6948.. sums */
	int rank;                /* 9b6960 0..4 = S..D */
	int quote;               /* 0..2 -> _01.._03, 10..12 -> X01..X03, -1 none */
	bool captured;
	/* scene */
	int sub, t, e4;          /* 68e0, 68e8, 68e4 */
	int count;               /* 6988 continue countdown */
	bool game_over, cont;    /* 692d, 692e */
	int slide;               /* 6964 50 -> 0 */
	float band, scroll, row[4], rank_a, cont_a; /* 6978, 697c, 6968.., 6980, 6984 */
	int leave;
	sel_wipe wipe;
} result_state;
extern result_state g_res;
/* Game Exit FUN_005f5780 (DAT_00885970 == 1): winner, sums, rank, win quote (cosmetic RNG). Call
 * right before modes_flow_game_exit(MODES_EXIT_DECIDED, w). */
void result_capture(const aos_world *w); /* reads g_setup.input for the CPU tag */
int result_rank(const modes_result *r, int rounds); /* FUN_00617740 */
int result_quote_pick(int winnerChar, int loserChar, int roll); /* FUN_00617110 with roll = rand(0,100) */
const char *result_quote_key(int chara, int quote, char *buf, int n); /* "WIN_SORA_X01" */
void result_init(void);
void result_update(void);
/* Win-quote letters shown (FUN_0079a7d0 alphas: letter i = clamp(0.1*(t-60) - 0.2*i)). */
float result_letter_alpha(int i);

/* ---------------------------------------------------------------- pause menu */
enum { PAUSE_OFF = 0, PAUSE_ON = 1, PAUSE_RETRY = 3, PAUSE_QUIT = 2 };
typedef struct {
	bool on;                 /* 94330 paused */
	int st, t, cur;          /* 94338 (10 open, 100 menu, 200 command list, 900 closing), 9433c, 94340 */
	float a, a_row, a_cmd;   /* 94344, 94348, 9434c */
	int count;               /* 94370 resume countdown 91..0 */
	int exiting;             /* PAUSE_RETRY / PAUSE_QUIT chosen (DAT_00885970) */
	bool fading;             /* waiting for the fade to black */
} pause_state;
extern pause_state g_pause;
void pause_reset(void);
/* Once per battle frame before the tick. Returns PAUSE_OFF (tick normally), PAUSE_ON (frozen),
 * or PAUSE_RETRY / PAUSE_QUIT once the exit fade has finished (then call Game Exit). */
int pause_update(const aos_world *w);

/* ---------------------------------------------------------------- 3DS side (select_draw.c, result_draw.c) */
void select_scenes_register(void); /* scenes 8, 6, 3 */
void pause_draw_top(void);         /* Battle scene overlay (top screen) */
void pause_draw_bottom(int chara); /* command list of chara (bottom screen) */

#endif
