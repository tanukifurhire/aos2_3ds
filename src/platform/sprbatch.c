#include "sprbatch.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXQ 8192 /* quads per frame (top: draw list cap 4096 + ring + HUD; bottom panel) */

static DVLB_s *dvlb;
static u32 *shbin;
static shaderProgram_s prog;
static int loc_proj, loc_depth;
static C3D_AttrInfo attr;
static C3D_BufInfo buf;
static spr_vtx *vbuf;
static u16 *ibuf;
C3D_Tex spr_white; /* 8x8 opaque white: solid primitives */
static C3D_Mtx proj_top, proj_bot;
static bool ready, active;
static int q_last; /* first quad not yet drawn */
static spr_vtx *pass_start; /* vertices written by this pass (flushed from the data cache in spr_end) */
static int env_mode = -1;

spr_vtx *spr_vp, *spr_vend;
C3D_Tex *spr_tex_cur;
int spr_cls_cur = -1;
bool spr_premul_cur;
spr_stats_t spr_stats;

bool spr_init(void)
{
	if (ready)
		return true;
	FILE *f = fopen("romfs:/shader/sprite.shbin", "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	shbin = malloc((size_t)n);
	if (!shbin || fread(shbin, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		return false;
	}
	fclose(f);
	dvlb = DVLB_ParseFile(shbin, (u32)n);
	if (!dvlb)
		return false;
	shaderProgramInit(&prog);
	shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
	loc_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "projMtx");
	loc_depth = shaderInstanceGetUniformLocation(prog.vertexShader, "depth");

	AttrInfo_Init(&attr);
	AttrInfo_AddLoader(&attr, 0, GPU_FLOAT, 2);         /* x, y */
	AttrInfo_AddLoader(&attr, 1, GPU_FLOAT, 2);         /* u, v */
	AttrInfo_AddLoader(&attr, 2, GPU_UNSIGNED_BYTE, 4); /* colour */

	vbuf = linearAlloc(sizeof(spr_vtx) * 4 * MAXQ);
	ibuf = linearAlloc(sizeof(u16) * 6 * MAXQ);
	if (!vbuf || !ibuf)
		return false;
	for (int q = 0; q < MAXQ; q++) {
		u16 *i = &ibuf[q * 6], b = (u16)(q * 4);
		i[0] = b;
		i[1] = b + 2;
		i[2] = b + 1;
		i[3] = b + 1;
		i[4] = b + 2;
		i[5] = b + 3;
	}
	GSPGPU_FlushDataCache(ibuf, sizeof(u16) * 6 * MAXQ);
	BufInfo_Init(&buf);
	BufInfo_Add(&buf, vbuf, sizeof(spr_vtx), 3, 0x210);

	/* white texture for solid draws */
	if (!C3D_TexInit(&spr_white, 8, 8, GPU_RGBA8))
		return false;
	memset(spr_white.data, 0xff, spr_white.size);
	C3D_TexFlush(&spr_white);
	C3D_TexSetWrap(&spr_white, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	/* citro2d's projections (C2D_SceneSize for the two screens) */
	Mtx_OrthoTilt(&proj_top, 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	Mtx_OrthoTilt(&proj_bot, 0.0f, 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
	spr_vp = spr_vend = vbuf;
	ready = true;
	return true;
}

void spr_exit(void)
{
	if (!ready)
		return;
	ready = false;
	C3D_TexDelete(&spr_white);
	linearFree(vbuf);
	linearFree(ibuf);
	shaderProgramFree(&prog);
	DVLB_Free(dvlb);
	free(shbin);
}

void spr_flush(void)
{
	if (!active)
		return;
	int q = (int)(spr_vp - vbuf) / 4;
	if (q > q_last) {
		C3D_DrawElements(GPU_TRIANGLES, (q - q_last) * 6, C3D_UNSIGNED_SHORT, ibuf + q_last * 6);
		spr_stats.draws++;
		q_last = q;
	}
}

/* texenv: 0 = premultiplied texture, 1 = straight texture (rgb *= tex.a), 2 = raw (exotic PC blend modes) */
static void set_env(int mode)
{
	if (mode == env_mode)
		return;
	env_mode = mode;
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	env = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env);
	if (mode == 1) {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE0, 0);
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	}
}

static void set_blend(int cls)
{
	GPU_BLENDFACTOR s, d;
	switch (cls) {
	case 16 + 0: s = GPU_ONE; d = GPU_ZERO; break;                  /* opaque */
	case 16 + 4: s = GPU_ZERO; d = GPU_SRC_COLOR; break;            /* mul */
	case 16 + 5: s = GPU_ONE; d = GPU_ONE; break;                   /* add */
	case 16 + 6: s = GPU_DST_COLOR; d = GPU_ONE; break;             /* screen */
	case 16 + 7: s = GPU_ONE_MINUS_DST_COLOR; d = GPU_ZERO; break;  /* invert */
	case 1: s = GPU_ONE; d = GPU_ONE_MINUS_SRC_ALPHA; break;        /* PC 1 and 2, premultiplied */
	default: s = GPU_SRC_ALPHA; d = GPU_ONE_MINUS_SRC_ALPHA; break; /* unknown modes: PC default = alpha */
	}
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s, d, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

void spr_state_change(C3D_Tex *t, bool premul, int cls)
{
	spr_flush();
	if (t != spr_tex_cur) {
		C3D_TexBind(0, t);
		spr_tex_cur = t;
		spr_stats.texsw++;
	}
	spr_premul_cur = premul;
	if (cls != spr_cls_cur) {
		set_blend(cls);
		spr_cls_cur = cls;
		spr_stats.statesw++;
	}
	set_env(cls >= 16 ? 2 : spr_premul_cur ? 0 : 1);
}

spr_vtx *spr_quad_slow(void)
{
	if (!active)
		return NULL;
	spr_stats.dropped++;
	return NULL;
}

/* Start of a frame's 2D (render_world, before its pass): reuse the vertex buffer from the start. Called after
 * C3D_FrameBegin(C3D_FRAME_SYNCDRAW), so the GPU is done with the previous frame's vertices. (C3D_FrameCounter
 * can't be used: it advances asynchronously on the screen transfer, which once reset the buffer between the top
 * and bottom passes of one frame and let the panel's vertices overwrite the top screen's.) */
void spr_frame_begin(void)
{
	if (active)
		return;
	spr_vp = spr_vend = vbuf;
	q_last = 0;
}

void spr_depth(float z)
{
	spr_flush();
	C3D_FVUnifSet(GPU_VERTEX_SHADER, loc_depth, z, 0.0f, 0.0f, 0.0f);
}

void spr_begin(int W, float depth)
{
	if (!ready)
		return;
	const C3D_Mtx *pm = W == 320 ? &proj_bot : &proj_top;
	spr_vend = vbuf + 4 * MAXQ;
	C2D_Flush(); /* whatever citro2d still holds goes first */
	C3D_BindProgram(&prog);
	C3D_SetAttrInfo(&attr);
	C3D_SetBufInfo(&buf);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, loc_proj, pm);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, loc_depth, depth, 0.0f, 0.0f, 0.0f);
	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); /* as C2D_Prepare */
	C3D_CullFace(GPU_CULL_NONE);
	for (int i = 2; i < 6; i++)
		C3D_TexEnvInit(C3D_GetTexEnv(i));
	spr_tex_cur = NULL;
	spr_cls_cur = -1;
	env_mode = -1;
	pass_start = spr_vp;
	active = true;
}

void spr_end(void)
{
	if (!active)
		return;
	spr_flush();
	active = false;
	spr_vend = spr_vp; /* spr_quad() returns NULL outside a pass */
	/* the command list that reads these vertices is submitted at the next C3D_FrameDrawOn / FrameEnd */
	if (spr_vp > pass_start)
		GSPGPU_FlushDataCache(pass_start, (u32)((char *)spr_vp - (char *)pass_start));
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA,
	               GPU_ONE_MINUS_SRC_ALPHA);
	C2D_Prepare(); /* citro2d's program, buffers, texenv, depth state */
}

void spr_tri_solid(const float x[3], const float y[3], const uint32_t rgba[3], int pcblend)
{
	spr_state(NULL, false, pcblend);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 c2 = spr_color(rgba[2], pcblend);
	spr_set(&v[0], x[0], y[0], SPR_SOLID_UV, SPR_SOLID_UV, spr_color(rgba[0], pcblend));
	spr_set(&v[1], x[1], y[1], SPR_SOLID_UV, SPR_SOLID_UV, spr_color(rgba[1], pcblend));
	spr_set(&v[2], x[2], y[2], SPR_SOLID_UV, SPR_SOLID_UV, c2);
	v[3] = v[2]; /* degenerate second triangle */
}

void spr_rect_solid(float l, float t, float r, float b, uint32_t rgba, int pcblend)
{
	spr_state(NULL, false, pcblend);
	spr_vtx *v = spr_quad();
	if (!v)
		return;
	u32 c = spr_color(rgba, pcblend);
	spr_set(&v[0], l, t, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[1], r, t, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[2], l, b, SPR_SOLID_UV, SPR_SOLID_UV, c);
	spr_set(&v[3], r, b, SPR_SOLID_UV, SPR_SOLID_UV, c);
}
