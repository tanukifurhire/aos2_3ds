/* Whole-frame draw sequence (FUN_005f91d0 gameplay layer order, objects.md §1.4)
 * and shared draw helpers. Used by the 3DS renderer and by the host smoke test
 * so every draw function is exercised in tests. */
#include "draw.h"
#include "fx.h"
#include "sim.h"

/* 2D colour table @0x87e138 (objects.md §5.2). */
static const uint8_t colour_table[14][3] = {
	{0x80, 0x90, 0xFF}, {0xFF, 0x90, 0x80}, {0x80, 0xFF, 0x80}, {0xFF, 0xFF, 0x90}, {0xE1, 0x80, 0xFF},
	{0xFF, 0x80, 0xE1}, {0x90, 0xE1, 0xFF}, {0xFF, 0xE1, 0x80}, {0x80, 0xFF, 0xE1}, {0xE1, 0xFF, 0x80},
	{0x80, 0x80, 0xFF}, {0xFF, 0xA4, 0xA4}, {0x20, 0x20, 0x20}, {0xFF, 0xFF, 0xFF},
};

/* stats[id] +0x0c..+0x2c: 3 palettes × 3 colour-table indices (core.md §7; "–" = 0). */
static const uint8_t palette_slots[19][3][3] = {
	[3] = {{9, 8, 0}, {6, 4, 0}, {9, 8, 0}},     [4] = {{5, 5, 0}, {4, 6, 0}, {0, 0, 0}},
	[5] = {{10, 8, 6}, {3, 7, 11}, {0, 0, 0}},   [6] = {{6, 4, 11}, {3, 2, 7}, {0, 0, 0}},
	[7] = {{5, 8, 6}, {3, 7, 3}, {0, 0, 0}},     [8] = {{6, 8, 10}, {3, 7, 11}, {0, 0, 0}},
	[9] = {{1, 0, 1}, {1, 0, 0}, {1, 0, 1}},     [10] = {{0, 5, 0}, {2, 1, 0}, {0, 5, 0}},
	[11] = {{3, 6, 0}, {1, 5, 0}, {3, 6, 0}},    [12] = {{2, 6, 0}, {0, 0, 0}, {0, 0, 0}},
	[13] = {{5, 11, 0}, {1, 7, 0}, {5, 11, 0}},  [14] = {{11, 5, 0}, {4, 10, 0}, {11, 5, 0}},
	[15] = {{0, 0, 0}, {5, 0, 0}, {0, 0, 0}},    [16] = {{0, 11, 5}, {2, 3, 7}, {0, 11, 5}},
	[17] = {{0, 11, 5}, {2, 3, 7}, {0, 11, 5}},  [18] = {{0, 11, 5}, {2, 3, 7}, {0, 0, 0}},
};

int aos_team_colour(const aos_world *w, int team, int k) /* DAT_008859f4 + team*0x10 + 4k (FUN_005f5780) */
{
	if (team < 1 || team > 2 || k < 0 || k > 2)
		return 0;
	const aos_player *p = &w->p[team - 1];
	int pal = p->color < 0 ? 0 : p->color > 2 ? 2 : p->color;
	if (p->id < 0 || p->id >= 19)
		return 0;
	return palette_slots[p->id][pal][k];
}

uint32_t aos_colour_rgba(int idx, float alpha)
{
	if (idx < 0 || idx >= 14)
		idx = 13;
	return aos_rgba(colour_table[idx][0] / 255.0f, colour_table[idx][1] / 255.0f, colour_table[idx][2] / 255.0f, alpha);
}

/* Line primitive (FUN_00457ae0) as a thin quad on the common white texture. */
void aos_draw_line(float x0, float y0, float x1, float y1, float width, uint32_t rgba, int blend)
{
	float dx = x1 - x0, dy = y1 - y0, len = aos_sqrt(dx * dx + dy * dy);
	if (len <= 0.0f)
		return;
	aos_dcmd *c = aos_draw(aos_tex("$white"), AOS_BANK_COMMON, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0);
	c->halfW = len * 0.5f;
	c->halfH = width * 0.5f;
	c->rotZ = aos_rad2bam(aos_atan2(dy, dx));
	c->rgba = rgba;
	c->blend = (uint8_t)blend;
}

void aos_draw_world(aos_world *w)
{
	aos_objects *o = &w->obj;
	aos_dark_draw(w);       /* screen dim and speedlines under the gameplay layer (UNVERIFIED placement) */
	aos_speedlines_draw(w);
	for (int i = 0; i < o->neffect; i++)
		if (o->effect[i].drawLayer == 0 && o->effect[i].age >= 0 && aos_otab.effectDraw[o->effect[i].type])
			aos_otab.effectDraw[o->effect[i].type](w, &o->effect[i]);
	for (int i = 0; i < o->nwhip; i++)
		if (aos_otab.whipDraw[o->whip[i].type])
			aos_otab.whipDraw[o->whip[i].type](w, &o->whip[i]);
	for (int layer = 0; layer < 2; layer++)
		for (int i = 0; i < o->nenemy; i++)
			if (o->enemy[i].drawLayer == layer && aos_otab.enemyDraw[o->enemy[i].type])
				aos_otab.enemyDraw[o->enemy[i].type](w, &o->enemy[i]);
	aos_afterimages_draw(w); /* Blur list FUN_004d7bc0, before players */
	aos_draw_players(w);
	for (int i = 0; i < o->nenemy; i++)
		if (o->enemy[i].drawLayer == 2 && aos_otab.enemyDraw[o->enemy[i].type])
			aos_otab.enemyDraw[o->enemy[i].type](w, &o->enemy[i]);
	for (int i = 0; i < o->nmissile; i++)
		if (aos_otab.missileDraw[o->missile[i].type])
			aos_otab.missileDraw[o->missile[i].type](w, &o->missile[i]);
	aos_particles_draw(w); /* FUN_0068c000 after missiles */
	for (int i = 0; i < o->nlaser; i++)
		if (o->laser[i].age >= 0 && aos_otab.laserDraw[o->laser[i].type])
			aos_otab.laserDraw[o->laser[i].type](w, &o->laser[i]);
	for (int i = 0; i < o->nbeam; i++)
		if (aos_otab.beamDraw[o->beam[i].type])
			aos_otab.beamDraw[o->beam[i].type](w, &o->beam[i]);
	for (int i = 0; i < o->neffect; i++)
		if (o->effect[i].drawLayer == 1 && o->effect[i].age >= 0 && aos_otab.effectDraw[o->effect[i].type])
			aos_otab.effectDraw[o->effect[i].type](w, &o->effect[i]);
	aos_sparks_draw(w);  /* FUN_007bd350 */
	aos_blur_draw(w);    /* FUN_004d7900 */
	aos_popups_draw(w);  /* FUN_0065e050 */
	aos_cutin_draw(w);
	if (aos_fx_draw_hook)
		aos_fx_draw_hook(w);
}

void (*aos_fx_draw_hook)(aos_world *w);
