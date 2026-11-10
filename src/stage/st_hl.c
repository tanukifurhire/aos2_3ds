/* Stage 17 "hl" (Guardian's Chamber) — build/stagedocs/hl.md.
 * loader FUN_004cbfa0, unload FUN_004cc1a0, init FUN_004cc490, reinit FUN_004cc540,
 * exit FUN_004cc590, update FUN_004cc5e0, draw FUN_004cca90
 * (camera FUN_004cc330, states FUN_004cc240, light FUN_004cc290, 2D rotate FUN_005f1210,
 * CModel +0x64 orient FUN_0046d590). */
#include <math.h>
#include <string.h>

#include "stage.h"

#define HL_N1 36 /* ring 1: DAT_0087ad30, stride 0x10 */
#define HL_N2 36 /* ring 2: DAT_0087af70 */
#define HL_N3 24 /* ring 3: DAT_0087b1b0 */

static struct {
	int frame;              /* DAT_0087ad20 */
	float camAng;           /* DAT_0087ad24 */
	float wallAng;          /* DAT_0087ad28 */
	float pulse;            /* DAT_0087ad2c */
	float ring1[HL_N1][4];  /* x, y, z, (pad) */
	float ring2[HL_N2][4];
	float ring3[HL_N3][4];
	float tilt1, tilt2;     /* DAT_0087b330 / b334 */
	float lightR, lightG, lightB; /* DAT_0087b348 / b34c / b350 */
	float ambient;          /* DAT_0087b354 */
	sg_camera cam;          /* DAT_0087b358 */
	int texWall, texCube;   /* DAT_0087b338 / b340 */
	sg_model *wall, *cube;  /* DAT_0087b33c / b344 */
} S;

static void load(void)
{
	S.texWall = sg_tex_load(2, "hl_wall");
	S.wall = sg_sphere(600.0f, 600.0f, 600.0f, 20, 100); /* DAT_008149b8 */
	sg_set_tex(S.wall, 0, S.texWall, -1, -1);
	sg_light_type(S.wall, SG_LIGHT_NONE, -1);
	S.texCube = sg_tex_load(2, "hl_cube");
	S.cube = sg_box(16.0f, 16.0f, 16.0f); /* DAT_008140d8 */
	sg_set_tex(S.cube, 0, S.texCube, -1, -1);
}

static void reinit(void)
{
	S.frame = 0;
	S.camAng = 0;
	S.wallAng = 0;
	S.pulse = 0;
	S.tilt1 = 0;
	S.tilt2 = 0;
}

static void init(void)
{
	reinit();
	S.lightR = 0.5f;  /* DAT_00853b1c */
	S.lightG = 1.0f;
	S.lightB = 0.85f; /* DAT_00814120 */
	S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void stage_exit(void) {}

/* FUN_005f1210: rotate (x, y) by `a` */
static void rot2d(float *v, float a)
{
	float c = cosf(a), s = sinf(a), x = v[0];
	v[0] = v[0] * c - v[1] * s;
	v[1] = x * s + v[1] * c;
}

static void update(void)
{
	S.camAng += 0.0025f; /* DAT_008145e0 */
	if (3.1415927f < S.camAng)
		S.camAng -= 6.2831855f;
	S.wallAng += 0.001f; /* DAT_00814bd8 */
	if (3.1415927f < S.wallAng)
		S.wallAng -= 6.2831855f;
	S.pulse = sinf(((float)(S.frame % 200) / 200.0f) * 6.2831855f) * 0.2f + 0.8f; /* DAT_008147cc, 00853b14, 008145e8 */
	S.ambient = S.pulse;
	for (int i = 0; i < HL_N1; i++) {
		float t = ((float)(S.frame % 1000) / 1000.0f) * 6.2831855f;
		S.ring1[i][0] = cosf(((float)i / 36.0f) * 6.2831855f + t) * 250.0f; /* DAT_008140f4, DAT_008149ac */
		S.ring1[i][2] = sinf(((float)i / 36.0f) * 6.2831855f + t) * 250.0f;
		S.ring1[i][1] = 0;
		rot2d(S.ring1[i], sinf(S.tilt1) * 0.75f); /* DAT_00813f3c */
	}
	if (249 < S.frame % 300 && (S.tilt1 += 0.01f, 3.1415927f < S.tilt1))
		S.tilt1 -= 6.2831855f;
	for (int i = 0; i < HL_N2; i++) {
		float t = ((float)(S.frame % 1300) / 1300.0f) * 6.2831855f; /* DAT_008147d0 */
		S.ring2[i][0] = cosf(((float)i / 36.0f) * 6.2831855f - t) * 280.0f; /* DAT_00814f14 */
		S.ring2[i][2] = sinf(((float)i / 36.0f) * 6.2831855f - t) * 280.0f;
		S.ring2[i][1] = 0;
		rot2d(S.ring2[i], sinf(S.tilt2) * 0.75f);
	}
	if (S.frame % 300 < 50 && (S.tilt2 -= 0.01f, S.tilt2 < -3.1415927f))
		S.tilt2 += 6.2831855f;
	for (int i = 0; i < HL_N3; i++) {
		S.ring3[i][0] = cosf(((float)i / 24.0f) * 6.2831855f) * 120.0f; /* DAT_00853b8c, DAT_00814174 */
		S.ring3[i][2] = sinf(((float)i / 24.0f) * 6.2831855f) * 120.0f;
		S.ring3[i][1] = -200.0f; /* DAT_00814600 */
	}
	S.frame++;
}

/* CModel vtable +0x64 (FUN_0046d590): orientation from position `p` looking at `at`
 * (forward = +Z row), up hint `up` (NULL = +Y); also sets the position. */
static void model_orient(sg_model *m, const float p[3], const float at[3], const float *upv)
{
	float u[3] = {0, 1.0f, 0};
	if (upv)
		memcpy(u, upv, sizeof u);
	float f[3] = {at[0] - p[0], at[1] - p[1], at[2] - p[2]};
	float l = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
	if (l == 0) {
		/* p == at (only before the first update, when the ring tables are still 0):
		 * the original divides by zero (NaN matrix). Keep the reset orientation instead. */
		memcpy(m->pos, p, sizeof m->pos);
		return;
	}
	l = 1.0f / sqrtf(l);
	f[0] *= l;
	f[1] *= l;
	f[2] *= l;
	if (f[0] == 0 && f[2] == 0 && (f[1] == 1.0f || f[1] == -1.0f)) {
		u[0] = 0;
		u[1] = 0;
		u[2] = -f[1];
	}
	/* s = -(f x u) / |f x u| */
	float s[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
	l = 1.0f / sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	s[0] *= -l;
	s[1] *= -l;
	s[2] *= -l;
	/* y = f x s, normalized */
	float y[3] = {s[2] * f[1] - s[1] * f[2], s[0] * f[2] - s[2] * f[0], s[1] * f[0] - s[0] * f[1]};
	l = 1.0f / sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (int k = 0; k < 3; k++) {
		m->rot[0][k] = s[k];     /* +0x84 */
		m->rot[1][k] = y[k] * l; /* +0x78 */
		m->rot[2][k] = f[k];     /* +0x6c */
		m->pos[k] = p[k];        /* +0x90 */
	}
}

/* FUN_004cc330 */
static void camera(void)
{
	float fov = 75.0f; /* DAT_00815254 */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 40.0f; /* DAT_00813f4c, DAT_008140e0 */
	float eye[3] = {0, sinf(S.camAng) * 180.0f + 0.0f, -120.0f}; /* DAT_00815258, PTR_00853af4, DAT_0081525c */
	float at[3] = {0, 0, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect); /* DAT_008145e4, DAT_008145fc */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

static void ring(float (*r)[4], int n, const float at[3], int wrapUp, const float *up, int glow)
{
	for (int i = 0; i < n; i++) {
		float p[3] = {r[i][0], r[i][1], r[i][2]}, d[3];
		sg_reset(S.cube);
		sg_pos(S.cube, p[0], p[1], p[2]);
		if (wrapUp) {
			int j = (i + 1) % n;
			d[0] = r[j][0] - r[i][0];
			d[1] = r[j][1] - r[i][1];
			d[2] = r[j][2] - r[i][2];
			up = d;
		}
		model_orient(S.cube, p, at, up);
		sg_render(S.cube);
		sg_render_end(S.cube);
		if (glow) {
			/* second, additive pass without depth test */
			sg_rs(7, 0);
			sg_blend(2);
			model_orient(S.cube, p, at, up);
			sg_render(S.cube);
			sg_render_end(S.cube);
			sg_rs(7, 1);
			sg_blend(1);
		}
	}
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004cc240 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	/* FUN_004cc290 */
	static const float dir[3] = {0, 1.0f, 0};
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	/* blend / cull are not set here: inherited (FUN_004bfdd0 of the previous frame: blend 1, cull none) */
	sg_reset(S.wall);
	sg_pos(S.wall, 0, 0, 0);
	sg_color(S.wall, 1.0f, S.pulse, S.pulse, S.pulse, -1);
	sg_rot_y(S.wall, sg_rad2bam(S.wallAng));
	sg_render(S.wall);
	sg_render_end(S.wall);

	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x16, 3);
	static const float origin[3] = {0, 0, 0};
	ring(S.ring1, HL_N1, origin, 1, NULL, 1);
	ring(S.ring2, HL_N2, origin, 1, NULL, 0);
	static const float at3[3] = {0, -200.0f, 0}, up3[3] = {0, 1.0f, 0};
	ring(S.ring3, HL_N3, at3, 0, up3, 0);
	sg_restore_2d();
}

const stage_def stage_hl = {"hl", "Guardian's Chamber", load, NULL, init, reinit, stage_exit, update, draw};
