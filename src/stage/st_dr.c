/* Stage 8 "dr" (Dust Storm) — build/stagedocs/dr.md.
 * loader FUN_004c7110, unload FUN_004c7890, init FUN_004c7ca0, reinit FUN_004c7d70,
 * exit FUN_004c7de0, update FUN_004c7e30, draw FUN_004c8080
 * (camera FUN_004c7b80, states FUN_004c7a60, light FUN_004c7ae0). */
#include <math.h>
#include <stddef.h>

#include "stage.h"

static struct {
	int frame;                    /* DAT_0087abb0 */
	float swayPh;                 /* DAT_0087abb4: sky/wind orbit radius phase (rad) */
	float orbit;                  /* DAT_0087abb8: camera orbit angle (rad) */
	float radius;                 /* _DAT_0087abbc */
	float height;                 /* DAT_0087abc0 */
	float windV, tornV;           /* DAT_0087abc4 / abc8: v scroll */
	float tornA;                  /* DAT_0087abcc: tornado fade 0..1 */
	float px, py, pz;             /* DAT_0087abd0 / abd4 / abd8: orbit point (camera eye) */
	float lightR, lightG, lightB; /* DAT_0087ac0c / ac10 / ac14 */
	float ambient;                /* DAT_0087ac18 */
	sg_camera cam;                /* DAT_0087ac1c */
	int texSand, texRock, texSky, texWind, texTorn; /* DAT_0087abdc / abe8 / abfc / abf4 / ac04 */
	sg_model *sand, *rock, *sky, *wind, *torn;      /* DAT_0087abe0 / abec / ac00 / abf8 / ac08 */
} S;

static void load(void)
{
	/* FUN_004742b0("dr_hightsand") -> DAT_0087abe4; FUN_00469450(map, 16, 2000, 2000, 250, 10, 0) */
	S.sand = sg_heightfield("dr_hightsand", 16, 2000.0f, 2000.0f, 250.0f, 10); /* DAT_008145fc, DAT_008149ac */
	sg_color(S.sand, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	S.texSand = sg_tex_load(2, "dr_sand");
	sg_set_tex(S.sand, 0, S.texSand, -1, -1);
	sg_fog_type(S.sand, SG_FOG_VIEW, -1);
	/* FUN_004742b0("dr_hightrock") -> DAT_0087abf0; FUN_00469450(map, 8, 2000, 2000, 350, 20, 0) */
	S.rock = sg_heightfield("dr_hightrock", 8, 2000.0f, 2000.0f, 350.0f, 20); /* DAT_00814be0 */
	sg_color(S.rock, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	S.texRock = sg_tex_load(2, "dr_rock");
	sg_set_tex(S.rock, 0, S.texRock, -1, -1);
	sg_fog_type(S.rock, SG_FOG_VIEW, -1);
	S.texSky = sg_tex_load(2, "dr_sky");
	S.sky = sg_sphere(1500.0f, 1200.0f, 1500.0f, 100, 100); /* DAT_008149d4, DAT_008149d0 */
	sg_fog_type(S.sky, SG_FOG_NONE, -1);
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);
	sg_light_type(S.sky, SG_LIGHT_NONE, -1);
	S.texWind = sg_tex_load(2, "dr_wind");
	S.wind = sg_sphere(300.0f, 2000.0f, 300.0f, 50, 50); /* DAT_008145f0, DAT_008145fc */
	sg_fog_type(S.wind, SG_FOG_VIEW, -1);
	sg_set_tex(S.wind, 0, S.texWind, -1, -1);
	sg_fog_type(S.wind, SG_FOG_NONE, -1);
	S.texTorn = sg_tex_load(2, "dr_tornade");
	S.torn = sg_sphere(300.0f, 2000.0f, 300.0f, 50, 50);
	sg_fog_type(S.torn, SG_FOG_NONE, -1);
	sg_set_tex(S.torn, 0, S.texTorn, -1, -1);
	sg_fog_type(S.torn, SG_FOG_NONE, -1);
}

/* FUN_004c7890 (the height-map surfaces DAT_0087abe4 / abf0 are owned by sg) */
static void unload(void)
{
	if (S.sand) { sg_model_free(S.sand); S.sand = 0; }
	if (S.texSand) { sg_tex_free(S.texSand); S.texSand = 0; }
	if (S.rock) { sg_model_free(S.rock); S.rock = 0; }
	if (S.texRock) { sg_tex_free(S.texRock); S.texRock = 0; }
	if (S.wind) { sg_model_free(S.wind); S.wind = 0; }
	if (S.texWind) { sg_tex_free(S.texWind); S.texWind = 0; }
	if (S.sky) { sg_model_free(S.sky); S.sky = 0; }
	if (S.texSky) { sg_tex_free(S.texSky); S.texSky = 0; }
	if (S.torn) { sg_model_free(S.torn); S.torn = 0; }
	if (S.texTorn) { sg_tex_free(S.texTorn); S.texTorn = 0; }
}

/* (px, py, pz are NOT reset: the first draw before any update uses the previous values) */
static void reinit(void)
{
	S.frame = 0;
	S.swayPh = 0;
	S.orbit = 0;
	S.radius = 1900.0f;           /* DAT_00814be8 */
	S.height = 800.0f;            /* DAT_008149c8 */
	S.windV = 0;
	S.tornV = 0;
	S.tornA = 0;
}

static void init(void)
{
	reinit();
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void stage_exit(void) {}

static void orbit_point(void)
{
	S.px = cosf(S.orbit) * S.radius;
	S.pz = sinf(S.orbit) * S.radius;
	S.py = S.height;
}

static void update(void)
{
	S.orbit += 0.002f;                /* _DAT_008145dc */
	if (3.1415927f < S.orbit)
		S.orbit -= 6.2831855f;
	S.swayPh += 0.00125f;             /* _DAT_00814bdc */
	if (3.1415927f < S.swayPh)
		S.swayPh -= 6.2831855f;
	S.height = 1000.0f;               /* DAT_00814408 */
	S.radius = 1200.0f;               /* DAT_008149d0 */
	orbit_point();
	S.windV += 0.00075f;              /* _DAT_00814bd4 */
	if (1.0f < S.windV)
		S.windV -= 1.0f;
	S.tornV += 0.001f;                /* DAT_00814bd8 */
	if (1.0f < S.tornV)
		S.tornV -= 1.0f;
	if (S.frame % 2000 < 0x641) {
		S.tornA -= 0.0025f;           /* DAT_008145e0 */
		if (S.tornA < 0.0f)
			S.tornA = 0.0f;
	} else {
		if (S.frame % 2000 == 0x64a && stage_g.gameMode == 2)
			stage_se(0x37);
		S.tornA += 0.0025f;
		if (1.0f < S.tornA)
			S.tornA = 1.0f;
	}
	S.frame++;
}

/* FUN_004c7b80: fov 40 (DAT_008140e0) * 0.64, near 50 (DAT_008140ec), far 3000 (DAT_008147d8);
 * eye = orbit point, target = origin; no zoom dependence. */
static void camera(void)
{
	float eye[3] = {S.px, S.py, S.pz};
	float at[3] = {0, 0, 0};
	sg_cam_persp(&S.cam, 40.0f * 0.64f, 50.0f, 3000.0f, stage_g.aspect);
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
	/* FUN_004c7a60 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	sg_fog(50.0f, 1500.0f, 0xff646450);  /* DAT_008140ec, DAT_008149d4 */
	/* FUN_004c7ae0 */
	static const float dir[3] = {0, -1.0f, 0}; /* DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	sg_clear(0xff646450);
	sg_blend(1);
	sg_rs(0x16, 3);
	sg_reset(S.sand);
	sg_pos(S.sand, 0, 0, 0);
	sg_render(S.sand);
	sg_render_end(S.sand);
	sg_reset(S.rock);
	sg_pos(S.rock, 0, 0, 0);
	sg_render(S.rock);
	sg_render_end(S.rock);
	sg_rs(0x16, 1);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	sg_blend(2);
	S.height = cosf(S.swayPh) * 100.0f + 400.0f;   /* DAT_00853b90, DAT_008145f4 */
	S.radius = cosf(S.swayPh) * 300.0f + 1600.0f;  /* DAT_008145f0, DAT_00814be4 */
	orbit_point();
	sg_reset(S.sky);
	sg_color(S.sky, sinf(S.orbit) * 0.15f + 0.85f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00813f20, DAT_00814120 */
	sg_pos(S.sky, S.px, S.py, S.pz);
	sg_render(S.sky);
	sg_render_end(S.sky);
	sg_reset(S.wind);
	sg_color(S.wind, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.wind, S.px, S.py, S.pz);
	sg_rot_y(S.wind, sg_rad2bam(-S.orbit));        /* XOR DAT_00853e00 */
	sg_rot_x(S.wind, sg_deg2bam(90));
	sg_uv_v(S.wind, S.windV, -1);
	sg_render(S.wind);
	sg_render_end(S.wind);
	S.height = 1000.0f;
	S.radius = 1200.0f;
	orbit_point();
	sg_blend(1);
	sg_reset(S.torn);
	sg_color(S.torn, S.tornA * 0.75f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00813f3c */
	sg_pos(S.torn, S.px, S.py, S.pz);
	sg_rot_y(S.torn, sg_rad2bam(-S.orbit));
	sg_rot_x(S.torn, sg_deg2bam(90));
	sg_uv_v(S.torn, -S.tornV, -1);
	sg_render(S.torn);
	sg_render_end(S.torn);
	sg_restore_2d();
}

const stage_def stage_dr = {"dr", "Dust Storm", load, unload, init, reinit, stage_exit, update, draw};
