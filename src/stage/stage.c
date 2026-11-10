/* Stage dispatch (FUN_004bf480 tables) — docs/re/stages.md §1-2. */
#include "stage.h"

#include <stddef.h>

stage_env stage_g = {.gameMode = 2, .zoom = 1.0f, .aspect = 800.0f / 600.0f, .lod = STAGE_LOD_CAP};

extern const stage_def stage_sl, stage_tr, stage_bf, stage_gr, stage_dr, stage_ri, stage_se_def,
	stage_br, stage_ct, stage_iw, stage_ct2, stage_ft, stage_gz, stage_hl, stage_ml, stage_sh;

/* Index = original stage id (DAT_0087fadc). Entry 3 is empty in the original too. */
const stage_def *const stage_defs[STAGE_COUNT] = {
	[4] = &stage_sl, [5] = &stage_tr, [6] = &stage_bf, [7] = &stage_gr, [8] = &stage_dr,
	[9] = &stage_ri, [10] = &stage_se_def, [11] = &stage_br, [12] = &stage_ct, [13] = &stage_iw,
	[14] = &stage_ct2, [15] = &stage_ft, [16] = &stage_gz, [17] = &stage_hl, [18] = &stage_ml,
	[19] = &stage_sh,
};

/* FUN_0079acc0 pushes into the stage vector 0x9b8dc8 in this order. */
const int stage_vs_list[] = {5, 13, 12, 11, 9, 10, 8, 7, 6, STAGE_RANDOM, 15, 16, 17, 18, 19};
const int stage_vs_count = (int)(sizeof stage_vs_list / sizeof stage_vs_list[0]);

const char *const stage_lod_names[STAGE_LOD_COUNT] = {"Exact", "Smoke cap", "Half-rate smoke", "Cap + half-rate"};

static int cur = 0;

float stage_rand(float lo, float hi) { return aos_rand_f(&stage_g.rng, lo, hi); }
int32_t stage_randi(int32_t lo, int32_t hi) { return aos_rand_i(&stage_g.rng, lo, hi); }
void stage_se(int id)
{
	if (stage_g.se)
		stage_g.se(id);
}

/* FUN_007aaf90: copy the list without entry 1, then
 * idx = rand_int(0, n*10) % n (the "n*10" is computed on the full list). */
int stage_random(void)
{
	int ids[STAGE_COUNT], n = 0;
	for (int i = 0; i < stage_vs_count; i++)
		if (stage_vs_list[i] != STAGE_RANDOM)
			ids[n++] = stage_vs_list[i];
	int r = stage_randi(0, n * 10);
	return ids[r % n];
}

const char *stage_name(int id)
{
	if (id == STAGE_RANDOM)
		return "Random";
	return id > 0 && id < STAGE_COUNT && stage_defs[id] ? stage_defs[id]->name : "?";
}

int stage_current(void) { return cur; }

void stage_begin(int id)
{
	if (id == STAGE_RANDOM)
		id = stage_random();
	if (id <= 0 || id >= STAGE_COUNT || !stage_defs[id])
		id = 6;
	if (cur)
		stage_end();
	sg_frame_begin();
	const stage_def *d = stage_defs[id];
	if (d->load)
		d->load();
	if (d->init)
		d->init();
	cur = id;
}

void stage_end(void)
{
	if (!cur)
		return;
	const stage_def *d = stage_defs[cur];
	if (d->exit)
		d->exit();
	if (d->unload)
		d->unload();
	sg_free_all();
	cur = 0;
}

void stage_update(void)
{
	if (cur && stage_defs[cur]->update)
		stage_defs[cur]->update();
}

void stage_draw(void)
{
	sg_frame_begin();
	if (cur && stage_defs[cur]->draw)
		stage_defs[cur]->draw(1);
}

/* Game Init runs FUN_004bfc10(stage) every round; the stage is already in the
 * BG_STAGE_MODEL vector, so the re-init entry (0x87a8c8) runs. */
void stage_round(void)
{
	if (cur && stage_defs[cur]->reinit)
		stage_defs[cur]->reinit();
}
