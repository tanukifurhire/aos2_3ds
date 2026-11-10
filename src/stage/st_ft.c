/* Stage 15 "ft" (Nature 10,000 Years After) — build/stagedocs/ft.md.
 * loader FUN_004c86f0, unload FUN_004c88c0, init FUN_004c8cf0, reinit FUN_004c8d90,
 * exit FUN_004c8dd0, update FUN_004c8e20, draw FUN_004c8f40
 * (camera FUN_004c8a50, states FUN_004c8960, light FUN_004c89b0). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* DAT_0087ac20 */
	float camY;                   /* DAT_0087ac24 (camera target bob) */
	float roll;                   /* DAT_0087ac28 (camera roll phase) */
	float skyAng;                 /* DAT_0087ac2c */
	float cloudAng;               /* DAT_0087ac30 */
	float lightR, lightG, lightB; /* DAT_0087ac44 / ac48 / ac4c */
	float ambient;                /* DAT_0087ac50 */
	sg_camera cam;                /* DAT_0087ac54 */
	int texForest, texCloud;      /* DAT_0087ac34 / ac3c */
	sg_model *forest, *cloud;     /* DAT_0087ac38 / ac40 */
} S;

static void load(void)
{
	S.texForest = sg_tex_load(2, "ft_forest");
	S.forest = sg_sphere(600.0f /* DAT_008149b8 */, 400.0f /* DAT_008145f4 */, 600.0f, 40, 40);
	sg_set_tex(S.forest, 0, S.texForest, -1, -1);
	S.texCloud = sg_tex_load(2, "ft_cloud2");
	S.cloud = sg_grid_xz(900.0f /* DAT_00814d74 */, 900.0f, 20, 20, 1.0f);
	sg_set_tex(S.cloud, 0, S.texCloud, -1, -1);
}

static void init(void)
{
	S.frame = 0;
	S.camY = S.roll = S.skyAng = S.cloudAng = 0;
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.frame = 0;
	S.camY = S.roll = S.skyAng = S.cloudAng = 0;
}

static void stage_exit(void) {}

static void update(void)
{
	S.camY += 0.0025f;           /* DAT_008145e0 */
	if (3.1415927f < S.camY)
		S.camY -= 6.2831855f;
	S.roll += 0.00075f;          /* _DAT_00814bd4 */
	if (6.2831855f < S.roll)     /* DAT_00853b78 (wraps at 2pi, not pi) */
		S.roll -= 6.2831855f;
	S.skyAng += 0.002f;          /* _DAT_008145dc */
	if (3.1415927f < S.skyAng)
		S.skyAng -= 6.2831855f;
	S.cloudAng += 0.0035f;       /* DAT_00814d6c */
	if (3.1415927f < S.cloudAng)
		S.cloudAng -= 6.2831855f;
	S.frame++;
}

/* FUN_004c8a50 */
static void camera(void)
{
	float fov = 70.0f;                          /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 15.0f * stage_g.zoom + 40.0f;     /* DAT_00853b88, DAT_008140e0 */
	if (stage_g.gameMode == 4)
		fov = 90.0f;                            /* DAT_00814170 */
	float eye[3] = {0, 0, -200.0f};             /* DAT_00814600 */
	float at[3] = {0, sinf(S.camY) * 40.0f + 50.0f, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect);
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	if (S.roll < 1.5707964f) {                  /* DAT_00814104 */
		sg_cam_rot_z(&S.cam, sg_rad2bam((sinf(S.roll) - 1.0f) * 0.35f /* DAT_00813f2c */));
	} else if (4.712389f < S.roll) {            /* DAT_00814d70 */
		sg_cam_rot_z(&S.cam, sg_rad2bam((sinf(S.roll) + 1.0f) * -0.35f /* DAT_00814d78 */));
	} else {
		sg_cam_rot_z(&S.cam, sg_rad2bam((fabsf(sinf(S.roll)) - 1.0f) * -0.35f));
	}
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004c8960 (fog enabled but no FUN_00463750 call: all meshes are SG_FOG_NONE) */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	/* FUN_004c89b0 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_light_dir(0, dir);
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_reset(S.forest);
	sg_pos(S.forest, 0, 0, 0);
	sg_rot_y(S.forest, sg_rad2bam(S.skyAng));
	sg_render(S.forest);
	sg_render_end(S.forest);
	sg_rs(7, 0);
	sg_reset(S.cloud);
	sg_pos(S.cloud, 0, 100.0f, 0);
	sg_rot_y(S.cloud, sg_rad2bam(S.cloudAng));
	sg_render(S.cloud);
	sg_render_end(S.cloud);
	sg_restore_2d();
}

const stage_def stage_ft = {"ft", "Nature 10,000 Years After", load, NULL, init, reinit, stage_exit, update, draw};
