/* Stage 10 "se" (Equator doldrums) — build/stagedocs/se.md.
 * loader FUN_004d2090, unload FUN_004d2590, init FUN_004d29f0, reinit FUN_004d2a90,
 * exit FUN_004d2ac0, update FUN_004d2b10, draw FUN_004d2be0
 * (camera FUN_004d2810, states FUN_004d26f0, light FUN_004d2770).
 * Sibling of stage 9 "ri" (st_ri.c); differences are listed in se.md. */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* _DAT_0087b490 (reset only, never incremented or read) */
	float cloudAng;               /* DAT_0087b494 */
	float seaUV;                  /* DAT_0087b498 */
	float rainV;                  /* DAT_0087b49c */
	float lightR, lightG, lightB; /* DAT_0087b4c4 / b4c8 / b4cc */
	float ambient;                /* DAT_0087b4d0 */
	sg_camera cam;                /* DAT_0087b4d4 */
	int texRain, texCloud, texHorizon, texSea;      /* DAT_0087b4a0 / b4ac / b4b4 / b4bc */
	sg_model *rain, *rain2, *cloud, *horizon, *sea; /* DAT_0087b4a4 / b4a8 / b4b0 / b4b8 / b4c0 */
} S;

static void load(void)
{
	S.texRain = sg_tex_load(2, "se_rain");
	/* DAT_0087b4a4: loaded but never drawn by FUN_004d2be0 (kept, literal). */
	S.rain = sg_sphere(300.0f /* DAT_008145f0 */, 3000.0f /* DAT_008147d8 */, 300.0f, 50, 50);
	sg_set_tex(S.rain, 0, S.texRain, -1, -1);
	sg_fog_type(S.rain, SG_FOG_VIEW, -1);
	S.rain2 = sg_sphere(600.0f /* DAT_008149b8 */, 3000.0f, 600.0f, 50, 50);
	sg_set_tex(S.rain2, 0, S.texRain, -1, -1);
	sg_fog_type(S.rain2, SG_FOG_VIEW, -1);
	S.texCloud = sg_tex_load(2, "se_cloud");
	S.cloud = sg_grid_xz(1000.0f /* DAT_00814408 */, 1000.0f, 20, 20, 1.0f);
	sg_set_tex(S.cloud, 0, S.texCloud, -1, -1);
	sg_fog_type(S.cloud, SG_FOG_VIEW, -1);
	S.texSea = sg_tex_load(2, "se_sea");
	S.sea = sg_grid_xz(3000.0f, 2000.0f /* DAT_008145fc */, 50, 50, 12.0f /* DAT_00813f54 */);
	sg_set_tex(S.sea, 0, S.texSea, -1, -1);
	sg_fog_type(S.sea, SG_FOG_VIEW, -1);
	S.texHorizon = sg_tex_load(2, "se_horizon");
	S.horizon = sg_grid_xy(3000.0f, 1500.0f /* DAT_008149d4 */, 5, 5, 1.0f);
	sg_set_tex(S.horizon, 0, S.texHorizon, -1, -1);
	sg_fog_type(S.horizon, SG_FOG_VIEW, -1);
}

static void init(void)
{
	S.frame = 0;
	S.cloudAng = S.seaUV = S.rainV = 0;
	S.lightR = 0.9f;  /* DAT_00814178 */
	S.lightG = 0.8f;  /* DAT_008145e8 */
	S.lightB = 0.7f;  /* DAT_00813f38 */
	S.ambient = 0.85f;/* DAT_00814120 */
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.frame = 0;
	S.cloudAng = S.seaUV = S.rainV = 0;
}

static void stage_exit(void) {}

static void update(void)
{
	S.cloudAng -= 0.0005f;           /* _DAT_0081587c */
	if (S.cloudAng < -3.1415927f)    /* DAT_00814100 */
		S.cloudAng += 3.1415927f;    /* DAT_00853b58 (pi, not 2pi: literal) */
	S.seaUV += 0.0125f;              /* _DAT_00815880 */
	if (1.0f < S.seaUV)
		S.seaUV -= 1.0f;
	S.rainV += 0.02f;                /* DAT_00813ed8 */
	if (1.0f < S.rainV)
		S.rainV -= 0.5f;             /* DAT_00853b1c (literal) */
	/* no frame counter increment, no sound effect (unlike ri) */
}

/* FUN_004d2810: animated by the match frame counter DAT_008858f8, not a stage counter. */
static void camera(void)
{
	float fov = 50.0f;                      /* DAT_008140ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 60.0f; /* DAT_00813f4c, DAT_00813a58 */
	float eye[3] = {0, 0, 0};
	float a = ((float)(stage_g.matchFrame % 3000) / 3000.0f) * 6.2831855f;
	float b = ((float)(stage_g.matchFrame % 2000) / 2000.0f) * 6.2831855f;
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
	/* FUN_004d26f0 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 1);
	sg_rs(0x16, 1);
	sg_light_enable(0, 1);
	sg_fog(0, 2000.0f, 0xff000000);
	/* FUN_004d2770 */
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
	sg_uv_v(S.sea, S.seaUV, -1);
	sg_uv_u(S.sea, S.seaUV, -1);
	sg_render(S.sea);
	sg_render_end(S.sea);
	sg_blend(2);
	sg_reset(S.horizon);
	sg_pos(S.horizon, 0, 100.0f, 1000.0f);
	sg_render(S.horizon);
	sg_render_end(S.horizon);
	sg_blend(1);
	sg_reset(S.rain2);
	sg_pos(S.rain2, 0, 0, 0);
	sg_uv_v(S.rain2, -S.rainV + 0.5f, -1);
	sg_render(S.rain2);
	sg_render_end(S.rain2);
	sg_restore_2d();
}

const stage_def stage_se_def = {"se", "Equator doldrums", load, NULL, init, reinit, stage_exit, update, draw};
