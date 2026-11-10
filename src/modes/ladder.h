/*
 * Arcade / story ladder sequencer (docs/re/modes.md §0.3, §0.4, §D.1, §D.2.4).
 * Pure C, host-testable, no I/O. One ladder_run holds everything the PC kept in the
 * sequencer globals (DAT_008940a8..0x8940d4). Runs outside the tick: random picks use a
 * cosmetic MT19937 owned by the app (never aos_world.rng / cpuRng).
 *
 * Usage (app / scene layer):
 *   ArcSelect confirmed:   ladder_start(&run, mode, char, difficulty, &cosmetic, &save, &out)
 *   Battle ended:          ladder_game_exit(&run, how, &world, &cosmetic, &save, &out, &result)
 *   Conversation exit:     ladder_step(&run, 0, &cosmetic, &save, &out)
 *   Result exit:           ladder_result_exit(&run, gameOver, cont, &cosmetic, &save, &out)
 *   Endroll exit:          ladder_endroll_exit(&run, &cosmetic, &save, &out)
 * then switch to out.scene. For scene 2 build the fight with ladder_match_params().
 * Write the save file whenever save.dirty is set (the PC writes at those points).
 */
#ifndef AOS_MODES_LADDER_H
#define AOS_MODES_LADDER_H

#include <stdint.h>

#include "../sim/rng.h"
#include "../sim/sim.h"
#include "save.h"
#include "score.h"

enum { LADDER_ARCADE = 0, LADDER_STORY = 1 };                     /* DAT_0087faa8 */
enum { LADDER_FIGHT = 0, LADDER_CONV = 1, LADDER_RESULT = 2, LADDER_ENDING = 3 }; /* entry +0 */
#define LADDER_RANDOM 1 /* entry +8 of a fight: opponent picked from the pool when the fight starts */

/* Scene ids (DAT_00885870, modes.md §0.1). */
enum {
	SCENE_BATTLE = 2, SCENE_RESULT = 3, SCENE_MENU = 4, SCENE_CONVERSATION = 5,
	SCENE_ARCSELECT = 8, SCENE_ENDROLL = 11,
};

/* Game Exit reasons (DAT_00885970). */
enum { LADDER_EXIT_DECIDED = 1, LADDER_EXIT_QUIT = 2, LADDER_EXIT_RETRY = 3 };

typedef struct {
	int32_t type;   /* +0x00 LADDER_FIGHT / CONV / RESULT / ENDING */
	int32_t a;      /* +0x04 fight: P1 char; conversation: script id (resource "TEXT_CONVERSATION%03d") */
	int32_t b;      /* +0x08 fight: opponent (LADDER_RANDOM until picked); conversation: backdrop stage */
	int32_t colP1;  /* +0x0c */
	int32_t colP2;  /* +0x10 */
	int32_t stage;  /* +0x14 */
	int32_t bgm;    /* +0x18 */
} ladder_entry;

#define LADDER_MAX_STEPS 24
#define LADDER_MAX_POOL 12

/* Match-setup globals the steppers write (modes.md §0.2); stay valid between steps. */
typedef struct {
	int32_t p1, p2;         /* DAT_0087fac4 / fac8 */
	int32_t colP1, colP2;   /* DAT_0087facc / fad0 */
	int32_t stage;          /* DAT_0087fadc */
	int32_t bgm;            /* DAT_0087fae0 */
} ladder_setup;

typedef struct {
	int32_t mode;        /* DAT_0087faa8 */
	int32_t difficulty;  /* DAT_0087fab0 (constant for the run, §D.2.1) */
	int32_t player;      /* DAT_008940a8 */
	int32_t index;       /* DAT_008940ac cursor */
	int32_t continues;   /* DAT_008940c8 */
	int32_t score;       /* DAT_008940cc arcade run score */
	int32_t rank;        /* DAT_008940d0: 0..9 ranked, 10 not ranked, 11 story */
	int32_t winner;      /* DAT_009b6930 of the last decided fight (0 P1, 1 P2) */
	int32_t nsteps;
	ladder_entry step[LADDER_MAX_STEPS]; /* vector 0x8940b0 */
	int32_t npool;
	int32_t pool[LADDER_MAX_POOL];       /* vector 0x8940bc */
	modes_msgs msgs;                     /* vector 0x8940d4 (unlock popups for the menu) */
	ladder_setup setup;
} ladder_run;

typedef struct {
	int32_t scene;      /* next scene (SCENE_*) */
	int32_t newMatch;   /* DAT_00885879 (scene 2) */
	int32_t scriptId;   /* scene 5: conversation script id (FUN_004e35e0 argument) */
	int32_t backdrop;   /* scene 5: backdrop stage (FUN_004bfc10 argument) */
	int32_t finished;   /* scene 4 from the end-of-ladder branch or a game over: run over;
	                     * run.rank / run.msgs / save are final */
	int32_t lost;       /* scene 3 entered for a lost fight (Result in continue mode) */
} ladder_out;

/* Builders FUN_004a7cc0 (arcade, chars 3..17) / FUN_007befa0 (story, chars 3..16). Other
 * characters give an empty ladder (the first step then ends the run). */
void ladder_build(ladder_run *r, int mode, int32_t character, int32_t difficulty);
/* ArcSelect Exit FUN_0079e4b0 (state 900): build + step(0). */
void ladder_start(ladder_run *r, int mode, int32_t character, int32_t difficulty, aos_mt *cosmetic, aos_save *save,
                  ladder_out *out);
/* Steppers FUN_004a7910 (arcade) / FUN_007bec90 (story). param 0 advance, 1 match decided
 * (uses r->winner), 3 retry the fight just played counting a continue. */
void ladder_step(ladder_run *r, int param, aos_mt *cosmetic, aos_save *save, ladder_out *out);
/* Game Exit FUN_005f5780 arcade/story part. how = LADDER_EXIT_*. For DECIDED reads the
 * winner and the round records from w (result may be NULL), adds the arcade score when
 * P1 won, then step(1). RETRY -> step(3). QUIT -> scene 8 (run abandoned, nothing saved). */
void ladder_game_exit(ladder_run *r, int how, const aos_world *w, aos_mt *cosmetic, aos_save *save, ladder_out *out,
                      modes_result *result);
/* Result Exit FUN_00796ca0 (arcade/story): gameOver = continue countdown ran out
 * (DAT_009b692d), cont = continue chosen (DAT_009b692e); neither = the fight was won. */
void ladder_result_exit(ladder_run *r, int gameOver, int cont, aos_mt *cosmetic, aos_save *save, ladder_out *out);
/* Endroll Exit FUN_00519f60: clear records (no-continue runs only), save, step(0). */
void ladder_endroll_exit(ladder_run *r, aos_mt *cosmetic, aos_save *save, ladder_out *out);

/* Fight parameters for the current setup (scene 2), §D.3 / §D.3.1: P2 = CPU, rounds 1,
 * timer 180 (999 in story on stage 19), handicap 2, colours from the entry.
 * p1Scheme = hyper-control mode chosen on ArcSelect (DAT_0087fabc). */
void ladder_match_params(const ladder_run *r, int32_t p1Scheme, aos_match_params *m);
int32_t ladder_round_seconds(int mode, int32_t stage); /* Game Init FUN_005f4650 */

/* Character default BGM (stats record +0x2d8, DAT_00882258 + c*0x2dc), used when an entry's
 * BGM is 0 (never in the shipped ladders). Every character registration stores 0 there
 * (16 writers, e.g. `mov [eax+0x882258],0` at 0x4a0dee), so it is always 0. */
int32_t ladder_char_bgm(int32_t c);

#endif
