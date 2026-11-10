/* Stage 13 "iw" (Whirlpool of Malice) — build/stagedocs/iw.md.
 * loader FUN_004cd0e0, unload FUN_004cd430, init FUN_004cd9e0, reinit FUN_004cdb30,
 * exit FUN_004cdc10, update FUN_004cdc60, draw FUN_004cdde0
 * (states FUN_004cd520, light FUN_004cd570, camera FUN_004cd610,
 *  rock spawn FUN_004cd770, rock move FUN_004cd910). */
#include <math.h>
#include <string.h>

#include "stage.h"

/* std::vector element (32 bytes), pushed by FUN_004ceef0 */
typedef struct {
	float x, y, z;      /* +0 / +4 / +8 */
	int type;           /* +0xc: spawn counter % 3 (selects the scale) */
	int unused;         /* +0x10: always 0 */
	float rx, ry, rz;   /* +0x14 / +0x18 / +0x1c: radians, constant */
} iw_rock;

#define IW_MAX_ROCK 256 /* steady state: 1100 / 2.5 / 5 = 88 */

static struct {
	int frame;                    /* DAT_0087b360 */
	int spawned;                  /* DAT_0087b364 */
	float uvV, uvU;               /* DAT_0087b368 / b36c (wind texture scroll) */
	float b370, b374;             /* DAT_0087b370 / b374: advanced, never used */
	float b378;                   /* DAT_0087b378: angle, advanced, never used */
	float eye[3];                 /* DAT_0087b3a8 / b3ac / b3b0 */
	float dir[3];                 /* DAT_0087b3b4 / b3b8 / b3bc (look-at = eye + dir) */
	float lightR, lightG, lightB; /* DAT_0087b394 / b398 / b39c */
	float ambient;                /* DAT_0087b3a0 */
	sg_camera cam;                /* DAT_0087b3a4 */
	iw_rock rock[IW_MAX_ROCK];
	int nrock;
	sg_model *wind, *sky, *box;   /* DAT_0087b37c / b384 / b38c */
	int texWind, texSky, texRock; /* DAT_0087b380 / b388 / b390 */
} S;

static void load(void)
{
	S.texWind = sg_tex_load(2, "iw_wind");
	S.wind = sg_sphere(500.0f, 2000.0f, 500.0f, 50, 50); /* DAT_00814140, DAT_008145fc */
	sg_set_tex(S.wind, 0, S.texWind, -1, -1);
	sg_fog_type(S.wind, SG_FOG_VIEW, -1);
	sg_light_type(S.wind, SG_LIGHT_NONE, -1);
	S.texSky = sg_tex_load(2, "iw_sky");
	S.sky = sg_sphere(2000.0f, 2000.0f, 2000.0f, 100, 100);
	sg_fog_type(S.sky, SG_FOG_NONE, -1);
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);
	sg_light_type(S.sky, SG_LIGHT_NONE, -1);
	S.texRock = sg_tex_load(2, "iw_rock");
	S.box = sg_box(100.0f, 100.0f, 100.0f); /* DAT_00853b90 */
	sg_set_tex(S.box, 0, S.texRock, -1, -1);
}

/* FUN_004cd770 */
static void spawn(void)
{
	iw_rock r;
	r.x = stage_rand(-1000.0f, 1000.0f); /* DAT_00814604, DAT_00814408 */
	r.z = stage_rand(-1000.0f, 1000.0f);
	if (r.x + r.z < 200.0f) { /* DAT_008147cc (quirk: signed sum, not a distance) */
		if (fabsf(r.z) <= fabsf(r.x))
			r.z = 300.0f; /* DAT_008145f0 */
		else
			r.x = 300.0f;
	}
	r.y = S.eye[1] - 100.0f; /* DAT_00853b90 */
	r.rx = stage_rand(-3.1415927f, 3.1415927f); /* DAT_00814100, DAT_00853b58 */
	r.ry = stage_rand(-3.1415927f, 3.1415927f);
	r.rz = stage_rand(-3.1415927f, 3.1415927f);
	r.unused = 0;
	r.type = S.spawned % 3;
	if (S.nrock < IW_MAX_ROCK)
		S.rock[S.nrock++] = r;
	S.spawned++;
}

/* FUN_004cd910 */
static void move(void)
{
	for (int i = 0; i < S.nrock; i++) {
		S.rock[i].y += 2.5f; /* DAT_008153e0 */
		if (S.eye[1] + 1000.0f < S.rock[i].y) {
			memmove(&S.rock[i], &S.rock[i + 1], (size_t)(S.nrock - i - 1) * sizeof S.rock[0]);
			S.nrock--;
			i--;
		}
	}
}

/* FUN_004cdb30 (also the first part of FUN_004cd9e0) */
static void reinit(void)
{
	S.frame = 0;
	S.spawned = 0;
	S.uvV = S.uvU = S.b370 = S.b374 = S.b378 = 0;
	S.eye[0] = S.eye[1] = S.eye[2] = 0;
	S.dir[0] = 0;
	S.dir[2] = 0;
	S.dir[1] = 10.0f; /* DAT_00813f4c */
	S.nrock = 0;
	for (int i = 0; i < 1000; i++) {
		if (i % 5 == 0)
			spawn();
		move();
	}
}

/* FUN_004cd9e0 */
static void init(void)
{
	reinit();
	S.lightR = 0.75f; /* DAT_00813f3c */
	S.lightG = 0.25f; /* DAT_00813f24 */
	S.lightB = 0.25f;
	S.ambient = 0.5f; /* DAT_00853b1c */
	sg_cam_init(&S.cam);
}

/* FUN_004cdc10 */
static void stage_exit(void) { S.nrock = 0; }

static float wrap1(float v, float step)
{
	v += step;
	if (1.0f < v)
		v -= 1.0f;
	return v;
}

/* FUN_004cdc60 */
static void update(void)
{
	S.b378 += 0.001f; /* DAT_00814bd8 */
	if (3.1415927f < S.b378)
		S.b378 -= 6.2831855f;
	S.uvV = wrap1(S.uvV, 0.0025f); /* DAT_008145e0 */
	S.uvU = wrap1(S.uvU, 0.0015f); /* DAT_008153dc */
	S.b370 = wrap1(S.b370, 0.004f); /* DAT_00813ea4 */
	S.b374 = wrap1(S.b374, 0.002f); /* DAT_008145dc */
	if (S.frame % 5 == 0)
		spawn();
	move();
	S.frame++;
}

/* FUN_004cd610: looks straight up (dir (0,10,0) with up (0,1,0): the LookAt degenerate case) */
static void camera(float farZ)
{
	float fov = 120.0f; /* DAT_00814174 */
	if (stage_g.gameMode == 2)
		fov = 20.0f * stage_g.zoom + 100.0f; /* DAT_008140fc, DAT_00853b90 */
	float at[3] = {S.eye[0] + S.dir[0], S.eye[1] + S.dir[1], S.eye[2] + S.dir[2]};
	static const float up[3] = {0, 1.0f, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, farZ, stage_g.aspect); /* DAT_008145e4, DAT_00813f4c */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, S.eye, at, up);
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera(4000.0f); /* DAT_008147e0 */
	/* FUN_004cd520 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	sg_light_enable(0, 1);
	/* FUN_004cd570 */
	static const float ldir[3] = {0, -1.0f, 0}; /* DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, ldir);
	sg_light_enable(0, 1);

	sg_rs(0x16, 1);
	sg_reset(S.sky);
	sg_color(S.sky, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.sky, S.eye[0], S.eye[1], S.eye[2]);
	sg_render(S.sky);
	sg_render_end(S.sky);
	sg_blend(2);
	sg_rs(0x1c, 1);
	sg_fog(1000.0f, 3000.0f, 0xff000000); /* DAT_00814408, DAT_008147d8 */
	sg_reset(S.wind);
	sg_pos(S.wind, S.eye[0], S.eye[1], S.eye[2]);
	sg_color(S.wind, 0.25f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00813f24 */
	sg_scale(S.wind, 5.0f, 1.5f, 5.0f);            /* DAT_00813f40, DAT_00814110 */
	sg_uv_v(S.wind, S.uvV, -1);
	sg_uv_u(S.wind, S.uvU, -1);
	sg_render(S.wind);
	sg_render_end(S.wind);
	sg_reset(S.wind);
	sg_pos(S.wind, S.eye[0], S.eye[1], S.eye[2]);
	sg_color(S.wind, 0.5f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00853b1c */
	sg_scale(S.wind, 2.0f, 1.5f, 2.0f);           /* DAT_00813a4c */
	sg_uv_v(S.wind, S.uvV, -1);
	sg_uv_u(S.wind, S.uvU, -1);
	sg_render(S.wind);
	sg_render_end(S.wind);
	sg_blend(1);
	sg_rs(0xe, 1);
	sg_rs(7, 1);
	sg_fog(100.0f, 1000.0f, 0xff000000); /* DAT_00853b90 (rocks have fog type NONE) */
	for (int i = 0; i < S.nrock; i++) {
		const iw_rock *r = &S.rock[i];
		float a = 1.0f;
		if (S.eye[1] + 600.0f < r->y) /* DAT_008149b8 */
			a = (S.eye[1] + 1000.0f - r->y) / 400.0f; /* DAT_008145f4 */
		sg_reset(S.box);
		sg_color(S.box, a, 1.0f, 1.0f, 1.0f, -1);
		sg_pos(S.box, r->x + S.eye[0], r->y, r->z + S.eye[2]);
		sg_rot_x(S.box, sg_rad2bam(r->rx));
		sg_rot_y(S.box, sg_rad2bam(r->ry));
		sg_rot_z(S.box, sg_rad2bam(r->rz));
		if (r->type == 0)
			sg_scale(S.box, 1.0f, 0.08f, 0.08f); /* DAT_00813f00 */
		else if (r->type == 1)
			sg_scale(S.box, 0.6f, 0.3f, 0.05f);  /* DAT_00813f34, DAT_00813f28, DAT_00813ef4 */
		else
			sg_scale(S.box, 0.25f, 0.25f, 0.25f);
		sg_render(S.box);
		sg_render_end(S.box);
	}
	sg_restore_2d();
}

const stage_def stage_iw = {"iw", "Whirlpool of Malice", load, NULL, init, reinit, stage_exit, update, draw};
