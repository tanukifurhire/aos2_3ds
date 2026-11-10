/* Stage 12 "ct" (Capital in Flames) and stage 14 (End of Winter) — build/stagedocs/ct.md.
 * Both ids register the same functions (FUN_004bf480) and Game Init loads both with the
 * same loader; the stage code never reads the stage id (DAT_0087fadc).
 * loader FUN_004c3dc0, unload FUN_004c42a0, init FUN_004c4b70, reinit FUN_004c4f90,
 * exit FUN_004c5350, update FUN_004c53c0, draw FUN_004c5630
 * (states FUN_004c4430, light FUN_004c44b0, camera FUN_004c4570,
 *  smoke update FUN_004c4840, flame update FUN_004c4a70). */
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "stage.h"

/* std::list 0x87ab9c: fire sources (16 bytes) */
typedef struct {
	float x;      /* +0 */
	float y;      /* +4 (never initialised by the pushes, never read) */
	float z;      /* +8 */
	int on;       /* +0xc (bool, always 1) */
} ct_src;

/* std::list 0x87ab94: smoke (28 bytes) */
typedef struct {
	float x, y, z;  /* +0 / +4 / +8 */
	float life;     /* +0xc: alpha, -0.0025 per tick */
	float red;      /* +0x10: colour red, -0.005 per tick (clamped at 0) */
	float scale;    /* +0x14: +0.01 per tick, also the drift amplitude */
	int anim;       /* +0x18: texture frame counter ((anim/10)%4) */
	unsigned serial;/* port only: spawn number (stable choice for the sprite cap) */
} ct_smoke;

/* std::list 0x87aba4: flames (24 bytes) */
typedef struct {
	float x, y, z;  /* +0 / +4 / +8 */
	float alpha;    /* +0xc: 1 - age/120 */
	float scale;    /* +0x10: +0.01 per tick */
	int age;        /* +0x14 */
	unsigned serial;/* port only */
} ct_flame;

#define CT_MAX_SRC 22
#define CT_MAX_SMOKE 1024 /* steady state: 22 sources * 40 = 880 */
#define CT_MAX_FLAME 512  /* steady state: 22 * ~14 = ~300 */

static struct {
	int frame;                     /* DAT_0087ab20 */
	float lightR, lightG, lightB;  /* DAT_0087ab80 / ab84 / ab88 */
	float ambient;                 /* DAT_0087ab8c */
	sg_camera cam;                 /* DAT_0087ab90 */
	ct_src src[CT_MAX_SRC];
	int nsrc;
	ct_smoke smoke[CT_MAX_SMOKE];
	int nsmoke;
	ct_flame flame[CT_MAX_FLAME];
	int nflame;
	sg_model *city, *cityF, *tower;  /* DAT_0087ab24 / ab28 / ab2c */
	sg_model *smokeM[4];             /* DAT_0087ab30 */
	int smokeT[4];                   /* DAT_0087ab40 */
	sg_model *flameM[6];             /* DAT_0087ab50 */
	int flameT[6];                   /* DAT_0087ab68 */
	/* 3DS presentation options (stage_g.lod, stages.md §5) — not part of the original */
	unsigned serial;
	int halfSkip;                    /* this tick's sprite step was done on the previous tick */
	sg_cmd *cache;                   /* half-rate: sprite commands of the last stepped tick */
	int ncache, cacheValid;
} S;

/* Sprite budget of the "Smoke cap" mode: above it only every k-th sprite (by spawn
 * number, so the choice is stable while sprites age) is drawn, k = ceil(n / CT_CAP). */
#define CT_CAP 300

static void load(void)
{
	char name[32];
	S.city = sg_xfile("city");
	sg_fog_type(S.city, SG_FOG_VIEW, -1);
	S.cityF = sg_xfile("city_f");
	sg_fog_type(S.cityF, SG_FOG_VIEW, -1);
	S.tower = sg_xfile("tower");
	sg_fog_type(S.tower, SG_FOG_VIEW, -1);
	for (int i = 0; i < 4; i++) {
		snprintf(name, sizeof name, "cy_smoke_0%d", i);
		S.smokeT[i] = sg_tex_load(2, name);
		S.smokeM[i] = sg_plane(32.0f, 32.0f); /* DAT_008140dc */
		sg_set_tex(S.smokeM[i], 0, S.smokeT[i], -1, -1);
		sg_light_type(S.smokeM[i], SG_LIGHT_NONE, -1);
		sg_fog_type(S.smokeM[i], SG_FOG_NONE, -1);
	}
	for (int i = 0; i < 6; i++) {
		snprintf(name, sizeof name, "cy_flame0%d", i);
		S.flameT[i] = sg_tex_load(2, name);
		S.flameM[i] = sg_plane(32.0f, 32.0f);
		sg_set_tex(S.flameM[i], 0, S.flameT[i], -1, -1);
		sg_light_type(S.flameM[i], SG_LIGHT_NONE, -1);
		sg_fog_type(S.flameM[i], SG_FOG_NONE, -1);
	}
}

/* FUN_004c70b0 on list 0x87ab9c */
static void push_src(float x, float z)
{
	if (S.nsrc < CT_MAX_SRC)
		S.src[S.nsrc++] = (ct_src){x, 0, z, 1};
}

/* shared tail of FUN_004c4b70 / FUN_004c4f90 */
static void reset_lists(void)
{
	S.frame = 0;
	S.cacheValid = 0;
	S.nsmoke = 0; /* FUN_004c6e20 on 0x87ab94 */
	S.nsrc = 0;   /* FUN_004c6dc0 on 0x87ab9c */
	push_src(600.0f, 1200.0f);   /* DAT_008149b8, DAT_008149d0 */
	push_src(680.0f, 600.0f);    /* DAT_008149bc, DAT_008149b8 */
	push_src(-600.0f, -650.0f);  /* DAT_00814a04, DAT_00814a08 */
	push_src(-720.0f, -100.0f);  /* DAT_00814a0c, DAT_00853bb8 */
	push_src(-200.0f, -1300.0f); /* DAT_00814600, DAT_00814a20 */
	push_src(-250.0f, -500.0f);  /* DAT_008149f4, DAT_008149f8 */
	push_src(850.0f, 50.0f);     /* DAT_008149cc, DAT_008140ec */
	push_src(-1500.0f, -500.0f); /* DAT_00814a24, DAT_008149f8 */
	push_src(-50.0f, -600.0f);   /* DAT_008149e4, DAT_00814a04 */
	push_src(-750.0f, 300.0f);   /* DAT_00814a10, DAT_008145f0 */
	push_src(-950.0f, 500.0f);   /* DAT_00814a1c, DAT_00814140 */
	push_src(1700.0f, 200.0f);   /* DAT_008149d8, DAT_008147cc */
	push_src(120.0f, -900.0f);   /* DAT_00814174, DAT_00814a18 */
	push_src(1500.0f, -150.0f);  /* DAT_008149d4, DAT_008149ec */
	push_src(750.0f, -250.0f);   /* DAT_008149c4, DAT_008149f4 */
	push_src(560.0f, 700.0f);    /* DAT_008149b4, DAT_008149c0 */
	push_src(700.0f, -550.0f);   /* DAT_008149c0, DAT_008149fc */
	push_src(100.0f, 800.0f);    /* DAT_00853b90, DAT_008149c8 */
	push_src(-800.0f, 600.0f);   /* DAT_00814a14, DAT_008149b8 */
	push_src(550.0f, -230.0f);   /* DAT_008149b0, DAT_008149f0 */
	push_src(-560.0f, 1000.0f);  /* DAT_00814a00, DAT_00814408 */
	push_src(-60.0f, 700.0f);    /* DAT_008149e8, DAT_008149c0 */
	S.nflame = 0; /* FUN_004c6d60 on 0x87aba4 */
}

/* FUN_004c4b70 */
static void init(void)
{
	S.frame = 0;
	S.lightR = 0.75f;  /* DAT_00813f3c */
	S.lightG = 0.25f;  /* DAT_00813f24 */
	S.lightB = 0.25f;
	S.ambient = 0.5f;  /* DAT_00853b1c */
	sg_cam_init(&S.cam);
	reset_lists();
}

/* FUN_004c4f90 */
static void reinit(void) { reset_lists(); }

/* FUN_004c5350 */
static void stage_exit(void)
{
	S.nflame = 0;
	S.nsmoke = 0;
	S.nsrc = 0;
	free(S.cache);
	S.cache = NULL;
	S.cacheValid = 0;
}

/* FUN_004c4840 */
static void update_smoke(void)
{
	for (int i = 0; i < S.nsmoke; i++) {
		ct_smoke *s = &S.smoke[i];
		s->anim = (s->anim + 1) % 4000;
		s->y += 1.0f;
		float r = stage_rand(-1.0f, 1.0f);
		s->x = r * s->scale * 1.0f + s->x;
		r = stage_rand(-1.0f, 1.0f);
		s->z = r * s->scale * 1.0f + s->z;
		s->scale += 0.01f;  /* DAT_00813ec8 */
		s->red -= 0.005f;   /* DAT_00813eac */
		if (s->red < 0.0f)
			s->red = 0;
		s->life -= 0.0025f; /* DAT_008145e0 */
		if (!(0.0f < s->life)) {
			memmove(s, s + 1, (size_t)(S.nsmoke - i - 1) * sizeof *s);
			S.nsmoke--;
			i--;
		}
	}
}

/* FUN_004c4a70 */
static void update_flames(void)
{
	for (int i = 0; i < S.nflame; i++) {
		ct_flame *f = &S.flame[i];
		f->age++;
		f->scale += 0.01f; /* DAT_00813ec8 */
		f->alpha = 1.0f - (float)f->age / 120.0f; /* DAT_00814174 */
		if (!(0.0f < f->alpha)) {
			memmove(f, f + 1, (size_t)(S.nflame - i - 1) * sizeof *f);
			S.nflame--;
			i--;
		}
	}
}

/* FUN_004c53c0 */
static void update(void)
{
	if (S.frame % 10 == 0) {
		for (int i = 0; i < S.nsrc; i++) {
			if (!S.src[i].on)
				continue;
			ct_smoke s = {S.src[i].x, 0, S.src[i].z, 1.0f, 1.0f, 0.5f, 0, 0};
			s.anim = stage_randi(0, 40);
			if (S.nsmoke < CT_MAX_SMOKE) /* FUN_004c70d0 */
				s.serial = S.serial++, S.smoke[S.nsmoke++] = s;
		}
	}
	if (S.frame % 9 == 0) {
		for (int i = 0; i < S.nsrc; i++) {
			if (!S.src[i].on)
				continue;
			ct_flame f;
			f.x = stage_rand(-25.0f, 25.0f) + S.src[i].x; /* DAT_008149e0, DAT_008140f0 */
			f.z = stage_rand(-25.0f, 25.0f) + S.src[i].z;
			f.y = 0;
			f.alpha = 1.0f;
			f.scale = stage_rand(0, 1.0f) + 1.0f;
			f.age = 0;
			if (S.nflame < CT_MAX_FLAME) /* FUN_004c7090 */
				f.serial = S.serial++, S.flame[S.nflame++] = f;
		}
	}
	if (stage_g.lod == STAGE_LOD_HALF || stage_g.lod == STAGE_LOD_CAP_HALF) {
		/* half rate: both steps of a tick pair on the even tick (same RNG draws per pair,
		 * same state after every even tick), nothing on the odd tick */
		S.halfSkip = S.frame & 1;
		if (!S.halfSkip) {
			update_smoke();
			update_flames();
			update_smoke();
			update_flames();
		}
	} else {
		S.halfSkip = 0;
		update_smoke();
		update_flames();
	}
	S.frame++;
}

/* FUN_004c4570 */
static void camera(void)
{
	float fov = 70.0f; /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 50.0f; /* DAT_00813f4c, DAT_008140ec */
	float a = ((float)(S.frame % 8000) / 8000.0f) * 6.2831855f; /* DAT_008147ec, DAT_00853b78 */
	float eye[3] = {cosf(a) * 1000.0f, 1000.0f, sinf(a) * 1000.0f}; /* DAT_00814408 */
	float at[3] = {0, 250.0f, 0};                                    /* DAT_008149ac */
	sg_cam_persp(&S.cam, fov * 0.64f, 50.0f, 4000.0f, stage_g.aspect); /* DAT_008145e4, DAT_008140ec, DAT_008147e0 */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	float s = sinf(((float)(S.frame % 3300) / 3300.0f) * 6.2831855f); /* DAT_008147dc */
	sg_cam_rot_x(&S.cam, sg_rad2bam(s * 0.15f));                       /* DAT_00813f20 */
	if (stage_g.gameMode == 2) {
		s = sinf(((float)(S.frame % 2700) / 2700.0f) * 6.2831855f); /* DAT_008149dc */
		sg_cam_rot_z(&S.cam, sg_rad2bam(s * 0.1f));                 /* DAT_00853b10 */
		s = sinf(((float)(S.frame % 1300) / 1300.0f) * 6.2831855f); /* DAT_008147d0 */
		sg_cam_rot_y(&S.cam, sg_rad2bam(s * 0.15f));
	}
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

/* FUN_004c4430 */
static void states(void)
{
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	sg_fog(1000.0f, 2500.0f, 0xff000000); /* DAT_00814408, DAT_008147d4 */
}

/* FUN_004c44b0: light from above, or from below (param 1, used for the tower) */
static void light(int up)
{
	float dir[3] = {0, up == 1 ? 1.0f : -1.0f, 0}; /* DAT_00853b34 / DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	states();
	light(0);
	sg_clear(0xff000000);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x16, 3);
	sg_reset(S.city);
	sg_scale(S.city, 1.0f, 2.0f, 1.0f); /* DAT_00813a4c */
	sg_color(S.city, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.city, 0, 0, 0);
	sg_render(S.city);
	sg_render_end(S.city);
	float s = sinf(((float)(S.frame % 50) / 50.0f) * 6.2831855f); /* DAT_008140ec */
	float glow = s * 0.15f + 0.85f; /* DAT_00813f20, DAT_00814120 */
	sg_rs(0xe, 0);
	sg_rs(7, 0);
	sg_blend(2);
	sg_reset(S.cityF);
	sg_color(S.cityF, glow * 0.25f, 1.0f, 0.25f, 0, -1); /* DAT_00813f24 */
	sg_pos(S.cityF, 0, 0, 0);
	sg_render(S.cityF);
	sg_render_end(S.cityF);
	sg_blend(2);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x16, 1);
	int first = sg_cmds.n;
	bool half = stage_g.lod == STAGE_LOD_HALF || stage_g.lod == STAGE_LOD_CAP_HALF;
	if (half && S.halfSkip && S.cacheValid && sg_cmds.n + S.ncache <= SG_MAX_CMDS) {
		/* odd tick: the sprites did not move; re-emit last tick's commands (the camera
		 * snapshot slot is the same, so they follow the current camera) */
		memcpy(&sg_cmds.cmd[sg_cmds.n], S.cache, (size_t)S.ncache * sizeof(sg_cmd));
		sg_cmds.n += S.ncache;
		goto sprites_done;
	}
	unsigned stride = 1;
	if (stage_g.lod == STAGE_LOD_CAP || stage_g.lod == STAGE_LOD_CAP_HALF) {
		unsigned n = (unsigned)(S.nsmoke + S.nflame);
		if (n > CT_CAP)
			stride = (n + CT_CAP - 1) / CT_CAP;
	}
	for (int i = 0; i < S.nflame; i++) {
		const ct_flame *f = &S.flame[i];
		if (f->serial % stride)
			continue;
		int k = f->age / 20;
		if (5 < k)
			k = 5;
		sg_model *m = S.flameM[k];
		sg_reset(m);
		sg_color(m, f->alpha / 2.0f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00813a4c */
		sg_scale(m, f->scale, f->scale, f->scale);
		sg_pos(m, f->x, f->y, f->z);
		sg_render(m);
		sg_render_end(m);
	}
	sg_blend(1);
	sg_rs(7, 1);
	sg_rs(0x16, 1);
	for (int i = 0; i < S.nsmoke; i++) {
		const ct_smoke *p = &S.smoke[i];
		if (p->serial % stride)
			continue;
		sg_model *m = S.smokeM[(p->anim / 10) % 4];
		sg_reset(m);
		sg_color(m, p->life, p->red, 0, 0, -1);
		sg_scale(m, p->scale, p->scale, p->scale);
		sg_pos(m, p->x, p->y, p->z);
		sg_render(m);
		sg_render_end(m);
	}
	if (half) {
		if (!S.cache)
			S.cache = malloc(SG_MAX_CMDS * sizeof(sg_cmd));
		S.ncache = sg_cmds.n - first;
		S.cacheValid = S.cache != NULL;
		if (S.cache)
			memcpy(S.cache, &sg_cmds.cmd[first], (size_t)S.ncache * sizeof(sg_cmd));
	}
sprites_done:
	sg_blend(1); /* state after the smoke loop (also when re-emitting) */
	sg_rs(7, 1);
	sg_rs(0x16, 1);
	sg_rs(0xe, 1);
	sg_rs(7, 1);
	sg_rs(0x16, 3);
	light(1);
	sg_reset(S.tower);
	sg_pos(S.tower, 0, 0, 0);
	sg_render(S.tower);
	sg_render_end(S.tower);
	sg_restore_2d();
}

const stage_def stage_ct = {"ct", "Capital in Flames", load, NULL, init, reinit, stage_exit, update, draw};
const stage_def stage_ct2 = {"ct", "End of Winter", load, NULL, init, reinit, stage_exit, update, draw};
