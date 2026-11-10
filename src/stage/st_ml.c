/* Stage 18 "ml" (Moonlight Dance Hall) — build/stagedocs/ml.md.
 * loader FUN_004cef60, unload FUN_004cf5b0, init FUN_004cfb10, reinit FUN_004cfcf0,
 * exit FUN_004cfe70, update FUN_004cfec0, draw FUN_004cff60
 * (camera FUN_004cf8a0, states FUN_004cf780, light FUN_004cf800,
 *  star vector at DAT_0087b420: 12-byte elements, push_back FUN_004d0d90,
 *  size FUN_004d0db0, at FUN_004d0af0). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

#define ML_MAX_STARS 60  /* init pushes exactly 30 * 2 */

static struct {
	int frame;                    /* DAT_0087b3d0 */
	float cloudAng;               /* DAT_0087b3d4 */
	float unused3d8;              /* DAT_0087b3d8 (animated, never read) */
	float star[ML_MAX_STARS][3];  /* std::vector<vec3> */
	int nstar;
	float lightR, lightG, lightB; /* DAT_0087b40c / b410 / b414 */
	float ambient;                /* DAT_0087b418 */
	sg_camera cam;                /* DAT_0087b41c */
	int texSky, texCloud, texCloud2, texFtCloud, texMoon, texStar; /* DAT_0087b3dc / 3e4 / 3ec / 3f4 / 3fc / 404 */
	sg_model *sky, *cloud, *cloud2, *ftCloud, *moon, *starM;       /* DAT_0087b3e0 / 3e8 / 3f0 / 3f8 / 400 / 408 */
} S;

static void load(void)
{
	S.texSky = sg_tex_load(2, "ml_sky");
	S.sky = sg_sphere(500.0f /* DAT_00814140 */, 300.0f /* DAT_008145f0 */, 500.0f, 20, 20);
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);
	S.texCloud = sg_tex_load(2, "ml_cloud");
	S.cloud = sg_sphere(400.0f /* DAT_008145f4 */, 400.0f, 400.0f, 20, 20);
	sg_set_tex(S.cloud, 0, S.texCloud, -1, -1);
	S.texCloud2 = sg_tex_load(2, "ml_cloud2");
	/* DAT_0087b3f0: loaded but never drawn by FUN_004cff60 (kept, literal). */
	S.cloud2 = sg_sphere(495.0f /* DAT_008145f8 */, 350.0f /* DAT_00814be0 */, 495.0f, 20, 20);
	sg_set_tex(S.cloud2, 0, S.texCloud2, -1, -1);
	S.texFtCloud = sg_tex_load(2, "ft_cloud2");
	S.ftCloud = sg_grid_xz(2400.0f /* DAT_00815588 */, 2400.0f, 20, 20, 1.0f);
	sg_set_tex(S.ftCloud, 0, S.texFtCloud, -1, -1);
	sg_fog_type(S.ftCloud, SG_FOG_NONE, -1);
	S.texMoon = sg_tex_load(2, "ml_moon");
	S.moon = sg_plane(240.0f /* DAT_00815584 */, 240.0f);
	sg_set_tex(S.moon, 0, S.texMoon, -1, -1);
	sg_light_type(S.moon, SG_LIGHT_WHITE, -1);
	sg_fog_type(S.moon, SG_FOG_NONE, -1);
	S.texStar = sg_tex_load(2, "ml_star");
	S.starM = sg_plane(1.5f /* DAT_00814110 */, 1.5f);
	sg_set_tex(S.starM, 0, S.texStar, -1, -1);
	sg_light_type(S.starM, SG_LIGHT_WHITE, -1);
	sg_fog_type(S.starM, SG_FOG_NONE, -1);
}

static void push_star(float x, float y, float z)
{
	if (S.nstar < ML_MAX_STARS) {
		S.star[S.nstar][0] = x;
		S.star[S.nstar][1] = y;
		S.star[S.nstar][2] = z;
		S.nstar++;
	}
}

/* shared body of FUN_004cfb10 / FUN_004cfcf0 (RNG order x, y, z per star) */
static void reset_stars(void)
{
	S.frame = 0;
	S.cloudAng = 0;
	S.unused3d8 = 0;
	S.nstar = 0;
	for (int i = 0; i < 30; i++) {
		float x = stage_rand(100.0f /* DAT_00853b90 */, 300.0f /* DAT_008145f0 */);
		float y = stage_rand(0, 200.0f /* DAT_008147cc */);
		float z = stage_rand(200.0f, 400.0f /* DAT_008145f4 */);
		push_star(x, y, z);
		x = -stage_rand(100.0f, 300.0f);
		y = stage_rand(0, 200.0f);
		z = stage_rand(200.0f, 500.0f /* DAT_00814140 */);
		push_star(x, y, z);
	}
}

static void init(void)
{
	reset_stars();
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void reinit(void) { reset_stars(); }

static void stage_exit(void) { S.nstar = 0; }

static void update(void)
{
	S.frame++;
	S.cloudAng += 0.0025f;       /* DAT_008145e0 */
	if (3.1415927f < S.cloudAng)
		S.cloudAng -= 6.2831855f;
	S.unused3d8 += 0.00175f;     /* _DAT_008145d4 */
	if (3.1415927f < S.unused3d8)
		S.unused3d8 -= 6.2831855f;
}

/* FUN_004cf8a0 */
static void camera(void)
{
	float fov = 70.0f;                          /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 30.0f;     /* DAT_00813f4c, DAT_008140e8 */
	float eye[3] = {0, 0, -200.0f};             /* DAT_00814600 */
	float at[3] = {0, 0, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2500.0f /* DAT_008147d4 */, stage_g.aspect);
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	float s = sinf(((float)(S.frame % 3300) / 3300.0f /* _DAT_008147dc */) * 6.2831855f);
	sg_cam_rot_x(&S.cam, sg_rad2bam(s * 0.15f /* DAT_00813f20 */));
	if (stage_g.gameMode == 2) {
		s = sinf(((float)(S.frame % 2700) / 2700.0f /* _DAT_008149dc */) * 6.2831855f);
		sg_cam_rot_z(&S.cam, sg_rad2bam(s * 0.1f /* DAT_00853b10 */));
		s = sinf(((float)(S.frame % 1300) / 1300.0f /* _DAT_008147d0 */) * 6.2831855f);
		sg_cam_rot_y(&S.cam, sg_rad2bam(s * 0.15f));
	}
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004cf780 (fog colour/range set, fog disabled) */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	sg_light_enable(0, 1);
	sg_fog(600.0f /* DAT_008149b8 */, 800.0f /* DAT_008149c8 */, 0xff5791ff);
	/* FUN_004cf800 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_light_dir(0, dir);
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_reset(S.sky);
	sg_pos(S.sky, 0, 0, 0);
	sg_rot_y(S.sky, sg_rad2bam(((float)(S.frame % 10000) / 10000.0f /* _DAT_00853ba0 */) * 6.2831855f));
	sg_rot_y(S.sky, sg_deg2bam(180));
	sg_render(S.sky);
	sg_render_end(S.sky);
	sg_blend(2);
	sg_reset(S.moon);
	sg_pos(S.moon, 0, 100.0f, 200.0f);
	sg_render(S.moon);
	sg_render_end(S.moon);
	for (int i = 0; i < S.nstar; i++) {
		sg_reset(S.starM);
		sg_pos(S.starM, S.star[i][0], S.star[i][1], S.star[i][2]);
		sg_render(S.starM);
		sg_render_end(S.starM);
	}
	sg_blend(1);
	for (int i = 10; -1 < i; i--) {
		float s = sinf(((float)((i * 50 + S.frame) % 500) / 500.0f) * 6.2831855f);
		float r = s * 0.25f /* DAT_00813f24 */ + 0.75f /* DAT_00813f3c */;
		float g = s * 0.15f /* DAT_00813f20 */ + 0.85f /* DAT_00814120 */;
		int period = i * -300 + 8000;
		sg_reset(S.ftCloud);
		sg_color(S.ftCloud, 1.0f, r, g, 1.0f, -1);
		sg_pos(S.ftCloud, 0, -50.0f - (float)i * 10.0f, 0);
		sg_rot_y(S.ftCloud, (uint32_t)sg_rad2bam(((float)(S.frame % period) / (float)period) * 6.2831855f) +
		                        sg_deg2bam(i * 36));
		sg_render(S.ftCloud);
		sg_render_end(S.ftCloud);
	}
	sg_reset(S.cloud);
	sg_color(S.cloud, 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.cloud, 0, 0, 0);
	sg_rot_y(S.cloud, sg_rad2bam(S.cloudAng));
	sg_render(S.cloud);
	sg_render_end(S.cloud);
	sg_restore_2d();
}

const stage_def stage_ml = {"ml", "Moonlight Dance Hall", load, NULL, init, reinit, stage_exit, update, draw};
