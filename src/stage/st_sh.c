/* Stage 19 "sh" (Sumika's Hideout) â€” build/stagedocs/sh.md.
 * loader FUN_004d3d60, unload FUN_004d4a40, init FUN_004d5310, reinit FUN_004d54c0,
 * exit FUN_004d5610, update FUN_004d5670, draw FUN_004d5890
 * (states FUN_004d4cf0, light FUN_004d4d40, camera FUN_004d4de0, particle spawn FUN_004d5060,
 * particle update FUN_004d51e0; particle std::list DAT_0087b5a8, noise vector DAT_0087b59c). */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "stage.h"

/* std::list element (0x24 bytes), same layout as stage gz */
typedef struct {
	int age;      /* +0x00 */
	float x, y, z;/* +0x04 */
	float ang;    /* +0x10 */
	float spin;   /* +0x14 (never read) */
	float radius; /* +0x18 */
	float dr;     /* +0x1c */
	float alpha;  /* +0x20 */
} sh_part;

/* std::vector element (0x1c bytes) */
typedef struct {
	float yaw;    /* +0x00 */
	float pitch;  /* +0x04 */
	float yaw2;   /* +0x08 (= yaw) */
	float scale;  /* +0x0c */
	float bright; /* +0x10 */
	int timer;    /* +0x14 */
	/* +0x18 uninitialised, unused */
} sh_noise;

#define SH_MAX_PART 256
#define SH_NNOISE 36

static struct {
	int frame;                    /* DAT_0087b520 */
	float camYaw;                 /* DAT_0087b524 */
	float camBob;                 /* DAT_0087b528 */
	float roll;                   /* DAT_0087b52c */
	float lightR, lightG, lightB; /* DAT_0087b588 / b58c / b590 */
	float ambient;                /* DAT_0087b594 */
	sg_camera cam;                /* DAT_0087b598 */
	sh_noise noise[SH_NNOISE];    /* vector DAT_0087b59c */
	int nnoise;
	sh_part part[SH_MAX_PART];    /* list DAT_0087b5a8 */
	int npart;
	int texWall, texSph, texSph2, texSphB, texAtmos, texNoise[4], texRoll, texPart;
	sg_model *wall;   /* DAT_0087b534 */
	sg_model *sph;    /* DAT_0087b538 */
	sg_model *sph2;   /* DAT_0087b540 */
	sg_model *sphB;   /* DAT_0087b548 */
	sg_model *atmos;  /* DAT_0087b550 */
	sg_model *noiseM[4]; /* DAT_0087b568..b574 */
	sg_model *stripe; /* DAT_0087b560 */
	sg_particles *parts; /* DAT_0087b558 */
} S;

static void load(void)
{
	S.texWall = sg_tex_load(3, "sh_wall");
	S.wall = sg_sphere(400.0f, 400.0f, 400.0f, 20, 100); /* DAT_008145f4 */
	sg_set_tex(S.wall, 0, S.texWall, -1, -1);
	sg_light_type(S.wall, SG_LIGHT_WHITE, -1);
	sg_fog_type(S.wall, SG_FOG_NONE, -1);
	S.texSph = sg_tex_load(3, "sh_shpere");
	S.sph = sg_sphere(80.0f, 80.0f, 80.0f, 40, 40); /* DAT_00814164 */
	sg_set_tex(S.sph, 0, S.texSph, -1, -1);
	S.texSph2 = sg_tex_load(3, "sh_shpere2");
	S.sph2 = sg_sphere(80.0f, 80.0f, 80.0f, 40, 40);
	sg_set_tex(S.sph2, 0, S.texSph2, -1, -1);
	S.texSphB = sg_tex_load(3, "sh_shpereb");
	S.sphB = sg_sphere(80.0f, 80.0f, 80.0f, 40, 40);
	sg_set_tex(S.sphB, 0, S.texSphB, -1, -1);
	S.texAtmos = sg_tex_load(3, "sh_atmos");
	S.atmos = sg_sphere(80.0f, 80.0f, 80.0f, 40, 40);
	sg_set_tex(S.atmos, 0, S.texAtmos, -1, -1);
	static const char *const noiseName[4] = {"sp_noise00", "sp_noise01", "sp_noise02", "sp_noise03"};
	for (int i = 0; i < 4; i++) {
		S.texNoise[i] = sg_tex_load(2, noiseName[i]);
		/* noise00 is a 2x2 grid_xy, 01..03 are planes (all 40x40, DAT_008140e0) */
		S.noiseM[i] = i == 0 ? sg_grid_xy(40.0f, 40.0f, 2, 2, 1.0f) : sg_plane(40.0f, 40.0f);
		sg_set_tex(S.noiseM[i], 0, S.texNoise[i], -1, -1);
		sg_light_type(S.noiseM[i], SG_LIGHT_NONE, -1);
		sg_fog_type(S.noiseM[i], SG_FOG_NONE, -1);
	}
	S.texRoll = sg_tex_load(2, "sl_roll");
	S.stripe = sg_xfile("stripe"); /* "mdl\\stripe.x" */
	sg_set_tex(S.stripe, 0, S.texRoll, -1, -1);
	S.texPart = sg_tex_load(3, "gz_particle");
	S.parts = sg_particles_new(1000); /* FUN_00475660(1000, 2) */
	sg_particles_tex(S.parts, S.texPart);
}

/* body shared by init FUN_004d5310 and reinit FUN_004d54c0 */
static void reset(void)
{
	S.frame = 0;
	S.camYaw = 0;
	S.camBob = 0;
	S.roll = 0;
	S.nnoise = 0; /* FUN_004a9f10 */
	for (int i = 0; i < SH_NNOISE; i++) {
		sh_noise n;
		n.yaw = ((float)i / 36.0f) * 6.2831855f;                 /* DAT_008140f4 */
		n.pitch = stage_rand(-0.15f, 0.15f) + -0.25f;             /* DAT_00815dc4, DAT_00813f20, DAT_0081411c */
		n.yaw2 = ((float)i / 36.0f) * 6.2831855f;
		n.scale = stage_rand(-0.25f, 0.25f) + 1.25f;              /* DAT_0081411c, DAT_00813f24, DAT_00815dbc */
		n.bright = 0;
		n.timer = i * 180;
		S.noise[S.nnoise++] = n;
	}
	S.npart = 0; /* FUN_004cbed0 */
}

static void init(void)
{
	reset();
	S.lightR = S.lightG = S.lightB = 1.0f;
	S.ambient = 0.75f; /* DAT_00813f3c */
	sg_cam_init(&S.cam);
}

static void reinit(void) { reset(); }

static void stage_exit(void)
{
	S.nnoise = 0;
	S.npart = 0;
}

/* FUN_004d5060 (= FUN_004cad10 of gz with y = -350) */
static void spawn(void)
{
	float r = stage_rand(50.0f, 200.0f);
	float a = stage_rand(-3.1415927f, 3.1415927f);
	sh_part p;
	p.age = 0;
	p.x = cosf(a) * r;
	p.y = -350.0f; /* DAT_00815dc8 */
	p.z = sinf(a) * r;
	p.ang = stage_rand(-3.1415927f, 3.1415927f);
	p.spin = 0.05f;
	if (stage_randi(0, 100) < 50)
		p.spin *= -1.0f;
	p.radius = 0;
	p.dr = stage_rand(1.0f, 10.0f) * 0.1f;
	p.alpha = 1.0f;
	if (S.npart < SH_MAX_PART)
		S.part[S.npart++] = p;
}

/* FUN_004d51e0 */
static void parts_update(void)
{
	for (int i = 0; i < S.npart; i++) {
		sh_part *p = &S.part[i];
		p->age++;
		p->radius += p->dr;
		p->y += 1.0f;
		if (30 < p->age)
			p->alpha -= 0.01f;
		if (!(0.0f < p->alpha)) {
			memmove(p, p + 1, (size_t)(S.npart - i - 1) * sizeof *p);
			S.npart--;
			i--;
		}
	}
}

static void update(void)
{
	S.camYaw += 0.0025f; /* DAT_008145e0 */
	if (3.1415927f < S.camYaw)
		S.camYaw -= 6.2831855f;
	S.camBob += 0.01f; /* DAT_00813ec8 */
	if (3.1415927f < S.camBob)
		S.camBob -= 6.2831855f;
	parts_update();
	if (S.frame % 5 == 0)
		spawn();
	S.roll += 0.0025f;
	if (1.0f < S.roll)
		S.roll -= 1.0f;
	for (int i = 0; i < S.nnoise; i++) {
		sh_noise *n = &S.noise[i];
		n->timer++;
		if (n->timer % 500 < 350) {
			n->bright -= 0.01f;
			if (n->bright < 0.0f)
				n->bright = 0;
		} else {
			n->bright += 0.01f;
			if (1.0f < n->bright)
				n->bright = 1.0f;
		}
	}
	S.frame++;
}

/* FUN_004d4de0 */
static void camera(void)
{
	float fov;
	if (stage_g.gameMode == 2)
		fov = 15.0f * stage_g.zoom + 50.0f; /* DAT_00853b88, DAT_008140ec */
	else
		fov = 80.0f; /* DAT_00814164 */
	float eye[3] = {cosf(S.camYaw) * 300.0f, sinf(S.camBob) * 80.0f + 180.0f, sinf(S.camYaw) * 300.0f}; /* DAT_008145f0, DAT_00814164, DAT_00815258 */
	float at[3] = {0, 0, 0};
	sg_cam_persp(&S.cam, fov * 0.64f, 10.0f, 2000.0f, stage_g.aspect); /* DAT_008145e4, DAT_00813f4c, DAT_008145fc */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	if (stage_g.gameMode == 2) {
		float s = sinf(((float)(S.frame % 2700) / 2700.0f) * 6.2831855f); /* DAT_008149dc */
		sg_cam_rot_z(&S.cam, sg_rad2bam(s * 0.1f));                       /* DAT_00853b10 */
		s = sinf(((float)(S.frame % 1300) / 1300.0f) * 6.2831855f);       /* DAT_008147d0 */
		sg_cam_rot_y(&S.cam, sg_rad2bam(s * 0.15f));                      /* DAT_00813f20 */
	}
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

/* CModel vtable +0x64 (FUN_0046d590) with up = NULL: billboard at p facing `at`. */
static void model_orient(sg_model *m, const float p[3], const float at[3])
{
	float u[3] = {0, 1.0f, 0};
	float f[3] = {at[0] - p[0], at[1] - p[1], at[2] - p[2]};
	float l = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
	if (l == 0) { /* original: division by zero */
		memcpy(m->pos, p, sizeof m->pos);
		return;
	}
	l = 1.0f / sqrtf(l);
	f[0] *= l;
	f[1] *= l;
	f[2] *= l;
	if (f[0] == 0 && f[2] == 0 && (f[1] == 1.0f || f[1] == -1.0f)) {
		u[1] = 0;
		u[2] = -f[1];
	}
	float s[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
	l = 1.0f / sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	s[0] *= -l;
	s[1] *= -l;
	s[2] *= -l;
	float y[3] = {s[2] * f[1] - s[1] * f[2], s[0] * f[2] - s[2] * f[0], s[1] * f[0] - s[0] * f[1]};
	l = 1.0f / sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	for (int k = 0; k < 3; k++) {
		m->rot[0][k] = s[k];
		m->rot[1][k] = y[k] * l;
		m->rot[2][k] = f[k];
		m->pos[k] = p[k];
	}
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera();
	/* FUN_004d4cf0 */
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 0);
	sg_light_enable(0, 1);
	/* FUN_004d4d40 */
	static const float dir[3] = {0, -1.0f, 0};
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);

	sg_blend(1);
	sg_clear(0xff5ab0ff);
	sg_rs(7, 0);
	sg_rs(0xe, 0);
	/* scrolling stripe ring */
	sg_reset(S.stripe);
	sg_pos(S.stripe, 0, -400.0f, 0); /* DAT_00815bc8 */
	sg_color(S.stripe, 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_uv_v(S.stripe, -S.roll, -1);
	sg_render(S.stripe);
	sg_render_end(S.stripe);
	/* outer wall sphere */
	sg_reset(S.wall);
	sg_pos(S.wall, 0, 0, 0);
	sg_render(S.wall);
	sg_render_end(S.wall);

	/* 36 noise billboards on a radius-460 ring, model chosen by (i + frame) % 4 */
	static const float origin[3] = {0, 0, 0};
	for (int i = 0; i < S.nnoise; i++) {
		const sh_noise *n = &S.noise[i];
		sg_model *m = S.noiseM[(i + S.frame) % 4];
		float p[3] = {cosf(n->yaw) * 460.0f, sinf(n->pitch) * 460.0f, sinf(n->yaw2) * 460.0f}; /* DAT_00815dc0 */
		sg_reset(m);
		sg_scale(m, n->scale, n->scale, n->scale);
		float c = n->bright * 0.5f + 0.25f; /* DAT_00853b1c, DAT_00813f24 */
		sg_color(m, 0.75f, c, c, c, -1);
		sg_pos(m, p[0], p[1], p[2]);
		model_orient(m, p, origin);
		sg_render(m);
		sg_render_end(m);
	}

	sg_rs(7, 0);
	sg_rs(0xe, 0);
	sg_rs(0x1c, 0);
	/* FUN_004676b0(0, 0): the enable flag (DAT_00e25d70) is never read by the vertex-shader
	 * path — FUN_00472480 uploads light 0's colour unconditionally — so the planet layers stay
	 * lit in the original. sg zeroes the light colour of a disabled light, so the call is
	 * omitted here to keep the original result (see sh.md). */
	/* planet: black base (alpha blend), then two cross-fading additive layers + atmosphere */
	sg_reset(S.sphB);
	sg_color(S.sphB, 1.0f, 0, 0, 0, -1);
	sg_pos(S.sphB, 0, -100.0f, 0); /* DAT_00853bb8 */
	sg_render(S.sphB);
	sg_render_end(S.sphB);
	sg_blend(2);
	float k = sinf(((float)(S.frame % 800) / 800.0f) * 6.2831855f) * 0.5f + 0.5f; /* DAT_008149c8 */
	sg_reset(S.sph);
	sg_color(S.sph, k, 0.75f, 0.75f, 1.0f, -1);
	sg_pos(S.sph, 0, -100.0f, 0);
	sg_render(S.sph);
	sg_render_end(S.sph);
	sg_reset(S.sph2);
	sg_color(S.sph2, 1.0f - k, 1.0f, 0.75f, 0.75f, -1);
	sg_pos(S.sph2, 0, -100.0f, 0);
	sg_render(S.sph2);
	sg_render_end(S.sph2);
	sg_reset(S.atmos);
	sg_color(S.atmos, 0.5f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.atmos, 0, -100.0f, 0);
	sg_render(S.atmos);
	sg_render_end(S.atmos);

	sg_particles_begin(S.parts);
	static const int rect[4] = {0, 0, 63, 63};
	for (int i = 0; i < 1000 && i < S.npart; i++) {
		const sh_part *p = &S.part[i];
		int a = (int)(255.0f * p->alpha); /* DAT_00853b94 */
		float x = cosf(p->ang) * p->radius + p->x;
		float z = sinf(p->ang) * p->radius + p->z;
		sg_particles_add(S.parts, x, p->y, z, 10.0f, 10.0f, rect, (uint32_t)a << 24 | 0xffffff);
	}
	/* FUN_00475b10 applies the CPerticle kind (2) as blend mode itself; already 2 here. */
	sg_blend(2);
	sg_particles_end(S.parts);
	sg_restore_2d();
}

const stage_def stage_sh = {"sh", "Sumika's Hideout", load, NULL, init, reinit, stage_exit, update, draw};
