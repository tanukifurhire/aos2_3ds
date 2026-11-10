/* Stage 9 "ri" (Rain and Sunset) — build/stagedocs/ri.md.
 * loader FUN_004d1110, unload FUN_004d1630, init FUN_004d1ab0, reinit FUN_004d1b50,
 * exit FUN_004d1b80, update FUN_004d1bd0, draw FUN_004d1cd0
 * (camera FUN_004d18b0, states FUN_004d1790, light FUN_004d1810).
 * Sibling of stage 10 "se" (st_se.c). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* DAT_0087b448 */
	float cloudAng;               /* DAT_0087b44c */
	float seaV;                   /* DAT_0087b450 */
	float rainV;                  /* DAT_0087b454 */
	float lightR, lightG, lightB; /* DAT_0087b47c / b480 / b484 */
	float ambient;                /* DAT_0087b488 */
	sg_camera cam;                /* DAT_0087b48c */
	int texRain, texCloud, texHorizon, texSea;      /* DAT_0087b458 / b464 / b46c / b474 */
	sg_model *rain, *rain2, *cloud, *horizon, *sea; /* DAT_0087b45c / b460 / b468 / b470 / b478 */
} S;

static void load(void)
{
	S.texRain = sg_tex_load(2, "ri_rain");
	/* DAT_0087b45c: loaded but never drawn by FUN_004d1cd0 (kept, literal). */
	S.rain = sg_sphere(240.0f /* DAT_00815584 */, 3000.0f /* DAT_008147d8 */, 240.0f, 50, 50);
	sg_set_tex(S.rain, 0, S.texRain, -1, -1);
	sg_fog_type(S.rain, SG_FOG_VIEW, -1);
	S.rain2 = sg_sphere(480.0f /* DAT_00815884 */, 3000.0f, 480.0f, 50, 50);
	sg_set_tex(S.rain2, 0, S.texRain, -1, -1);
	sg_fog_type(S.rain2, SG_FOG_VIEW, -1);
	S.texCloud = sg_tex_load(2, "ri_cloud");
	S.cloud = sg_grid_xz(1000.0f /* DAT_00814408 */, 1000.0f, 20, 20, 1.0f);
	sg_set_tex(S.cloud, 0, S.texCloud, -1, -1);
	sg_fog_type(S.cloud, SG_FOG_VIEW, -1);
	S.texSea = sg_tex_load(2, "ri_sea");
	S.sea = sg_grid_xz(3000.0f, 2000.0f /* DAT_008145fc */, 50, 50, 12.0f /* DAT_00813f54 */);
	sg_set_tex(S.sea, 0, S.texSea, -1, -1);
	sg_fog_type(S.sea, SG_FOG_VIEW, -1);
	S.texHorizon = sg_tex_load(2, "ri_horizon");
	S.horizon = sg_grid_xy(3000.0f, 1500.0f /* DAT_008149d4 */, 5, 5, 1.0f);
	sg_set_tex(S.horizon, 0, S.texHorizon, -1, -1);
	sg_fog_type(S.horizon, SG_FOG_NONE, -1);
	sg_light_type(S.horizon, SG_LIGHT_NONE, -1);
}

static void init(void)
{
	S.frame = 0;
	S.cloudAng = S.seaV = S.rainV = 0;
	S.lightR = 0.9f;  /* DAT_00814178 */
	S.lightG = 0.8f;  /* DAT_008145e8 */
	S.lightB = 0.7f;  /* DAT_00813f38 */
	S.ambient = 0.85f;/* DAT_00814120 */
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.frame = 0;
	S.cloudAng = S.seaV = S.rainV = 0;
}

static void stage_exit(void) {}

static void update(void)
{
	S.cloudAng -= 0.0005f;           /* _DAT_0081587c */
	if (S.cloudAng < -3.1415927f)    /* DAT_00814100 */
		S.cloudAng += 3.1415927f;    /* DAT_00853b58 (pi, not 2pi: literal) */
	S.seaV += 0.0125f;               /* _DAT_00815880 */
	if (1.0f < S.seaV)
		S.seaV -= 1.0f;
	S.rainV += 0.02f;                /* DAT_00813ed8 */
	if (1.0f < S.rainV)
		S.rainV -= 0.5f;             /* DAT_00853b1c (literal) */
	if (S.frame % 350 == 300 && stage_g.gameMode == 2)
		stage_se(0x36);
	S.frame++;
}

/* FUN_004d18b0 */
static void camera(void)
{
	float fov;
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 60.0f; /* DAT_00813f4c, DAT_00813a58 */
	else
		fov = 70.0f;                        /* DAT_008145ec */
	float eye[3] = {0, 0, 0};
	float a = ((float)((S.frame + 500) % 3000) / 3000.0f) * 6.2831855f;
	float b = ((float)(S.frame % 2000) / 2000.0f) * 6.2831855f;
	float at[3] = {sinf(b) * 0.75f, sinf(a) * 1.5f + 0.5f, cosf(a) * 0.5f + 1.5f};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect);
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
	/* FUN_004d1790 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 1);
	sg_rs(0x16, 1);
	sg_light_enable(0, 1);
	sg_fog(0, 2000.0f, 0xff000000);
	/* FUN_004d1810 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_light_dir(0, dir);
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_clear(0xff000000);
	for (int i = 10; 0 < i; i--) {
		sg_reset(S.cloud);
		sg_pos(S.cloud, 0, (float)i * 200.0f /* DAT_008147cc */, (float)i * -50.0f /* DAT_008149e4 */);
		sg_rot_y(S.cloud, (uint32_t)sg_rad2bam(S.cloudAng) + sg_deg2bam(i * 36));
		sg_render(S.cloud);
		sg_render_end(S.cloud);
	}
	sg_reset(S.sea);
	sg_pos(S.sea, 0, -50.0f, 1000.0f);
	sg_uv_v(S.sea, S.seaV, -1);
	sg_render(S.sea);
	sg_render_end(S.sea);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_reset(S.horizon);
	sg_pos(S.horizon, -1500.0f /* DAT_00814a24 */, 100.0f, 1000.0f);
	sg_render(S.horizon);
	sg_render_end(S.horizon);
	sg_reset(S.horizon);
	sg_pos(S.horizon, 1500.0f, 100.0f, 1000.0f);
	sg_render(S.horizon);
	sg_render_end(S.horizon);
	sg_blend(1);
	sg_fog(0, 2000.0f, 0xff000000);
	sg_reset(S.rain2);
	sg_pos(S.rain2, 0, 0, 0);
	sg_uv_v(S.rain2, -S.rainV + 0.5f, -1);
	sg_render(S.rain2);
	sg_render_end(S.rain2);
	sg_restore_2d();
}

const stage_def stage_ri = {"ri", "Rain and Sunset", load, NULL, init, reinit, stage_exit, update, draw};
