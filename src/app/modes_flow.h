/*
 * Arcade / story flow glue (docs/re/modes.md §0.3, §D; port API in §0.1.1).
 *
 * Owns the run state (src/modes/ladder.h ladder_run), the app-wide cosmetic MT
 * (the PC's MT 0x875b68: random opponents, the "????" BGM pick, win quotes) and the
 * progress save (src/modes/save.h). Turns every ladder step into g_setup (§0.2) +
 * scene_goto(next). Portable (no libctru): host-tested by tests/test_flow_glue.c.
 *
 * Who calls what (each entry point ends with a scene_goto(); call it from update()):
 *
 *   ArcSelect (8) confirm      modes_flow_start(character, scheme)    FUN_0079e4b0 state 900
 *   ArcSelect (8) cancel       (scene's own) scene_goto(SCENE_MENU)
 *   Battle (2) match over      modes_flow_game_exit(MODES_EXIT_DECIDED, &world)
 *   Pause menu Retry / Quit    modes_flow_game_exit(MODES_EXIT_RETRY / MODES_EXIT_QUIT, NULL)
 *   Result (3) leaves          modes_flow_result_exit(gameOver, cont) FUN_00796ca0
 *   Conversation (5) leaves    modes_flow_conv_exit()                 FUN_004e3730
 *   Endroll (11) leaves        modes_flow_endroll_exit()              FUN_00519f60
 *
 * All of them work in every mode (g_setup.mode): arcade/story run the ladder; Match /
 * Training do what the PC does for versus (DECIDED -> Result, Retry -> same match again,
 * Quit -> VS Select; Result exit -> VS Select).
 *
 * Scene Inits read their inputs here:
 *   Result Init      modes_flow_result() (Game Exit sums + winner), modes_flow_lost()
 *                    (DAT_009b68e0 == 0x6e: continue mode), modes_flow_run() for the run
 *   Conversation     modes_flow_conv_script() (FUN_004e35e0 arg), modes_flow_conv_backdrop()
 *                    (FUN_004bfc10 arg); modes_flow_next_scene() after modes_flow_conv_exit()
 *                    (Conversation Exit unloads the stages when it is SCENE_ENDROLL)
 *   ArcSelect        modes_save() for unlocks (charOpen) and clear marks
 *                    (modes_arcade_cleared / modes_story_cleared, score.h)
 *   VS Select        modes_save()->stageOpen / bgmOpen
 *   Menu Init        modes_flow_run()->rank / ->score / ->msgs after an arcade/story run
 *   Battle Init      modes_flow_match_params() in arcade/story (= ladder_match_params)
 *
 * Randomness outside the tick (never aos_world.rng / cpuRng): modes_cosmetic().
 * Save: entry points only mark the save dirty; modes_save_poll() writes it, called by the
 * framework once per frame outside the Battle scene (never during a match frame).
 */
#ifndef AOS_APP_MODES_FLOW_H
#define AOS_APP_MODES_FLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "scene.h"
#include "../modes/ladder.h"
#include "../modes/save.h"
#include "../modes/score.h"
#include "../sim/rng.h"
#include "../sim/sim.h"

/* Game Exit reasons (DAT_00885970) = LADDER_EXIT_*. */
enum { MODES_EXIT_DECIDED = LADDER_EXIT_DECIDED, MODES_EXIT_QUIT = LADDER_EXIT_QUIT, MODES_EXIT_RETRY = LADDER_EXIT_RETRY };

/* Boot: seed the cosmetic MT (3DS: svcGetSystemTick, random_device stand-in) and load the
 * save from save_path (AOS_SAVE_PATH on 3DS; NULL = defaults, no file I/O: host tests).
 * A missing/corrupt file gives the defaults and is written at once (FUN_007c4450). */
void modes_flow_init(uint32_t seed, const char *save_path);

aos_mt *modes_cosmetic(void);   /* app-wide cosmetic MT 0x875b68 */
aos_save *modes_save(void);     /* progress (unlocks, clears, ranking); set ->dirty after changes */
/* Write the save when dirty (and a path was given). Returns 1 if a write happened. */
int modes_save_poll(void);

/* ---------------------------------------------------------------- entry points */
/* ArcSelect confirmed: build the ladder for g_setup.mode (MODE_ARCADE / MODE_STORY) with
 * g_setup.difficulty and step(0). character = engine id (DAT_0087fac4), scheme = hyper
 * control 0 Standard / 1 Advanced (DAT_0087fabc). */
void modes_flow_start(int character, int scheme);
/* Battle scene end (Game Exit FUN_005f5780). w is needed for MODES_EXIT_DECIDED (winner +
 * round records), may be NULL otherwise. */
void modes_flow_game_exit(int how, const aos_world *w);
/* Result scene end (FUN_00796ca0). gameOver = the continue countdown ran out (DAT_009b692d),
 * cont = continue chosen (DAT_009b692e); both 0 after a won fight (and always in versus). */
void modes_flow_result_exit(int gameOver, int cont);
void modes_flow_conv_exit(void);    /* Conversation Exit FUN_004e3730: step(0) */
void modes_flow_endroll_exit(void); /* Endroll Exit FUN_00519f60: clear records, save, step(0) */

/* ---------------------------------------------------------------- state for scenes */
bool modes_flow_active(void);                /* an arcade/story run is in progress */
const ladder_run *modes_flow_run(void);      /* current / last run (valid after it finished) */
const ladder_out *modes_flow_last_out(void); /* last stepper output */
int modes_flow_next_scene(void);             /* scene the last entry point went to */
const modes_result *modes_flow_result(void); /* last DECIDED match (any mode) */
bool modes_flow_lost(void);                  /* last fight lost in arcade/story: Result = continue mode */
int modes_flow_conv_script(void);            /* conversation script id (10k+n) */
int modes_flow_conv_backdrop(void);          /* conversation backdrop stage */
/* Battle Init in arcade/story: the fight's parameters (§D.3.1). */
void modes_flow_match_params(aos_match_params *m);

/* Unlock popups pending for the menu (D.5), consumed by the menu: count, i-th message id
 * (1000+char / 2000+stage / 3000+bgm) and text id (modes_msg_text). */
int modes_flow_msg_count(void);
int32_t modes_flow_msg(int i);
void modes_flow_msgs_clear(void);

#endif
