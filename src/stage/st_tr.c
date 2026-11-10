/* Stage 5 "tr" (Before the War: wireframe training room) — build/stagedocs/tr.md.
 * loader FUN_004d6500, unload FUN_004d6790, init FUN_004d6ab0, reinit FUN_004d6b40,
 * exit FUN_004d6b70, update FUN_004d6bc0, draw FUN_004d6d80
 * (camera FUN_004d6970, states FUN_004d6880, light FUN_004d68d0). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	float b0;                     /* _DAT_0087b5b0 (cleared, never read) */
	float rot;                    /* DAT_0087b5b4: room yaw (rad) */
	float camPh;                  /* DAT_0087b5b8: camera bob phase (rad) */
	float lightR, lightG, lightB; /* DAT_0087b5d4 / 5d8 / 5dc */
	float ambient;                /* DAT_0087b5e0 */
	sg_camera cam;                /* DAT_0087b5e4 */
	float pulse;                  /* DAT_0087b5e8: glow phase (never reset; static 0 at boot) */
	int texWire, texWireB;        /* DAT_0087b5bc / 5c8 */
	sg_model *wallA, *floorA;     /* DAT_0087b5c0 (xy) / 5c4 (xz), tr_wire */
	sg_model *wallB, *floorB;     /* DAT_0087b5cc (xy) / 5d0 (xz), tr_wireb */
} S;

static void load(void)
{
	S.texWire = sg_tex_load(2, "tr_wire");
	S.wallA = sg_grid_xy(600.0f, 600.0f, 10, 20, 20.0f); /* DAT_008149b8 = 600, DAT_008140fc = 20 */
	sg_set_tex(S.wallA, 0, S.texWire, -1, -1);
	S.floorA = sg_grid_xz(600.0f, 600.0f, 10, 20, 20.0f);
	sg_set_tex(S.floorA, 0, S.texWire, -1, -1);
	S.texWireB = sg_tex_load(2, "tr_wireb");
	S.wallB = sg_grid_xy(600.0f, 600.0f, 10, 20, 20.0f);
	sg_set_tex(S.wallB, 0, S.texWireB, -1, -1);
	S.floorB = sg_grid_xz(600.0f, 600.0f, 10, 20, 20.0f);
	sg_set_tex(S.floorB, 0, S.texWireB, -1, -1);
}

/* FUN_004d6790 */
static void unload(void)
{
	if (S.texWire) { sg_tex_free(S.texWire); S.texWire = 0; }
	if (S.texWireB) { sg_tex_free(S.texWireB); S.texWireB = 0; }
	if (S.wallA) { sg_model_free(S.wallA); S.wallA = 0; }
	if (S.wallB) { sg_model_free(S.wallB); S.wallB = 0; }
	if (S.floorA) { sg_model_free(S.floorA); S.floorA = 0; }
	if (S.floorB) { sg_model_free(S.floorB); S.floorB = 0; }
}

static void init(void)
{
	S.b0 = 0;
	S.rot = 0;
	S.camPh = 0;
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f; /* DAT_00853b34 */
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.b0 = 0;
	S.rot = 0;
	S.camPh = 0;
}

static void stage_exit(void) {}

static void update(void)
{
	S.pulse += 0.05f;                 /* DAT_00813ef4 */
	if (3.1415927f < S.pulse)         /* DAT_00853b58 */
		S.pulse -= 6.2831855f;        /* DAT_00853b78 */
	sg_color(S.wallB, sinf(S.pulse) * 0.5f + 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_color(S.floorB, sinf(S.pulse) * 0.5f + 0.5f, 1.0f, 1.0f, 1.0f, -1);
	S.rot += 0.00125f;                /* DAT_00814bdc */
	if (3.1415927f < S.rot)
		S.rot -= 6.2831855f;
	S.camPh += 0.0025f;               /* DAT_008145e0 */
	if (3.1415927f < S.camPh)
		S.camPh -= 6.2831855f;
}

/* FUN_004d6970: fixed fov 30 (DAT_008140e8) * 0.64, near 10, far 2000; no zoom dependence. */
static void camera(void)
{
	float fov = 30.0f;
	float eye[3] = {0, 0, 0};
	float at[3] = {0, sinf(S.camPh) * 5.0f, 10.0f}; /* DAT_00813f40 = 5, DAT_00813f4c = 10 */
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect);
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

/* Four walls around the camera (yaw a, a+pi/2, a-pi/2, a+pi at radius 300) + floor/ceiling. */
static void room(sg_model *wall, sg_model *flr)
{
	static const float off[4] = {0, 1.5707964f, -1.5707964f, 3.1415927f}; /* DAT_00814104, DAT_00853b58 */
	for (int i = 0; i < 4; i++) {
		float a = S.rot + off[i]; /* (b4 - pi/2 for i == 2: same value) */
		sg_reset(wall);
		sg_rot_y(wall, sg_rad2bam(a));
		sg_pos(wall, sinf(a) * 300.0f, 0, cosf(a) * 300.0f); /* DAT_008145f0 */
		sg_render(wall);
		sg_render_end(wall);
	}
	sg_reset(flr);
	sg_rot_y(flr, sg_rad2bam(S.rot));
	sg_pos(flr, 0, 300.0f, 0);
	sg_render(flr);
	sg_render_end(flr);
	sg_reset(flr);
	sg_rot_y(flr, sg_rad2bam(S.rot));
	sg_pos(flr, 0, -300.0f, 0);   /* DAT_00815f44 */
	sg_render(flr);
	sg_render_end(flr);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004d6880 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	/* FUN_004d68d0 */
	static const float dir[3] = {0, -1.0f, 0}; /* DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	sg_blend(1);
	room(S.wallA, S.floorA);
	sg_blend(2);
	room(S.wallB, S.floorB);
	sg_restore_2d();
}

const stage_def stage_tr = {"tr", "Before the War", load, unload, init, reinit, stage_exit, update, draw};
