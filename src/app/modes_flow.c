/* Arcade / story flow glue (modes_flow.h; docs/re/modes.md §0.3, §D). Portable. */
#include "modes_flow.h"

#include <string.h>

static aos_mt cosmetic;      /* MT 0x875b68 */
static aos_save save;
static const char *save_path;
static ladder_run run;
static ladder_out last;
static bool active;
static modes_result result;
static int next_scene;

void modes_flow_init(uint32_t seed, const char *path)
{
	aos_mt_seed(&cosmetic, seed);
	save_path = path;
	memset(&run, 0, sizeof run);
	run.rank = 10;
	memset(&last, 0, sizeof last);
	memset(&result, 0, sizeof result);
	active = false;
	next_scene = SCENE_NONE;
	if (!path) {
		aos_save_defaults(&save);
		save.dirty = 0;
	} else if (!aos_save_load_file(&save, path)) {
		save.dirty = 1; /* FUN_007c4450: missing/corrupt -> defaults written at once */
	} else {
		save.dirty = 0;
	}
}

aos_mt *modes_cosmetic(void) { return &cosmetic; }
aos_save *modes_save(void) { return &save; }

int modes_save_poll(void)
{
	if (!save.dirty || !save_path)
		return 0;
	save.dirty = 0; /* one attempt per change: a failing SD card must not retry every frame */
	aos_save_write_file(&save, strcmp(save_path, AOS_SAVE_PATH) ? NULL : AOS_SAVE_DIR, save_path);
	return 1;
}

bool modes_flow_active(void) { return active; }
const ladder_run *modes_flow_run(void) { return &run; }
const ladder_out *modes_flow_last_out(void) { return &last; }
int modes_flow_next_scene(void) { return next_scene; }
const modes_result *modes_flow_result(void) { return &result; }
bool modes_flow_lost(void) { return active && last.lost; }
int modes_flow_conv_script(void) { return last.scriptId; }
int modes_flow_conv_backdrop(void) { return last.backdrop; }
int modes_flow_msg_count(void) { return run.msgs.n; }
int32_t modes_flow_msg(int i) { return i >= 0 && i < run.msgs.n ? run.msgs.id[i] : 0; }
void modes_flow_msgs_clear(void) { run.msgs.n = 0; }

void modes_flow_match_params(aos_match_params *m) { ladder_match_params(&run, g_setup.command[0], m); }

static void go(int id)
{
	next_scene = id;
	scene_goto(id);
}

/* Stepper output -> setup globals + next scene. Until Parts B/C register Result /
 * Conversation, their steps are passed through (won fight / conversation = step(0),
 * lost fight = game over) so a run stays playable end to end. */
static void apply(const ladder_out *o)
{
	for (int guard = 0; guard < LADDER_MAX_STEPS * 2; guard++) {
		last = *o;
		if (o->finished)
			active = false;
		if (o->scene == LADDER_SCENE_RESULT && !scene_get(SCENE_RESULT)) {
			ladder_out n;
			ladder_result_exit(&run, o->lost, 0, &cosmetic, &save, &n);
			last = n;
			o = &last;
			continue;
		}
		if (o->scene == LADDER_SCENE_CONVERSATION && !scene_get(SCENE_CONVERSATION)) {
			ladder_out n;
			ladder_step(&run, 0, &cosmetic, &save, &n);
			last = n;
			o = &last;
			continue;
		}
		break;
	}
	if (last.finished)
		active = false;
	match_setup *s = &g_setup;
	if (last.scene == LADDER_SCENE_BATTLE) { /* stepper type 0 + Menu Exit case 0/1 values (§0.2, §D.3) */
		s->sub = SUB_CPU;
		s->chara[0] = run.setup.p1;
		s->chara[1] = run.setup.p2;
		s->color[0] = run.setup.colP1;
		s->color[1] = run.setup.colP2;
		s->stage = run.setup.stage;
		s->bgm = run.setup.bgm;
		s->handicap[0] = s->handicap[1] = AOS_HANDICAP_DEFAULT;
		s->input[0] = 0;
		s->input[1] = INPUT_CPU;
		s->command[1] = 0;
		s->rounds = 1;
		s->timer = ladder_round_seconds(run.mode, run.setup.stage);
		s->difficulty = run.difficulty;
		s->new_match = last.newMatch != 0;
	}
	int id = last.scene;
	if (id == LADDER_SCENE_ARCSELECT && !scene_get(SCENE_ARC_SELECT))
		id = SCENE_MENU;
	go(id);
}

void modes_flow_start(int character, int scheme)
{ /* ArcSelect Exit FUN_0079e4b0, state 900 */
	g_setup.chara[0] = character;
	g_setup.command[0] = scheme;
	int mode = g_setup.mode == MODE_STORY ? LADDER_STORY : LADDER_ARCADE;
	ladder_out o;
	active = true;
	memset(&result, 0, sizeof result);
	ladder_start(&run, mode, character, g_setup.difficulty, &cosmetic, &save, &o);
	apply(&o);
}

static bool ladder_mode(void) { return active && (g_setup.mode == MODE_ARCADE || g_setup.mode == MODE_STORY); }

void modes_flow_game_exit(int how, const aos_world *w)
{ /* Game Exit FUN_005f5780 */
	if (how == MODES_EXIT_DECIDED && w)
		modes_match_result(w, &result);
	if (ladder_mode()) {
		ladder_out o;
		if (how == MODES_EXIT_DECIDED && !w)
			how = MODES_EXIT_QUIT; /* port guard */
		ladder_game_exit(&run, how, w, &cosmetic, &save, &o, NULL);
		if (how == MODES_EXIT_QUIT)
			active = false; /* run abandoned, nothing saved */
		apply(&o);
		return;
	}
	/* versus / training */
	if (how == MODES_EXIT_RETRY) {
		g_setup.new_match = true;
		go(SCENE_BATTLE);
	} else if (how == MODES_EXIT_DECIDED) {
		go(SCENE_RESULT);
	} else {
		go(SCENE_VS_SELECT);
	}
}

void modes_flow_result_exit(int gameOver, int cont)
{ /* Result Exit FUN_00796ca0 */
	if (!ladder_mode()) {
		go(SCENE_VS_SELECT);
		return;
	}
	ladder_out o;
	ladder_result_exit(&run, gameOver, cont, &cosmetic, &save, &o);
	apply(&o);
}

void modes_flow_conv_exit(void)
{ /* Conversation Exit FUN_004e3730 */
	if (!ladder_mode()) {
		go(SCENE_MENU);
		return;
	}
	ladder_out o;
	ladder_step(&run, 0, &cosmetic, &save, &o);
	apply(&o);
}

void modes_flow_endroll_exit(void)
{ /* Endroll Exit FUN_00519f60 */
	if (!ladder_mode()) {
		go(SCENE_MENU);
		return;
	}
	ladder_out o;
	ladder_endroll_exit(&run, &cosmetic, &save, &o);
	apply(&o);
}
