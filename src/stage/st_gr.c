/* Stage 7 "gr" (Canyon of Wind) — build/stagedocs/gr.md.
 * loader FUN_004c90e0, unload FUN_004c9510, init FUN_004c9900, reinit FUN_004c9a60,
 * exit FUN_004c9b60, update FUN_004c9bb0, draw FUN_004c9ec0
 * (camera FUN_004c9740, states FUN_004c9620, light FUN_004c96a0). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* DAT_0087ac58 */
	float speed;                  /* DAT_0087ac5c (= 5, written, never read) */
	float ac60;                   /* _DAT_0087ac60 (= 1, never read) */
	float gust;                   /* DAT_0087ac64: extra wind scroll speed (0 .. 0.005) */
	float scroll;                 /* DAT_0087ac68: canyon z scroll (800 .. 3800) */
	float windV, wind2V;          /* DAT_0087ac6c / ac70: wind tube v offsets */
	float windA;                  /* DAT_0087ac74: wind alpha (set by the camera function) */
	float windC;                  /* DAT_0087ac78: wind brightness */
	char gustOn;                  /* DAT_0087ac7c */
	int gustTimer;                /* DAT_0087ac80 */
	float lightR, lightG, lightB; /* DAT_0087ac84 / ac88 / ac8c */
	float ambient;                /* DAT_0087ac90 */
	sg_camera cam;                /* DAT_0087ac94 */
	float eye[3];                 /* DAT_0087ac98 / ac9c / aca0 */
	float look[3];                /* DAT_0087aca4 / aca8 / acac (target = eye + look) */
	int texSkin, texWind, texWind2;   /* DAT_0087acb0 / acc0 / acc8 */
	sg_model *rock, *wind, *wind2;    /* DAT_0087acb4 / acbc / acc4 */
} S;

static void load(void)
{
	S.texSkin = sg_tex_load(2, "gr_skin");
	/* FUN_004742b0("gr_hightrock") -> DAT_0087acb8; FUN_00469450(map, 16, 3000, 3000, 1000, 16, 0)
	 * (DAT_008147d8 = 3000, DAT_00814408 = 1000) */
	S.rock = sg_heightfield("gr_hightrock", 16, 3000.0f, 3000.0f, 1000.0f, 16);
	sg_color(S.rock, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_set_tex(S.rock, 0, S.texSkin, -1, -1);
	sg_fog_type(S.rock, SG_FOG_VIEW, -1);
	S.texWind = sg_tex_load(2, "gr_wind");
	S.wind = sg_sphere(350.0f, 2000.0f, 350.0f, 50, 50); /* DAT_00814be0 = 350, DAT_008145fc = 2000 */
	sg_set_tex(S.wind, 0, S.texWind, -1, -1);
	sg_fog_type(S.wind, SG_FOG_VIEW, -1);
	S.texWind2 = sg_tex_load(2, "gr_wind2");
	S.wind2 = sg_sphere(280.0f, 2000.0f, 280.0f, 50, 50); /* DAT_00814f14 = 280 */
	sg_set_tex(S.wind2, 0, S.texWind2, -1, -1);
	sg_fog_type(S.wind2, SG_FOG_VIEW, -1);
	/* DAT_0087ac94 (camera pointer) = 0 */
}

/* FUN_004c9510 (the height-map surface DAT_0087acb8 is owned by sg) */
static void unload(void)
{
	if (S.texSkin) { sg_tex_free(S.texSkin); S.texSkin = 0; }
	if (S.rock) { sg_model_free(S.rock); S.rock = 0; }
	if (S.wind) { sg_model_free(S.wind); S.wind = 0; }
	if (S.texWind) { sg_tex_free(S.texWind); S.texWind = 0; }
	if (S.wind2) { sg_model_free(S.wind2); S.wind2 = 0; }
	if (S.texWind2) { sg_tex_free(S.texWind2); S.texWind2 = 0; }
}

static void reset(void)
{
	S.frame = 0;
	S.scroll = 800.0f;            /* DAT_008149c8 */
	S.windV = 0;
	S.wind2V = 0;
	S.windA = 0;
	S.windC = 0;
	S.ac60 = 1.0f;
	S.gust = 0;
	S.gustOn = 0;
	S.gustTimer = 0;
}

static void reset_cam(void)
{
	sg_cam_init(&S.cam);
	S.eye[0] = 0;
	S.eye[1] = 600.0f;            /* DAT_008149b8 */
	S.eye[2] = 0;
	S.look[0] = 0;
	S.look[1] = 1.0f;
	S.look[2] = 300.0f;           /* DAT_008145f0 */
}

static void init(void)
{
	reset();
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	reset_cam();
}

static void reinit(void)
{
	reset();
	reset_cam();
}

static void stage_exit(void) {}

static void update(void)
{
	S.speed = 5.0f;                                   /* DAT_00813f40 */
	S.scroll += 5.0f;
	if (3800.0f < S.scroll)                           /* DAT_00814f1c */
		S.scroll -= 3000.0f;
	S.windV = (5.0f * 0.0002f + S.gust) * 0.75f + S.windV; /* DAT_00814f10, DAT_00813f3c */
	if (1.0f < S.windV)
		S.windV -= 1.0f;
	S.wind2V = (5.0f * 0.0002f + S.gust) * 1.0f + S.wind2V;
	if (1.0f < S.wind2V)
		S.wind2V -= 1.0f;
	float c = sinf(((float)(S.frame % 1500) / 1500.0f) * 6.2831855f) * 0.25f + 0.75f; /* DAT_008149d4 */
	S.windC = c * c;
	S.look[0] = sinf(((float)(S.frame % 4000) / 4000.0f) * 6.2831855f) * 200.0f; /* DAT_008147e0, DAT_008147cc */
	S.look[1] = sinf(((float)(S.frame % 3000) / 3000.0f) * 6.2831855f) * 50.0f + 50.0f; /* DAT_008140ec */
	S.look[2] = 300.0f;
	if (0 < S.frame && S.frame % 1000 == 200) {
		S.gustOn = 1;
		S.gustTimer = 500;
	}
	if (S.gustOn == 1) {
		if (S.gust < 0.005f)                          /* DAT_00813eac */
			S.gust += 2.5e-05f;                       /* DAT_00814f0c */
		if (399 < S.gustTimer && S.gustTimer % 200 == 0 && stage_g.gameMode == 2)
			stage_se(0x35);
		if (S.gustTimer < 1)
			S.gustOn = 0;
		else
			S.gustTimer--;
	} else if (0.0f < S.gust) {                       /* PTR_00853af4 = 0.0f */
		S.gust -= 2.5e-05f;
	}
	S.frame++;
}

/* FUN_004c9740 */
static void camera(void)
{
	float fov = 70.0f;                                /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 40.0f;           /* DAT_00813f4c, DAT_008140e0 */
	float eye[3] = {S.eye[0], S.eye[1], S.eye[2]};
	float at[3] = {S.eye[0] + S.look[0], S.eye[1] + S.look[1], S.eye[2] + S.look[2]};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect);
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
	/* FUN_00474240 = (float)atan2(look.z, look.x) */
	S.windA = sinf((float)atan2(S.look[2], S.look[0])) * 0.25f + 0.5f;
}

static void tube(sg_model *m, float v)
{
	sg_reset(m);
	sg_color(m, S.windA, S.windC, S.windC, S.windC, -1);
	sg_pos(m, S.eye[0], S.eye[1], S.eye[2]);
	sg_rot_x(m, sg_deg2bam(90));
	sg_uv_v(m, -v, -1);           /* XOR DAT_00853e00 (sign bit) */
	sg_render(m);
	sg_render_end(m);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004c9620 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	sg_fog(0, 2000.0f, 0xff46505a);
	/* FUN_004c96a0 */
	static const float dir[3] = {0, -1.0f, 0};        /* DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	sg_clear(0xff46505a);
	sg_reset(S.rock);
	sg_color(S.rock, 1.0f, 0.25f, 0.25f, 0.25f, -1);  /* DAT_00813f24 */
	sg_pos(S.rock, 0, 0, 4500.0f - S.scroll);         /* DAT_00814f20 */
	sg_render(S.rock);
	sg_render_end(S.rock);
	sg_reset(S.rock);
	sg_color(S.rock, 1.0f, 0.25f, 0.25f, 0.25f, -1);
	sg_pos(S.rock, 0, 0, 1500.0f - S.scroll);         /* DAT_008149d4 */
	sg_render(S.rock);
	sg_render_end(S.rock);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x16, 1);
	sg_blend(1);
	sg_fog(0, 1500.0f, 0xff000000);
	sg_blend(2);
	tube(S.wind, S.windV);
	sg_fog(0, 1800.0f, 0xff000000);                   /* DAT_00814f18 */
	tube(S.wind2, S.wind2V);
	sg_blend(1);
	sg_restore_2d();
}

const stage_def stage_gr = {"gr", "Canyon of Wind", load, unload, init, reinit, stage_exit, update, draw};
