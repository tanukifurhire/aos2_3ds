/* Stage 4 "sl" (menu / title background) — build/stagedocs/sl.md.
 * loader FUN_004d2f10, unload FUN_004d3370, init FUN_004d3700, reinit FUN_004d3790,
 * exit FUN_004d37c0, update FUN_004d3810, draw FUN_004d38b0
 * (states FUN_004d34f0, light FUN_004d3540, camera FUN_004d35e0).
 * No gameMode checks in any of these functions (fixed camera, fov 50). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* DAT_0087b4d8 */
	float atmosU;                 /* DAT_0087b4dc */
	float roll;                   /* DAT_0087b4e0 */
	float lightR, lightG, lightB; /* DAT_0087b50c / b510 / b514 */
	float ambient;                /* DAT_0087b518 */
	sg_camera cam;                /* DAT_0087b51c */
	int texAtmosGrid, texSph, texSphB, texAtmos, texRoll; /* DAT_0087b4e8 / b4f0 / b4f8 / b500 / b508 */
	sg_model *atmosGrid; /* DAT_0087b4e4 (loaded, never drawn) */
	sg_model *sph;       /* DAT_0087b4ec */
	sg_model *sphB;      /* DAT_0087b4f4 */
	sg_model *atmos;     /* DAT_0087b4fc */
	sg_model *stripe;    /* DAT_0087b504 */
} S;

static void load(void)
{
	S.texAtmosGrid = sg_tex_load(2, "sl_atmos");
	S.atmosGrid = sg_grid_xz(1800.0f, 1800.0f, 10, 10, 1.0f); /* DAT_00814f18 */
	sg_set_tex(S.atmosGrid, 0, S.texAtmosGrid, -1, -1);
	S.texSph = sg_tex_load(2, "sl_shpere");
	S.sph = sg_sphere(400.0f, 400.0f, 400.0f, 80, 80); /* DAT_008145f4 */
	sg_set_tex(S.sph, 0, S.texSph, -1, -1);
	S.texSphB = sg_tex_load(2, "sl_shpereb");
	S.sphB = sg_sphere(400.0f, 400.0f, 400.0f, 80, 80);
	sg_set_tex(S.sphB, 0, S.texSphB, -1, -1);
	S.texAtmos = sg_tex_load(2, "sl_atmos");
	S.atmos = sg_sphere(400.0f, 400.0f, 400.0f, 80, 80);
	sg_set_tex(S.atmos, 0, S.texAtmos, -1, -1);
	S.texRoll = sg_tex_load(2, "sl_roll");
	S.stripe = sg_xfile("stripe"); /* "mdl\\stripe.x" */
	sg_set_tex(S.stripe, 0, S.texRoll, -1, -1);
}

static void reinit(void)
{
	S.frame = 0;
	S.atmosU = 0;
	S.roll = 0;
}

static void init(void)
{
	reinit();
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void stage_exit(void) {}

static void update(void)
{
	S.atmosU += 0.00025f; /* DAT_00815bc4 */
	if (1.0f < S.atmosU)
		S.atmosU -= 1.0f;
	S.roll += 0.0025f; /* DAT_008145e0 */
	if (1.0f < S.roll)
		S.roll -= 1.0f;
	S.frame++;
}

/* FUN_004d35e0 */
static void camera(void)
{
	float eye[3] = {0, 750.0f, 1500.0f}; /* DAT_008149c4, DAT_008149d4 */
	float at[3] = {0, -200.0f, 0};       /* DAT_00814600 */
	/* fov 50 (DAT_008140ec) * 0.64, near 10, far 3000 (DAT_008147d8) */
	sg_cam_persp(&S.cam, 50.0f * 0.64f, 10.0f, 3000.0f, stage_g.aspect);
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004d34f0 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 0);
	sg_light_enable(0, 0);
	/* FUN_004d3540 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_reset(S.stripe);
	sg_pos(S.stripe, 0, -400.0f, 0); /* DAT_00815bc8 */
	sg_color(S.stripe, 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_uv_v(S.stripe, -S.roll, -1);
	sg_render(S.stripe);
	sg_render_end(S.stripe);

	sg_reset(S.sphB);
	sg_pos(S.sphB, 0, 0, 0);
	float v = sinf(((float)(S.frame % 400) / 400.0f) * 6.2831855f) * 0.05f + 0.05f; /* DAT_008145f4, DAT_00813ef4 */
	sg_color(S.sphB, 1.0f, v, v, v, -1);
	sg_render(S.sphB);
	sg_render_end(S.sphB);

	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	/* FUN_004676b0(0, 0) omitted: the flag is never read by the vertex-shader path
	 * (FUN_00472480 uploads light 0's colour unconditionally), but sg zeroes the light
	 * colour of a disabled light. See sl.md. */
	sg_blend(2);
	sg_reset(S.sph);
	sg_color(S.sph, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.sph, 0, 0, 0);
	sg_render(S.sph);
	sg_render_end(S.sph);
	sg_reset(S.atmos);
	sg_color(S.atmos, 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.atmos, 0, 0, 0);
	sg_uv_u(S.atmos, S.atmosU, -1);
	sg_render(S.atmos);
	sg_render_end(S.atmos);
	sg_restore_2d();
}

const stage_def stage_sl = {"sl", "Menu", load, NULL, init, reinit, stage_exit, update, draw};
