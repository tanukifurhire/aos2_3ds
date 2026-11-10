/* Stage 6 "bf" (sky battlefield, explosions) — docs/re/stages.md §4.2.
 * loader FUN_004bffe0, unload FUN_004c04b0, init FUN_004c08e0, reinit FUN_004c0980,
 * exit FUN_004c09c0, update FUN_004c0a10, draw FUN_004c0f90
 * (camera FUN_004c0760, states FUN_004c0670, light FUN_004c06c0, flash FUN_004c0630). */
#include <math.h>
#include <string.h>

#include "stage.h"

typedef struct {
	int age;      /* +0 */
	float life;   /* +4 */
	float scale;  /* +8 */
	float x, y, z;/* +0xc */
	float y0;     /* +0x18 */
	float ang;    /* +0x1c */
	float radius; /* +0x20 */
} bf_exp;

#define BF_MAX_EXP 64
static struct {
	int frame;                    /* DAT_0087aa58 */
	float a5c, a60, a64;          /* DAT_0087aa5c / aa60 / aa64 */
	bf_exp exp[BF_MAX_EXP];       /* vector at DAT_0087aa68 */
	int nexp;
	float lightR, lightG, lightB; /* DAT_0087aa9c / aaa0 / aaa4 */
	float ambient;                /* DAT_0087aaa8 */
	sg_camera cam;                /* DAT_0087aaac */
	int texSky, texCloud, texCloud2, texExp0, texExp1;
	sg_model *sky, *cloud, *cloud2, *exp0, *exp1; /* DAT_0087aa78 / aa80 / aa88 / aa90 / aa98 */
} S;

static void load(void)
{
	S.texSky = sg_tex_load(2, "bf_sky");
	S.sky = sg_sphere(500, 400, 500, 20, 20);
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);
	S.texCloud = sg_tex_load(2, "bf_cloud");
	S.cloud = sg_sphere(400, 400, 400, 20, 20);
	sg_set_tex(S.cloud, 0, S.texCloud, -1, -1);
	S.texCloud2 = sg_tex_load(2, "bf_cloud2");
	S.cloud2 = sg_sphere(495, 400, 495, 20, 20);
	sg_set_tex(S.cloud2, 0, S.texCloud2, -1, -1);
	S.texExp0 = sg_tex_load(2, "bf_exp0");
	S.exp0 = sg_plane(50, 50);
	sg_set_tex(S.exp0, 0, S.texExp0, -1, -1);
	sg_light_type(S.exp0, SG_LIGHT_NONE, -1);
	S.texExp1 = sg_tex_load(2, "bf_exp1");
	S.exp1 = sg_plane(50, 50);
	sg_set_tex(S.exp1, 0, S.texExp1, -1, -1);
	sg_light_type(S.exp1, SG_LIGHT_NONE, -1);
}

static void init(void)
{
	S.frame = 0;
	S.a5c = S.a60 = S.a64 = 0;
	S.nexp = 0;
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.frame = 0;
	S.a5c = S.a60 = S.a64 = 0;
	S.nexp = 0;
}

static void stage_exit(void) { S.nexp = 0; }

/* FUN_004c0630 */
static void flash(void)
{
	S.lightG = 0.8f;
	S.lightB = 0.6f;
	S.ambient = 1.0f;
}

static float wrap(float a, float step)
{
	a += step;
	if (3.1415927f < a)
		a -= 6.2831855f;
	return a;
}

static void update(void)
{
	S.a5c = wrap(S.a5c, 0.0025f);
	S.a60 = wrap(S.a60, 0.0025f);
	S.a64 = wrap(S.a64, 0.00175f);
	for (int i = 0; i < S.nexp; i++) {
		bf_exp *e = &S.exp[i];
		e->life -= 0.025f;
		if (4 < e->age)
			e->scale -= 0.01f;
		e->ang -= 0.00185f;
		e->x = cosf(e->ang) * e->radius;
		e->z = sinf(e->ang) * e->radius;
		e->y = e->y0;
		if (0.0f < e->life) {
			e->age++;
		} else {
			memmove(e, e + 1, (size_t)(S.nexp - i - 1) * sizeof *e);
			S.nexp--;
			i--;
		}
	}
	if (S.lightG < 1.0f && (S.lightG += 0.002f, 1.0f < S.lightG))
		S.lightG = 1.0f;
	if (S.lightB < 1.0f && (S.lightB += 0.005f, 1.0f < S.lightB))
		S.lightB = 1.0f;
	if (0.6f < S.ambient && (S.ambient -= 0.005f, S.ambient < 0.6f))
		S.ambient = 0.6f;
	if (100 < S.frame && S.frame % 5 == 0) {
		float p = sinf(((float)(S.frame % 300) / 300.0f) * 3.1415927f) * 100.0f;
		float r = stage_rand(0, 120.0f);
		if (p <= r) {
			bf_exp e = {0};
			e.life = 1.0f;
			e.scale = 1.0f;
			e.z = -1000.0f;
			e.radius = stage_rand(100.0f, 300.0f);
			e.ang = stage_rand(-1.0f, 1.0f) + 1.5707964f;
			e.y0 = stage_rand(-200.0f, 0);
			if (S.nexp < BF_MAX_EXP)
				S.exp[S.nexp++] = e;
			if (S.frame % 100 == 0) {
				if (stage_g.gameMode == 2)
					stage_se(0x34);
				flash();
			}
		}
	}
	S.frame++;
}

/* FUN_004c0760 */
static void camera(void)
{
	float fov = 70.0f;
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 30.0f;
	float eye[3] = {0, 0, -200.0f};
	float at[3] = {0, (sinf(S.a5c) * 20.0f - 50.0f) + (float)S.frame * 0.01f, 0};
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
	/* FUN_004c0670 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	sg_light_enable(0, 1);
	/* FUN_004c06c0 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_light_dir(0, dir);
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_reset(S.sky);
	sg_pos(S.sky, 0, 0, 0);
	sg_rot_y(S.sky, sg_deg2bam(180));
	sg_render(S.sky);
	sg_render_end(S.sky);
	sg_reset(S.cloud2);
	sg_pos(S.cloud2, 0, 0, 0);
	sg_rot_y(S.cloud2, sg_rad2bam(S.a64));
	sg_render(S.cloud2);
	sg_render_end(S.cloud2);
	sg_blend(2);
	for (int i = 0; i < S.nexp; i++) {
		const bf_exp *e = &S.exp[i];
		float a = e->life * 0.5f, g = 0.5f - (1.0f - e->life);
		sg_reset(S.exp0);
		sg_scale(S.exp0, e->scale, e->scale, e->scale);
		sg_color(S.exp0, a, 1.0f, g, g, -1);
		sg_pos(S.exp0, e->x, e->y, e->z);
		sg_render(S.exp0);
		sg_render_end(S.exp0);
		sg_reset(S.exp1);
		sg_scale(S.exp1, e->scale, e->scale, e->scale);
		sg_color(S.exp1, e->life * 0.4f, 1.0f, 1.0f, 1.0f, -1);
		sg_pos(S.exp1, e->x, e->y, e->z);
		sg_render(S.exp1);
		sg_render_end(S.exp1);
	}
	sg_blend(1);
	sg_reset(S.cloud);
	sg_pos(S.cloud, 0, 0, 0);
	sg_rot_y(S.cloud, sg_rad2bam(S.a60));
	sg_render(S.cloud);
	sg_render_end(S.cloud);
	sg_restore_2d();
}

const stage_def stage_bf = {"bf", "War 10,000 Years Ago", load, NULL, init, reinit, stage_exit, update, draw};
