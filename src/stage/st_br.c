/* Stage 11 "br" (Big Bridge) — build/stagedocs/br.md.
 * loader FUN_004c1ff0, unload FUN_004c2710, init FUN_004c2db0, reinit FUN_004c2ed0,
 * exit FUN_004c2e80, update FUN_004c2f30, draw FUN_004c2fb0
 * (camera FUN_004c2ab0, states FUN_004c2990, light FUN_004c2a10,
 *  reflection/clip setup FUN_0046a0a0, depth surface FUN_004c2930 / FUN_004c2960). */
#include <math.h>
#include <string.h>

#include "stage.h"

static struct {
	int frame;                 /* DAT_0087aab0 */
	float skyRot;              /* DAT_0087aab4 (never written by the game: always 0) */
	float eyeX, eyeY, eyeZ;    /* DAT_0087aaf0 / aaf4 / aaf8 */
	float dirX, dirY, dirZ;    /* DAT_0087aafc / ab00 / ab04 (look-at = eye + dir) */
	float lightR, lightG, lightB; /* DAT_0087ab0c / ab10 / ab14 */
	float ambient;             /* DAT_0087ab18 */
	sg_camera cam;             /* DAT_0087ab1c */
	sg_model *bridge, *bridgeR;/* DAT_0087aab8 / aabc */
	sg_model *sky, *fog, *sea, *shadow, *lamp, *lamp2; /* aac0 / aac8 / aad0 / aad8 / aae0 / aae8 */
	int texSky, texFog, texSea, texShadow, texLamp, texLamp2; /* aac4 / aacc / aad4 / aadc / aae4 / aaec */
} S;

static void load(void)
{
	S.bridge = sg_xfile("bridge");
	sg_fog_type(S.bridge, SG_FOG_VIEW, -1);
	S.bridgeR = sg_xfile("bridger");
	sg_fog_type(S.bridgeR, SG_FOG_VIEW, -1);

	S.texSky = sg_tex_load(2, "bg_sky");
	S.sky = sg_sphere(3000.0f, 3000.0f, 3000.0f, 100, 100); /* DAT_008147d8 */
	sg_fog_type(S.sky, SG_FOG_NONE, -1);
	sg_set_tex(S.sky, 0, S.texSky, -1, -1);

	/* loaded but never drawn */
	S.texFog = sg_tex_load(2, "bg_fog");
	S.fog = sg_grid_xz(4000.0f, 4000.0f, 50, 50, 10.0f); /* DAT_008147e0, DAT_00813f4c */
	sg_set_tex(S.fog, 0, S.texFog, -1, -1);
	sg_fog_type(S.fog, SG_FOG_VIEW, -1);

	S.texSea = sg_tex_load(2, "bg_sea");
	S.sea = sg_grid_xz(8000.0f, 8000.0f, 50, 50, 20.0f); /* DAT_008147ec, DAT_008140fc */
	sg_set_tex(S.sea, 0, S.texSea, -1, -1);
	sg_fog_type(S.sea, SG_FOG_VIEW, -1);

	S.texShadow = sg_tex_load(2, "bg_shadow");
	S.shadow = sg_grid_xz(2000.0f, 2000.0f, 10, 10, 1.0f); /* DAT_008145fc, DAT_00853b34 */
	sg_set_tex(S.shadow, 0, S.texShadow, -1, -1);
	sg_fog_type(S.shadow, SG_FOG_VIEW, -1);

	S.texLamp = sg_tex_load(2, "bg_lamp");
	S.lamp = sg_plane(256.0f, 256.0f); /* DAT_00814160 */
	sg_set_tex(S.lamp, 0, S.texLamp, -1, -1);
	sg_fog_type(S.lamp, SG_FOG_VIEW, -1);

	S.texLamp2 = sg_tex_load(2, "bg_lamp2");
	S.lamp2 = sg_plane(164.0f, 164.0f); /* DAT_008147c8 */
	sg_set_tex(S.lamp2, 0, S.texLamp2, -1, -1);
	sg_fog_type(S.lamp2, SG_FOG_VIEW, -1);
}


static void reset_vars(void)
{
	S.frame = 0;
	S.eyeX = 0;
	S.eyeY = 1000.0f; /* DAT_00814408 */
	S.eyeZ = 0;
	S.dirX = 0;
	S.dirY = -0.4f;   /* DAT_008147f0 */
	S.dirZ = 1.0f;    /* DAT_00853b34 */
}

/* FUN_004c2db0 */
static void init(void)
{
	reset_vars();
	S.lightR = 0.5f;  /* DAT_00853b1c */
	S.lightG = 0.5f;
	S.lightB = 0.4f;  /* DAT_00813f30 */
	S.ambient = 0.5f;
	sg_cam_init(&S.cam);
}

/* FUN_004c2ed0 */
static void reinit(void) { reset_vars(); }

/* FUN_004c2e80: deletes the camera */
static void stage_exit(void) {}

/* FUN_004c2f30 */
static void update(void)
{
	S.eyeZ -= 10.0f;            /* DAT_00813f4c */
	if (1000.0f < S.eyeZ)       /* DAT_00814408 */
		S.eyeZ -= 2000.0f;      /* DAT_008145fc */
	if (S.eyeZ < -1000.0f)      /* DAT_00814604 */
		S.eyeZ += 2000.0f;
	S.frame++;
}

/* FUN_004c2ab0 */
static void camera(float farZ)
{
	float fov = 70.0f; /* DAT_008145ec */
	if (stage_g.gameMode == 2)
		fov = 10.0f * stage_g.zoom + 60.0f; /* DAT_00813f4c, DAT_00813a58 */
	float eye[3] = {S.eyeX, S.eyeY, S.eyeZ};
	float at[3] = {S.eyeX + S.dirX, S.eyeY + S.dirY, S.eyeZ + S.dirZ};
	if (stage_g.gameMode == 2) {
		float a = ((float)(S.frame % 5000) / 5000.0f) * 6.2831855f; /* DAT_008147e4, DAT_00853b78 */
		eye[0] = sinf(a) * 300.0f + S.eyeX;                        /* DAT_008145f0 */
		eye[1] = S.eyeY;
		eye[2] = S.eyeZ;
		at[0] = sinf(a) * 300.0f + S.eyeX + S.dirX;
		at[1] = S.eyeY + S.dirY;
		at[2] = S.eyeZ + S.dirZ;
	}
	sg_cam_persp(&S.cam, fov * 0.64f, 50.0f, farZ, stage_g.aspect); /* DAT_008145e4, DAT_008140ec */
	sg_cam_reset(&S.cam);
	sg_cam_lookat(&S.cam, eye, at, NULL);
	float s = sinf(((float)(S.frame % 3300) / 3300.0f) * 6.2831855f); /* DAT_008147dc */
	sg_cam_rot_x(&S.cam, sg_rad2bam(s * 0.55f));                       /* DAT_008147c4 */
	if (stage_g.gameMode == 2) {
		s = sinf(((float)(S.frame % 1300) / 1300.0f) * 6.2831855f); /* DAT_008147d0 */
		sg_cam_rot_y(&S.cam, sg_rad2bam(s * 0.25f));                /* DAT_00813f24 */
	}
	sg_cam_build(&S.cam);
	sg_cam_apply(&S.cam);
}

/* FUN_004c2990 */
static void states(void)
{
	sg_rs(0x1b, 1);
	sg_rs(0x13, 5);
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	sg_rs(0x1c, 1);
	sg_light_enable(0, 1);
	sg_fog(1000.0f, 2500.0f, 0xff96968c); /* DAT_00814408, DAT_008147d4 */
}

/* FUN_004c2a10 */
static void light(void)
{
	static const float dir[3] = {0, -1.0f, 0}; /* DAT_00853bb4 */
	sg_ambient(S.ambient);
	sg_light_color(0, S.lightR, S.lightG, S.lightB);
	sg_light_dir(0, dir);
	sg_light_enable(0, 1);
}

/* CModel +0xa0 (FUN_0046c3a0) draws with world * *(DAT_00d92968+0x50); FUN_0046a0a0 sets
 * that matrix to D3DXMatrixReflect(plane through the 4 corners (+-4000, 0, +-4000)) =
 * mirror at y = 0. sg ignores the parent matrix, so fold the mirror into the model
 * transform (world * diag(1,-1,1,1) negates column 1). Culling is off (0x16 = 1) here. */
static void render_mirrored(sg_model *m)
{
	for (int i = 0; i < 3; i++)
		m->rot[i][1] = -m->rot[i][1];
	m->pos[1] = -m->pos[1];
	sg_render(m);
	sg_render_end_parent(m);
}

static void draw(int layer)
{
	if (layer != 1)
		return;
	camera(4000.0f); /* DAT_008147e0 */
	states();
	light();
	sg_clear(0xff96968c);
	sg_blend(1);
	sg_rs(0xe, 0);
	sg_rs(0x16, 1);
	sg_reset(S.sky);
	sg_color(S.sky, 1.0f, 1.0f, 1.0f, 1.0f, -1);
	sg_pos(S.sky, S.eyeX, S.eyeY, S.eyeZ);
	sg_rot_y(S.sky, sg_rad2bam(S.skyRot));
	sg_render(S.sky);
	sg_render_end(S.sky);
	sg_rs(0xe, 1);
	sg_rs(7, 1);
	/* DAT_0087ab08 +0x24: switch to a private D16 depth surface (FUN_004c2930), then clear it */
	sg_clear_z();
	/* FUN_0046a0a0(corners): mirror matrix + 4 user clip planes through the projected corners
	 * (-4000,0,-4000) (4000,0,-4000) (4000,0,4000) (-4000,0,4000) -- the clip planes are
	 * NOT reproduced (UNVERIFIED impact: the reflection is only drawn under the sea anyway). */
	sg_reset(S.bridgeR);
	sg_color(S.bridgeR, 1.0f, 0, 0, 0, -1);
	sg_pos(S.bridgeR, 0, 200.0f, 1000.0f); /* DAT_008147cc, DAT_00814408 */
	render_mirrored(S.bridgeR);
	sg_reset(S.bridgeR);
	sg_color(S.bridgeR, 1.0f, 0, 0, 0, -1);
	sg_pos(S.bridgeR, 0, 200.0f, 3000.0f); /* DAT_008147d8 */
	render_mirrored(S.bridgeR);
	sg_reset(S.bridgeR);
	sg_color(S.bridgeR, 1.0f, 0, 0, 0, -1);
	sg_pos(S.bridgeR, 0, 200.0f, -1000.0f); /* DAT_00814604 */
	render_mirrored(S.bridgeR);
	/* DAT_0087e7b4 +0x24: back to the scene depth surface. The background pass draws
	 * nothing into it before (sky: ZWRITE off), so it is equivalent to a cleared buffer. */
	sg_clear_z();
	sg_rs(0xe, 1);
	sg_rs(0x16, 3);
	sg_reset(S.sea);
	sg_color(S.sea, 0.85f, 1.0f, 1.0f, 1.0f, -1); /* DAT_00814120 */
	sg_pos(S.sea, 0, 0, 2000.0f);                 /* DAT_008145fc */
	sg_render(S.sea);
	sg_render_end(S.sea);
	sg_rs(7, 0);
	static const float shadowZ[3] = {0, 2000.0f, 4000.0f};
	for (int i = 0; i < 3; i++) {
		sg_reset(S.shadow);
		sg_color(S.shadow, 0.5f, 1.0f, 1.0f, 1.0f, -1);
		sg_pos(S.shadow, 0, 0, shadowZ[i]);
		sg_rot_y(S.shadow, sg_deg2bam(90));
		sg_render(S.shadow);
		sg_render_end(S.shadow);
	}
	sg_rs(7, 1);
	sg_rs(0xe, 1);
	static const float bridgeZ[3] = {1000.0f, 3000.0f, -1000.0f};
	for (int i = 0; i < 3; i++) {
		sg_reset(S.bridge);
		sg_pos(S.bridge, 0, 200.0f, bridgeZ[i]);
		sg_render(S.bridge);
		sg_render_end(S.bridge);
	}
	sg_fog(1000.0f, 6000.0f, 0xff000000); /* DAT_008147e8 */
	camera(6000.0f);
	sg_rs(7, 1);
	sg_rs(0xe, 0);
	sg_rs(0x16, 1);
	sg_blend(2);
	float s = sinf(((float)(stage_g.matchFrame % 50) / 50.0f) * 6.2831855f); /* DAT_008140ec */
	float a = s * 0.15f + 0.85f; /* DAT_00813f20, DAT_00814120 */
	/* (a second value sin*0.25+0.75 is computed and unused) */
	for (int i = 0; i < 10; i++) {
		float z = (float)(i * 2000) - 6000.0f;
		sg_reset(S.lamp);
		sg_color(S.lamp, a, 1.0f, 1.0f, 1.0f, -1);
		sg_pos(S.lamp, 200.0f, 1100.0f, z); /* DAT_008147cc, DAT_00814180 */
		sg_render(S.lamp);
		sg_render_end(S.lamp);
		sg_reset(S.lamp2);
		sg_color(S.lamp2, a, 1.0f, 1.0f, 1.0f, -1);
		sg_pos(S.lamp2, 200.0f, 1100.0f, z);
		sg_render(S.lamp2);
		sg_render_end(S.lamp2);
		sg_reset(S.lamp);
		sg_pos(S.lamp, -200.0f, 1100.0f, z); /* DAT_00814600 */
		sg_render(S.lamp);
		sg_render_end(S.lamp);
		sg_reset(S.lamp2);
		sg_pos(S.lamp2, -200.0f, 1100.0f, z);
		sg_render(S.lamp2);
		sg_render_end(S.lamp2);
	}
	sg_blend(1);
	sg_restore_2d();
}

const stage_def stage_br = {"br", "Big Bridge", load, NULL, init, reinit, stage_exit, update, draw};
