/* Round / match flow: round-start banner (FUN_006179d0, FUN_00617a60),
 * round-end banner and next-round / match-end decision (FUN_0060b310,
 * FUN_00608a00), screen transitions (FUN_00561fc0, FUN_00561c70) and the
 * round part of Game Init (FUN_005f4650). Spec: docs/re/hud.md §5.
 *
 * Everything here is deterministic and lives in aos_world (rollback-safe).
 * Float constants are the binary's single-precision values. */
#include "sim.h"

#include <string.h>

#define F_ONE 1.0f     /* DAT_00853b34 */
#define F_002 0.02f    /* DAT_00813ed8 */
#define F_005 0.05f    /* DAT_00813ef4 */
#define F_01 0.1f      /* DAT_00853b10 */
#define F_00025 0.0025f /* DAT_008145e0 */
#define F_HALF 0.5f    /* DAT_00853b1c */

static const int end_count[AOS_END_SETS] = {7, 7, 11, 12};

bool aos_match_decided(const aos_world *w) /* FUN_00619c60 */
{
	for (int i = 0; i < 2; i++)
		if (w->p[i].roundsWon >= w->roundsToWin || w->matchFinished)
			return true;
	return false;
}

int aos_match_winner(const aos_world *w)
{
	for (int i = 0; i < 2; i++)
		if (w->p[i].roundsWon >= w->roundsToWin)
			return i;
	return -1;
}

void aos_screen_fade(aos_world *w, int type) /* FUN_00561fc0 */
{
	aos_flow *f = &w->flow;
	f->fadeType = type;
	f->fadeOn = 1;
	f->fadeA = 0;
	f->fadeDone = 0;
	switch (type) {
	case 1: case 9: case 5: case 0xd: f->fadeRate = 0.05f; break;   /* DAT_00813ef4 */
	case 2: case 10: case 6: case 0xe: f->fadeRate = 0.01f; break;  /* DAT_00813ec8 */
	case 3: case 0xb: case 7: case 0xf: f->fadeRate = 0.005f; break; /* DAT_00813eac */
	case 4: case 0xc: case 8: case 0x10: f->fadeRate = F_00025; break;
	case 0x11: case 0x12:
	case 0x13: case 0x14:
		for (int c = 0; c < 20; c++)
			for (int r = 0; r < 15; r++) {
				f->tileA[r][c] = type >= 0x13 ? F_ONE : 0.0f;
				f->tileDelay[r][c] = c + r;
			}
		f->fadeRate = type == 0x11 ? F_002 : type == 0x13 ? 0.025f /* DAT_00813edc */ : 0.01f;
		break;
	default: break;
	}
}

static void fade_update(aos_world *w) /* FUN_00561c70 */
{
	aos_flow *f = &w->flow;
	if (!f->fadeOn)
		return;
	if (f->fadeDone == 1) {
		f->fadeOn = 0;
		return;
	}
	if (f->fadeType >= 1 && f->fadeType <= 0x10) {
		f->fadeA += f->fadeRate;
		if (F_ONE <= f->fadeA) {
			f->fadeA = F_ONE;
			f->fadeDone = 1;
		}
	} else if (f->fadeType == 0x11 || f->fadeType == 0x12) {
		int done = 1;
		for (int c = 0; c < 20; c++)
			for (int r = 0; r < 15; r++) {
				if (f->tileDelay[r][c] < 0) {
					if (f->tileA[r][c] < F_ONE) {
						f->tileA[r][c] += f->fadeRate;
						if (F_ONE <= f->tileA[r][c])
							f->tileA[r][c] = F_ONE;
						done = 0;
					}
				} else {
					f->tileDelay[r][c]--;
					done = 0;
				}
			}
		f->fadeDone = done;
	} else if (f->fadeType == 0x13 || f->fadeType == 0x14) {
		int done = 1;
		for (int c = 0; c < 20; c++)
			for (int r = 0; r < 15; r++) {
				if (f->tileDelay[r][c] < 0) {
					if (0.0f < f->tileA[r][c]) {
						f->tileA[r][c] -= f->fadeRate;
						if (f->tileA[r][c] <= 0.0f)
							f->tileA[r][c] = 0;
						done = 0;
					}
				} else {
					f->tileDelay[r][c]--;
					done = 0;
				}
			}
		f->fadeDone = done;
	}
	/* Port: the PC leaves the battle scene for the result screen once the
	 * match-end wipe has covered the screen. */
	if (f->fadeType == 0x12 && f->fadeDone)
		f->matchEnd = 1;
}

static void start_banner(aos_world *w, int round) /* FUN_006179d0 */
{
	aos_flow *f = &w->flow;
	w->roundStartBanner = 1;
	f->stT = 0;
	f->stRound = round;
	f->stAlpha = 0;
	f->stText = 0;
	f->stNoise = F_ONE;
	for (int i = 0; i < 9; i++) {
		f->stLetter[i] = 0;
		f->stDelay[i] = i * 2 + 0x96;
	}
}

void aos_round_end_banner(aos_world *w, int kind) /* FUN_0060b310 */
{
	aos_flow *f = &w->flow;
	w->roundEndBanner = 1;
	f->enT = 0;
	f->enAlpha = 0;
	f->enType = kind;
	/* DAT_0087fb60 / fb68 are zeroed too (never read). fb64 is not reset here. */
	static const int step[AOS_END_SETS] = {3, 2, 2, 2};
	for (int s = 0; s < AOS_END_SETS; s++)
		for (int i = 0; i < 12; i++) {
			f->enLetter[s][i] = 0;
			f->enDelay[s][i] = s == kind && i < end_count[s] ? i * step[s] + 0x32 : 9999;
		}
	/* Steam achievement checks (FUN_007ca890) and the netplay tail are not ported. */
}

/* Shared letter animation of FUN_00617a60 / FUN_00608a00. */
static void letters(float *a, int32_t *delay, int n, int t)
{
	for (int i = 0; i < n; i++) {
		delay[i]--;
		if (t < 0xc9) {
			if (delay[i] < 0) {
				a[i] += F_01;
				if (F_ONE < a[i])
					a[i] = F_ONE;
			}
		} else {
			a[i] -= F_01;
			if (a[i] <= 0.0f && a[i] != 0.0f)
				a[i] = 0;
		}
	}
}

static void start_banner_tick(aos_world *w) /* FUN_00617a60 */
{
	aos_flow *f = &w->flow;
	if (!w->roundStartBanner)
		return;
	if (f->stT < 200) {
		f->stAlpha += F_002;
		if (F_ONE < f->stAlpha)
			f->stAlpha = F_ONE;
	} else {
		f->stAlpha -= F_01;
		if (f->stAlpha <= 0.0f) {
			w->roundStartBanner = 0;
			return;
		}
	}
	if (f->stT < 0x33 || 0x95 < f->stT) {
		if (0x95 < f->stT) {
			f->stText -= F_01;
			if (f->stText < 0.0f)
				f->stText = 0;
		}
	} else {
		f->stText += F_005;
		if (F_ONE < f->stText)
			f->stText = F_ONE;
	}
	letters(f->stLetter, f->stDelay, 9, f->stT);
	if (F_HALF < f->stNoise)
		f->stNoise -= F_00025;
	f->stT++;
}

static void round_record(const aos_world *w, int i, aos_round_rec *r) /* FUN_0061ac80 */
{
	int winner = aos_hp_winner(w);
	int32_t bonus = 0;
	if (i == winner) {
		float t = (float)(180 - w->frame / 60) / 120.0f; /* DAT_00814174; single precision (0x61ad3a) */
		t = t * 10000.0f;                                /* _DAT_00853ba0 (mulss) */
		bonus = (int32_t)t;                              /* cvttss2si */
		if (10000 < bonus)
			bonus = 10000;
		if (bonus < 0)
			bonus = 0;
	}
	r->timeBonus = bonus;
	r->seconds = w->frame / 60;
	r->hp = w->p[i].hp;
	r->comboDmg = w->p[i].comboBestDmg;
	r->comboHits = w->p[i].comboBestHits;
	r->score = r->hp + r->comboDmg + bonus;
}

static void round_records(aos_world *w) /* FUN_00619cb0: one record per player into 0x885a74 + side*0xc */
{
	if (w->nRoundRecs >= AOS_MAX_ROUND_RECS) /* port: fixed capacity (unreachable with roundsToWin <= 3) */
		return;
	for (int i = 0; i < 2; i++)
		round_record(w, i, &w->roundRec[w->nRoundRecs][i]);
	w->nRoundRecs++;
}

static void end_banner_tick(aos_world *w) /* FUN_00608a00 */
{
	aos_flow *f = &w->flow;
	if (!w->roundEndBanner)
		return;
	if (f->enT < 200) {
		f->enAlpha += F_002;
		if (F_ONE < f->enAlpha)
			f->enAlpha = F_ONE;
	} else {
		f->enAlpha -= F_005;
		if (f->enAlpha < 0.0f)
			f->enAlpha = 0;
		if (0.0f < f->enAlpha || f->enT < 0x12d || f->enNext) {
			if (0x15d < f->enT && f->enNext) {
				f->nextRound = 1; /* replay bookkeeping (FUN_005f88f0) not ported */
				round_records(w); /* FUN_00619cb0 */
			}
		} else {
			if (aos_match_decided(w)) {
				/* FUN_00619cb0 (results), FUN_00561fc0(0x12), FUN_004bee00 (BGM stop: app side) */
				round_records(w);
				aos_screen_fade(w, 0x12);
				w->roundEndBanner = 0;
				return;
			}
			aos_screen_fade(w, 2);
			f->enNext = 1;
		}
	}
	for (int s = 0; s < AOS_END_SETS; s++)
		letters(f->enLetter[s], f->enDelay[s], end_count[s], f->enT);
	f->enT++;
}

void aos_flow_tick(aos_world *w)
{
	start_banner_tick(w);
	end_banner_tick(w);
}

void aos_flow_round_init(aos_world *w) /* parts of FUN_005f4650 */
{
	aos_flow *f = &w->flow;
	if (f->firstInit)
		f->roundIndex = 0;
	else
		f->roundIndex++;
	/* FUN_006089c0 */
	w->roundEndBanner = 0;
	f->enT = 0;
	f->enNext = 0;
	f->enAlpha = 0;
	if (w->roundsToWin == 1)
		start_banner(w, 0);
	else if (!f->firstInit && w->p[0].roundsWon == w->roundsToWin - 1 && w->p[1].roundsWon == w->roundsToWin - 1)
		start_banner(w, 99);
	else
		start_banner(w, f->roundIndex + 1);
	w->matchFinished = 0;
	w->roundTimer = f->roundSeconds;
	aos_screen_fade(w, f->firstInit ? 0x13 : 6);
	f->nextRound = 0;
	f->matchEnd = 0;
	f->fadePending = 0;
	f->firstInit = 0;
}

void aos_flow_frame_start(aos_world *w)
{
	aos_flow *f = &w->flow;
	if (f->fadePending) { /* previous frame's FUN_00561c70 (it runs after the draw) */
		f->fadePending = 0;
		fade_update(w);
	}
	if (f->nextRound) /* FUN_006124b0: DAT_00885878 -> FUN_005f8750 + Game Init */
		aos_round_init(w);
}

void aos_flow_frame_end(aos_world *w)
{
	/* FUN_006b5b50 tail (runs from the HUD draw, which happens only while the
	 * HUD is visible): a faded-out combo counter resets its hit count to 1. */
	if (aos_hud_visible(w))
		for (int i = 0; i < 2; i++)
			if (w->p[i].comboFadeB == 0.0f)
				w->p[i].comboShowHits = 1;
	w->flow.fadePending = 1;
}

void aos_match_restart(aos_world *w)
{
	w->p[0].roundsWon = w->p[1].roundsWon = 0;
	w->flow.firstInit = 1;
	aos_round_init(w);
}
