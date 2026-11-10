/*
 * Stage backgrounds (docs/re/stages.md). Literal ports of the per-stage
 * functions registered by FUN_004bf480 (tables 0x87a878 init, 0x87a8c8
 * re-init, 0x87a918 exit, 0x87a968 update, 0x87a9b8 draw) plus the texture
 * loaders/unloaders called from Game Init (FUN_005f4650).
 *
 * Platform independent: drawing goes through sg.h. The caller owns the
 * environment (stage_env) and feeds it every tick.
 */
#ifndef STAGE_H
#define STAGE_H

#include <stdint.h>

#include "../sim/rng.h"
#include "sg.h"

enum {
	STAGE_RANDOM = 1,  /* VS list entry 1 = random (FUN_007aaf90) */
	STAGE_MENU = 4,    /* "sl" menu / title background */
	STAGE_FIRST = 5,
	STAGE_LAST = 19,
	STAGE_COUNT = 20,
};

enum { STAGE_LOD_EXACT, STAGE_LOD_CAP, STAGE_LOD_HALF, STAGE_LOD_CAP_HALF, STAGE_LOD_COUNT };
extern const char *const stage_lod_names[STAGE_LOD_COUNT];

/* The original globals the stage code reads. */
typedef struct {
	int gameMode;      /* DAT_00885870: 2 = in a match (camera follows zoom, SE allowed) */
	float zoom;        /* _DAT_008859e4: gameplay zoom Z after the focus blend (aos_world.obj.zoomView) */
	int matchFrame;    /* DAT_008858f8: match frame counter */
	float aspect;      /* DAT_009b1010: window W/H passed to the stage cameras (PC 800/600) */
	aos_mt rng;        /* cosmetic MT stream 0x875b68 (FUN_00459fd0 / FUN_00459f80 outside the tick) */
	void (*se)(int id);/* FUN_007bc9c0: stage sound effects (thunder, explosions) */
	int lod;           /* 3DS presentation option for the city sprites (stages 12/14):
	                      STAGE_LOD_EXACT / _CAP / _HALF / _CAP_HALF (stages.md §5) */
} stage_env;
extern stage_env stage_g;

typedef struct {
	const char *code;      /* two-letter code (texture prefix / "stageXX" name texture) */
	const char *name;
	void (*load)(void);    /* texture/model loader (Game Init switch, FUN_005f4650) */
	void (*unload)(void);  /* FUN_004c04b0 family (called for every stage at Game Init) */
	void (*init)(void);    /* 0x87a878 */
	void (*reinit)(void);  /* 0x87a8c8 (stage already active) */
	void (*exit)(void);    /* 0x87a918 */
	void (*update)(void);  /* 0x87a968 */
	void (*draw)(int layer); /* 0x87a9b8: layer 1 = background pass (FUN_005f91d0), 0 = unused by stages */
} stage_def;

extern const stage_def *const stage_defs[STAGE_COUNT];

/* VS stage list in select-screen order (FUN_0079acc0, unlock flags DAT_0087b744..752 all set). */
extern const int stage_vs_list[];
extern const int stage_vs_count;

/* FUN_007aaf90: random pick from the VS list without the "random" entry (cosmetic RNG). */
int stage_random(void);

/* Load assets + init (Game Init order: unload all, load `id`, FUN_004bfc10(id)). */
void stage_begin(int id);
void stage_end(void);      /* exit + unload + sg_free_all */
void stage_update(void);   /* FUN_004bfd30 (once per non-resimulated tick) */
void stage_draw(void);     /* FUN_004bfd80(1): records into sg_cmds */
void stage_round(void);    /* new round: FUN_004bfc10 on an active stage = re-init */
int stage_current(void);
const char *stage_name(int id);

/* helpers shared by the stage files */
float stage_rand(float lo, float hi);    /* FUN_00459fd0 on the cosmetic stream */
int32_t stage_randi(int32_t lo, int32_t hi); /* FUN_00459f80 */
void stage_se(int id);

#endif
