/* Stage 16 "gz" (Crashed Spaceship) — build/stagedocs/gz.md.
 * loader FUN_004ca360, unload FUN_004ca870, init FUN_004cafc0, reinit FUN_004cb040,
 * exit FUN_004cb060, update FUN_004cb0b0, draw FUN_004cb0f0
 * (camera FUN_004cab60, states FUN_004caa20, light FUN_004caaa0, particle update FUN_004cae90,
 * particle spawn FUN_004cad10, particle list std::list at DAT_0087ad14: clear FUN_004cbed0,
 * push_back FUN_004cbf80, erase FUN_004cbf30). */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "stage.h"

/* std::list element (0x24 bytes), layout from FUN_004cad10 / FUN_004cae90 / FUN_004cb0f0 */
typedef struct {
	int age;      /* +0x00 */
	float x;      /* +0x04 */
	float y;      /* +0x08 */
	float z;      /* +0x0c */
	float ang;    /* +0x10 */
	float spin;   /* +0x14 (+-0.05, never read) */
	float radius; /* +0x18 */
	float dr;     /* +0x1c */
	float alpha;  /* +0x20 */
} gz_part;

/* Spawned every 5 frames, lives ~131 frames -> at most ~27 alive. */
#define GZ_MAX_PART 256

static struct {
	int frame;                    /* DAT_0087acd0 */
	float lightR, lightG, lightB; /* DAT_0087ad00 / ad04 / ad08 */
	float ambient;                /* DAT_0087ad0c */
	sg_camera cam;                /* DAT_0087ad10 */
	gz_part part[GZ_MAX_PART];    /* list at DAT_0087ad14 */
	int npart;
	int texSky, texOrb, texPart, texForest, texBase; /* DAT_0087acd4 / acec / acf8 / acdc / ace4 */
	sg_model *sky, *orb, *forest, *base, *ship;      /* DAT_0087acd8 / acf0 / ace0 / ace8 / acfc */
	sg_particles *parts;          /* DAT_0087acf4 */
} S;

static void load(void)
{
	S.texSky = sg_tex_load(3, "gz_sky");
	S.sky = sg_grid_xy(2000.0f, 1000.0f, 20, 20, 1.0f); /* DAT_008145fc, DAT_00814408 */
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);
	S.texOrb = sg_tex_load(3, "gz_particle");
	S.orb = sg_grid_xz(1000.0f, 1000.0f, 4, 4, 1.0f);
	sg_set_tex(S.orb, 0, S.texOrb, -1, -1);
	S.texPart = sg_tex_load(3, "gz_particle");
	S.parts = sg_particles_new(1000); /* FUN_00475660(1000, 2) */
	sg_particles_tex(S.parts, S.texPart);
	S.texForest = sg_tex_load(3, "gz_forest");
	S.forest = sg_grid_xz(1200.0f, 1200.0f, 20, 20, 1.0f); /* DAT_008149d0 */
	sg_set_tex(S.forest, 0, S.texForest, -1, -1);
	sg_fog_type(S.forest, SG_FOG_VIEW, -1);
	S.texBase = sg_tex_load(3, "gz_base");
	S.base = sg_grid_xz(1200.0f, 1200.0f, 20, 20, 1.0f);
	sg_set_tex(S.base, 0, S.texBase, -1, -1);
	sg_fog_type(S.base, SG_FOG_NONE, -1);
	S.ship = sg_xfile("ship"); /* "mdl\\ship.x", no texture */
	sg_fog_type(S.ship, SG_FOG_VIEW, -1);
}

static void init(void)
{
	S.frame = 0;
	S.npart = 0; /* FUN_004cbed0 */
	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	sg_cam_init(&S.cam);
}

static void reinit(void)
{
	S.frame = 0;
	S.npart = 0;
}

static void stage_exit(void) { S.npart = 0; }

/* FUN_004cad10 */
static void spawn(void)
{
	float r = stage_rand(50.0f, 200.0f);             /* DAT_008140ec, DAT_008147cc */
	float a = stage_rand(-3.1415927f, 3.1415927f);   /* DAT_00814100, DAT_00853b58 */
	gz_part p;
	p.age = 0;
	p.x = cosf(a) * r;
	p.y = -50.0f; /* DAT_008149e4 */
	p.z = sinf(a) * r;
	p.ang = stage_rand(-3.1415927f, 3.1415927f);
	p.spin = 0.05f; /* DAT_00813ef4 */
	if (stage_randi(0, 100) < 50)
		p.spin *= -1.0f;
	p.radius = 0;
	p.dr = stage_rand(1.0f, 10.0f) * 0.1f; /* DAT_00853b10 */
	p.alpha = 1.0f;
	if (S.npart < GZ_MAX_PART)
		S.part[S.npart++] = p;
}

/* FUN_004cae90 */
static void parts_update(void)
{
	for (int i = 0; i < S.npart; i++) {
		gz_part *p = &S.part[i];
		p->age++;
		p->radius += p->dr;
		p->y += 1.0f;
		if (30 < p->age)
			p->alpha -= 0.01f; /* DAT_00813ec8 */
		if (!(0.0f < p->alpha)) {
			memmove(p, p + 1, (size_t)(S.npart - i - 1) * sizeof *p);
			S.npart--;
			i--;
		}
	}
}

static void update(void)
{
	parts_update();
	if (S.frame % 5 == 0)
		spawn();
	S.frame++;
}

/* FUN_004cab60 */
static void camera(void)
{
	float fov = 70.0f; /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 30.0f; /* DAT_00813f4c, DAT_008140e8 */
	float eye[3] = {0, 750.0f, -750.0f};    /* DAT_008149c4, DAT_00814a10 */
	float at[3] = {0, 0, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2500.0f, stage_g.aspect); /* DAT_008145e4, DAT_008147d4 */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	float s = sinf(((float)(S.frame % 3300) / 3300.0f) * 6.2831855f); /* DAT_008147dc */
	sg_cam_rot_x(&S.cam, sg_rad2bam(s * 0.15f + -0.25f)); /* DAT_00813f20, DAT_0081411c */
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

/* FUN_004caa20 */
static void states(void)
{
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	sg_fog(1000.0f, 1500.0f, 0xff000000); /* DAT_00814408, DAT_008149d4 */
}

/* FUN_004caaa0 */
static void light(int up)
{
	float dir[3] = {0, up == 1 ? 1.0f : -1.0f, 0};
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	states();
	light(0);
	sg_clear(0xff000000);
	sg_blend(1);
	sg_reset(S.sky);
	sg_pos(S.sky, 0, -200.0f, 1000.0f); /* DAT_00814600, DAT_00814408 */
	sg_render(S.sky);
	sg_render_end(S.sky);

	/* ship lit from below with a blue light and a pulsing ambient */
	S.lightR = 0.54901963f; /* DAT_008150d0 */
	S.lightG = 0.74509805f; /* DAT_008150d4 */
	S.lightB = 1.0f;
	S.ambient = sinf(((float)(S.frame % 500) / 500.0f) * 6.2831855f) * 0.25f + 0.35f; /* DAT_00814140, 00813f24, 00813f2c */
	light(1);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x16, 3);
	sg_reset(S.ship);
	sg_color(S.ship, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_scale(S.ship, 5.0f, 5.0f, 5.0f); /* DAT_00813f40 */
	/* FUN_0045a0f0(frame % 3600) / 10: 0.1 degree per frame, int BAM (not masked) */
	sg_rot_y(S.ship, (uint32_t)(((S.frame % 3600) * 0x10000 / 360) / 10));
	sg_pos(S.ship, 0, sinf(((float)(S.frame % 400) / 400.0f) * 6.2831855f) * 5.0f + 50.0f, 0); /* DAT_008145f4 */
	sg_render(S.ship);
	sg_render_end(S.ship);

	S.lightR = S.lightG = S.lightB = S.ambient = 1.0f;
	light(0);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x16, 3);
	sg_blend(2);
	/* four glow sprites (1000x1000 grid scaled 0.03 = 30x30) circling the ship */
	for (int i = 0; i < 4; i++) {
		sg_reset(S.orb);
		float a = (-(float)((i * 900 + S.frame) % 3600) / 3600.0f) * 6.2831855f; /* DAT_00853e00 negate, DAT_008150d8 */
		float s = sinf(a), c = cosf(a);
		sg_pos(S.orb, c * 120.0f, 60.0f, s * 120.0f); /* DAT_00814174, DAT_00813a58 */
		sg_scale(S.orb, 0.03f, 0.03f, 0.03f);        /* DAT_00813ee0 */
		sg_color(S.orb, 1.0f, 0.5f, 1.0f, 0.75f, -1); /* a, r, g, b */
		sg_render(S.orb);
		sg_render_end(S.orb);
	}
	sg_rs(7, 1);
	/* ground (still additive) */
	sg_reset(S.base);
	sg_color(S.base, sinf(((float)(S.frame % 500) / 500.0f) * 6.2831855f) * 0.25f + 0.75f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.base, 0, -100.0f, 0); /* DAT_00853bb8 */
	sg_render(S.base);
	sg_render_end(S.base);

	sg_blend(1);
	sg_rs(7, 1);
	sg_rs(0xe, 0);
	sg_rs(0x16, 3);
	/* 11 stacked forest layers */
	for (int i = 10; -1 < i; i--) {
		sg_reset(S.forest);
		sg_color(S.forest, (float)i * 0.04f + 0.125f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00813eec, DAT_00813f1c */
		float sc = 1.0f - (float)i * 0.065f;                               /* DAT_008150cc */
		sg_scale(S.forest, sc, sc, sc);
		sg_pos(S.forest, 0, -50.0f - (float)i * 5.0f, 0);
		sg_rot_y(S.forest, sg_deg2bam((i * 36) % 360));
		sg_render(S.forest);
		sg_render_end(S.forest);
	}

	sg_particles_begin(S.parts);
	static const int rect[4] = {0, 0, 63, 63}; /* SetRect(0, 0, 0x3f, 0x3f) */
	for (int i = 0; i < 1000 && i < S.npart; i++) {
		const gz_part *p = &S.part[i];
		int a = (int)(255.0f * p->alpha); /* DAT_00853b94 */
		float x = cosf(p->ang) * p->radius + p->x;
		float z = sinf(p->ang) * p->radius + p->z;
		sg_particles_add(S.parts, x, p->y, z, 10.0f, 10.0f, rect, (uint32_t)a << 24 | 0xffffff);
	}
	/* FUN_00475b10 calls FUN_004637d0(kind) itself; kind = 2 (FUN_00475660(1000, 2)).
	 * sg_particles_end does not apply the kind, so set it here (same global effect). */
	sg_blend(2);
	sg_particles_end(S.parts);
	sg_restore_2d();
}

const stage_def stage_gz = {"gz", "Crashed Spaceship", load, NULL, init, reinit, stage_exit, update, draw};
