/* citro3d backend for the stage backgrounds: executes the sg command list
 * (src/stage/sg.h) recorded by the ported stage draw functions.
 * docs/re/stages.md §5 (3DS implementation notes). */
#include "stage.h"

#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tex3ds.h>

int sg_tex_backend_id(int tex);

unsigned stage3ds_last_us, stage3ds_record_us;

static DVLB_s *dvlb;
static shaderProgram_s prog;
static int u_mvp, u_fogv, u_light, u_lcolor, u_mcolor, u_uvoff, u_vscale, u_vbias;
static C3D_Tex fogRamp;
static bool ready;

/* ---------------------------------------------------------------- textures */

#define MAX_TEX 64
static C3D_Tex texs[MAX_TEX + 1];
static bool texUsed[MAX_TEX + 1];
static char texName[MAX_TEX + 1][40];
static int texRefs[MAX_TEX + 1];
static int texAtlas[MAX_TEX + 1];      /* atlas texture id containing this one, 0 = none */
static float texRect[MAX_TEX + 1][4];  /* u0 v0 u1 v1 inside the atlas (D3D v-down) */

/* atlas index written by tools/aos2mesh: "folder/name atlas u0 v0 u1 v1" */
#define MAX_ATLAS_ENTRIES 32
static struct {
	char key[40], atlas[32];
	float r[4];
} atlasIdx[MAX_ATLAS_ENTRIES];
static int natlas;

static void atlas_index_load(void)
{
	FILE *f = fopen("romfs:/stage/tex/atlas.txt", "r");
	if (!f)
		return;
	while (natlas < MAX_ATLAS_ENTRIES &&
	       fscanf(f, "%39s %31s %f %f %f %f", atlasIdx[natlas].key, atlasIdx[natlas].atlas, &atlasIdx[natlas].r[0],
	              &atlasIdx[natlas].r[1], &atlasIdx[natlas].r[2], &atlasIdx[natlas].r[3]) == 6)
		natlas++;
	fclose(f);
}

static int tex_load(const char *folder, const char *name)
{
	char key[40];
	snprintf(key, sizeof key, "%s/%s", folder, name);
	for (int i = 1; i <= MAX_TEX; i++)
		if (texUsed[i] && !strcmp(texName[i], key)) {
			texRefs[i]++;
			return i;
		}
	int id = 1;
	while (id <= MAX_TEX && texUsed[id])
		id++;
	if (id > MAX_TEX)
		return 0;
	char path[96];
	snprintf(path, sizeof path, "romfs:/stage/tex/%s.t3x", key);
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	Tex3DS_Texture t3 = Tex3DS_TextureImportStdio(f, &texs[id], NULL, false);
	fclose(f);
	if (!t3)
		return 0;
	Tex3DS_TextureFree(t3);
	C3D_TexSetFilter(&texs[id], GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetFilterMipmap(&texs[id], GPU_LINEAR);
	C3D_TexSetWrap(&texs[id], GPU_REPEAT, GPU_REPEAT);
	texUsed[id] = true;
	texRefs[id] = 1;
	texAtlas[id] = 0;
	snprintf(texName[id], sizeof texName[id], "%s", key);
	for (int i = 0; i < natlas; i++)
		if (!strcmp(atlasIdx[i].key, key)) {
			texAtlas[id] = tex_load(folder, atlasIdx[i].atlas);
			memcpy(texRect[id], atlasIdx[i].r, sizeof texRect[id]);
			break;
		}
	return id;
}

static void tex_free(int id)
{
	if (id > 0 && id <= MAX_TEX && texUsed[id] && --texRefs[id] <= 0) {
		C3D_TexDelete(&texs[id]);
		texUsed[id] = false;
		if (texAtlas[id])
			tex_free(texAtlas[id]);
	}
}

/* ---------------------------------------------------------------- geometry */

typedef struct {
	void *vbo, *ibo;
} gpu_geom;

static void geom_upload(sg_geom *g)
{
	gpu_geom *gg = calloc(1, sizeof *gg);
	gg->vbo = linearAlloc((size_t)g->nv * 32);
	gg->ibo = linearAlloc((size_t)g->ni * 2 + 4);
	if (!gg->vbo || !gg->ibo) {
		if (gg->vbo) linearFree(gg->vbo);
		if (gg->ibo) linearFree(gg->ibo);
		free(gg);
		return;
	}
	memcpy(gg->vbo, g->vtx, (size_t)g->nv * 32);
	memcpy(gg->ibo, g->idx, (size_t)g->ni * 2);
	GSPGPU_FlushDataCache(gg->vbo, (u32)g->nv * 32);
	GSPGPU_FlushDataCache(gg->ibo, (u32)g->ni * 2);
	g->gpu = gg;
}

static void geom_free(sg_geom *g)
{
	gpu_geom *gg = g->gpu;
	if (!gg)
		return;
	linearFree(gg->vbo);
	linearFree(gg->ibo);
	free(gg);
	g->gpu = NULL;
}

static void *read_file(const char *rel, int *len)
{
	char path[96];
	snprintf(path, sizeof path, "romfs:/stage/%s", rel);
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *b = malloc((size_t)n + 1);
	if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		b = NULL;
	}
	fclose(f);
	*len = (int)n;
	return b;
}

/* particles: pos f3, rgba u8x4, uv f2 = 24 bytes */
#define PART_MAX_VERTS (6 * 4096)
static uint8_t *partBuf;
static int partUsed;

/* ---------------------------------------------------------------- init */

bool stage3ds_init(void)
{
	FILE *f = fopen("romfs:/stage/stage.shbin", "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	static u32 *shbin;
	shbin = malloc((size_t)n);
	if (fread(shbin, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		return false;
	}
	fclose(f);
	dvlb = DVLB_ParseFile(shbin, (u32)n);
	shaderProgramInit(&prog);
	shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
	u_mvp = shaderInstanceGetUniformLocation(prog.vertexShader, "mvp");
	u_fogv = shaderInstanceGetUniformLocation(prog.vertexShader, "fogv");
	u_light = shaderInstanceGetUniformLocation(prog.vertexShader, "light");
	u_lcolor = shaderInstanceGetUniformLocation(prog.vertexShader, "lcolor");
	u_mcolor = shaderInstanceGetUniformLocation(prog.vertexShader, "mcolor");
	u_uvoff = shaderInstanceGetUniformLocation(prog.vertexShader, "uvoff");
	u_vscale = shaderInstanceGetUniformLocation(prog.vertexShader, "vscale");
	u_vbias = shaderInstanceGetUniformLocation(prog.vertexShader, "vbias");

	/* fog ramp: L8 64x8, texel(u) = 1 - u (texenv INTERPOLATE weight of the fog colour) */
	C3D_TexInit(&fogRamp, 64, 8, GPU_L8);
	uint8_t *px = fogRamp.data;
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 64; x++) {
			int m = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
			px[(x >> 3) * 64 + m] = (uint8_t)(255 - (x * 255 + 31) / 63);
		}
	C3D_TexFlush(&fogRamp);
	C3D_TexSetFilter(&fogRamp, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(&fogRamp, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	partBuf = linearAlloc(PART_MAX_VERTS * 24);
	atlas_index_load();
	sg_be.read_file = read_file;
	sg_be.tex_load = tex_load;
	sg_be.tex_free = tex_free;
	sg_be.geom_upload = geom_upload;
	sg_be.geom_free = geom_free;
	stage_g.aspect = 400.0f / 240.0f;
	aos_mt_seed(&stage_g.rng, (uint32_t)svcGetSystemTick());
	ready = dvlb && partBuf;
	return ready;
}

void stage3ds_exit(void)
{
	stage_end();
	if (!ready)
		return;
	C3D_TexDelete(&fogRamp);
	shaderProgramFree(&prog);
	DVLB_Free(dvlb);
	linearFree(partBuf);
	ready = false;
}

static int lastFrame;

void stage3ds_begin(int id)
{
	stage_g.gameMode = 2;
	stage_begin(id);
	lastFrame = 0;
}

void stage3ds_end(void) { stage_end(); }

/* Menus: the original keeps stage 4 (the "sl" title background) active while the
 * menus/select screens run (removed by FUN_004bfca0(4) when a match starts). */
void stage3ds_menu_tick(void)
{
	if (stage_current() != STAGE_MENU) {
		stage_begin(STAGE_MENU);
		lastFrame = 0;
	}
	stage_g.gameMode = 0;
	stage_g.zoom = 1.0f;
	stage_update();
}

unsigned stage3ds_update_us;
void stage3ds_tick(float zoom, int matchFrame)
{
	u64 t0 = svcGetSystemTick();
	if (matchFrame < lastFrame) /* the match frame counter restarts every round (Game Init) */
		stage_round();
	lastFrame = matchFrame;
	stage_g.zoom = zoom;
	stage_g.matchFrame = matchFrame;
	stage_update();
	stage3ds_update_us = (unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
}

/* ---------------------------------------------------------------- draw */

static void set_blend(int mode)
{
	GPU_BLENDFACTOR s, d;
	switch (mode) { /* FUN_004637d0 */
	case 0: s = GPU_ONE; d = GPU_ZERO; break;
	case 2: s = GPU_SRC_ALPHA; d = GPU_ONE; break;
	case 4: s = GPU_ZERO; d = GPU_SRC_COLOR; break;
	case 5: s = GPU_ONE; d = GPU_ONE; break;
	case 6: s = GPU_DST_COLOR; d = GPU_ONE; break;
	case 7: s = GPU_ONE_MINUS_DST_COLOR; d = GPU_ZERO; break;
	default: s = GPU_SRC_ALPHA; d = GPU_ONE_MINUS_SRC_ALPHA; break;
	}
	C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, s, d, s, d);
}

/* PICA clip from the D3D world*view*proj: tilt for the rotated top screen
 * (x' = y, y' = -x) and z' = z - w (D3D depth 0..1 near..far -> PICA -1..0,
 * depth buffer 1..0, so D3D LESSEQUAL = GPU_GEQUAL). */
static void set_mvp(const sg_mat *wvp)
{
	C3D_Mtx m;
	for (int j = 0; j < 4; j++) {
		float r0 = wvp->m[j][0], r1 = wvp->m[j][1], r2 = wvp->m[j][2], r3 = wvp->m[j][3];
		float row[4][1] = {{r1}, {-r0}, {r2 - r3}, {r3}};
		for (int i = 0; i < 4; i++)
			m.r[i].c[3 - j] = row[i][0];
	}
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mvp, &m);
}

static uint32_t abgr(uint32_t rgb)
{
	return 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
}

/* Full-screen quad: colour clear (FUN_004639d0) and/or depth reset to the
 * cleared value (FUN_00463a00; also run after the stage so the citro2d
 * layer's depth test is unaffected by the stage). */
static void fullscreen(bool color, uint32_t rgb, bool depth)
{
	if (partUsed + 6 > PART_MAX_VERTS)
		return;
	static const float q[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
	uint8_t *o = partBuf + partUsed * 24;
	for (int i = 0; i < 6; i++) {
		float p[3] = {q[i][0], q[i][1], -1e-4f};
		memcpy(o + i * 24, p, 12);
		o[i * 24 + 12] = (uint8_t)(rgb >> 16);
		o[i * 24 + 13] = (uint8_t)(rgb >> 8);
		o[i * 24 + 14] = (uint8_t)rgb;
		o[i * 24 + 15] = 255;
		memset(o + i * 24 + 16, 0, 8);
	}
	GSPGPU_FlushDataCache(o, 6 * 24);
	C3D_Mtx id;
	Mtx_Identity(&id);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mvp, &id);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_light, 0, 0, 0, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_lcolor, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_mcolor, 1, 1, 1, 1);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_vscale, 1 / 255.0f, 1 / 255.0f, 1 / 255.0f, 1 / 255.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_vbias, 0, 0, 0, 0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, u_fogv, 0, 0, 0, 1);
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	set_blend(0);
	C3D_DepthTest(true, GPU_ALWAYS, (color ? GPU_WRITE_COLOR : 0) | (depth ? GPU_WRITE_DEPTH : 0));
	C3D_CullFace(GPU_CULL_NONE);
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	C3D_AttrInfo *ai = C3D_GetAttrInfo();
	AttrInfo_Init(ai);
	AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(ai, 1, GPU_UNSIGNED_BYTE, 4);
	AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);
	BufInfo_Add(bi, o, 24, 3, 0x210);
	C3D_DrawArrays(GPU_TRIANGLES, 0, 6);
	partUsed += 6;
}

/* ---- GPU state cache: identical consecutive state is not re-sent (height-field
 * chunks, flame billboards). ---- */
static struct {
	float u[8][4]; /* fogv light lcolor mcolor uvoff vscale vbias */
	bool uvalid[8];
	sg_mat mvp;
	bool mvpValid;
	int tex, fog, blend, depth, cull, attr;
	uint32_t fogColor;
} gs;

static void gs_reset(void)
{
	memset(&gs, 0, sizeof gs);
	gs.tex = gs.fog = gs.blend = gs.depth = gs.cull = gs.attr = -1;
}

static void unif(int slot, int id, float x, float y, float z, float w)
{
	if (gs.uvalid[slot] && gs.u[slot][0] == x && gs.u[slot][1] == y && gs.u[slot][2] == z && gs.u[slot][3] == w)
		return;
	gs.u[slot][0] = x;
	gs.u[slot][1] = y;
	gs.u[slot][2] = z;
	gs.u[slot][3] = w;
	gs.uvalid[slot] = true;
	C3D_FVUnifSet(GPU_VERTEX_SHADER, id, x, y, z, w);
}

static void mvp_cached(const sg_mat *wvp)
{
	if (gs.mvpValid && !memcmp(&gs.mvp, wvp, sizeof *wvp))
		return;
	gs.mvp = *wvp;
	gs.mvpValid = true;
	set_mvp(wvp);
}

static bool fog_on(const sg_cmd *c) { return c->fogType == SG_FOG_VIEW || c->fogType == SG_FOG_WORLD; }

/* texenv, blend, depth, cull (everything except the vertex uniforms) */
static void fixed_state(const sg_cmd *c, int tid)
{
	if (!(tid > 0 && tid <= MAX_TEX && texUsed[tid]))
		tid = 0;
	int fog = fog_on(c);
	if (tid != gs.tex) {
		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		if (tid) {
			C3D_TexBind(0, &texs[tid]);
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		} else {
			C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
			C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		}
		gs.tex = tid;
	}
	/* texenv 1 = fog: INTERPOLATE(fog colour, previous, ramp(1 - f)) */
	if (fog != gs.fog || (fog && c->fogColor != gs.fogColor)) {
		C3D_TexEnv *env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		if (fog) {
			C3D_TexBind(1, &fogRamp);
			C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PREVIOUS, GPU_TEXTURE1);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
			C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, 0, 0);
			C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
			C3D_TexEnvColor(env, abgr(c->fogColor));
		}
		gs.fog = fog;
		gs.fogColor = c->fogColor;
	}
	if (c->blend != gs.blend) {
		set_blend(c->blend);
		gs.blend = c->blend;
	}
	int depth = c->zenable ? (c->zwrite ? 2 : 1) : 0;
	if (depth != gs.depth) {
		if (depth)
			C3D_DepthTest(true, GPU_GEQUAL, depth == 2 ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
		else
			C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
		gs.depth = depth;
	}
	/* D3D culls by screen winding (y down); NDC y is up and the tilt keeps winding:
	 * D3DCULL_CCW keeps NDC-CCW triangles = GPU_CULL_BACK_CCW. */
	if (c->cull != gs.cull) {
		C3D_CullFace(c->cull == 3 ? GPU_CULL_BACK_CCW : c->cull == 2 ? GPU_CULL_FRONT_CCW : GPU_CULL_NONE);
		gs.cull = c->cull;
	}
}

static void fog_unif(const sg_cmd *c, const sg_mat *wv)
{
	float fa = 0, fb = 1;
	if (fog_on(c) && c->fogEnd != c->fogStart) {
		fa = -1.0f / (c->fogEnd - c->fogStart);
		fb = c->fogEnd / (c->fogEnd - c->fogStart);
	}
	unif(0, u_fogv, wv->m[0][2] * fa, wv->m[1][2] * fa, wv->m[2][2] * fa, wv->m[3][2] * fa + fb);
}

/* attribute layout: 0 = mesh (pos f3, normal f3, uv f2), 1 = coloured (pos f3, rgba u8, uv f2) */
static void attrs(int mode, const void *buf)
{
	if (mode != gs.attr) {
		C3D_AttrInfo *ai = C3D_GetAttrInfo();
		AttrInfo_Init(ai);
		AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
		if (mode)
			AttrInfo_AddLoader(ai, 1, GPU_UNSIGNED_BYTE, 4);
		else
			AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 3);
		AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);
		gs.attr = mode;
	}
	C3D_BufInfo *bi = C3D_GetBufInfo();
	BufInfo_Init(bi);
	BufInfo_Add(bi, buf, mode ? 24 : 32, 3, 0x210);
}

static void coloured_unifs(void)
{
	unif(1, u_light, 0, 0, 0, 1);
	unif(2, u_lcolor, 1, 1, 1, 1);
	unif(3, u_mcolor, 1, 1, 1, 1);
	unif(4, u_uvoff, 0, 0, 0, 0);
	unif(5, u_vscale, 1 / 255.0f, 1 / 255.0f, 1 / 255.0f, 1 / 255.0f);
	unif(6, u_vbias, 0, 0, 0, 0);
}

static void draw_cmd(const sg_cmd *c)
{
	if (c->clear) {
		fullscreen(c->clear == 1, c->fogColor, c->clear == 2);
		gs_reset();
		return;
	}
	sg_mat wv, wvp;
	sg_mat_mul(&wv, &c->world, c->view);
	sg_mat_mul(&wvp, &wv, c->proj);
	mvp_cached(&wvp);
	fog_unif(c, &wv);
	fixed_state(c, c->tex[0]);
	if (c->pvtx) {
		if (partUsed + c->npv > PART_MAX_VERTS)
			return;
		uint8_t *o = partBuf + partUsed * 24;
		for (int i = 0; i < c->npv; i++) {
			const float *s = c->pvtx + i * 6;
			uint32_t argb;
			memcpy(&argb, &s[5], 4);
			memcpy(o, s, 12);
			o[12] = (uint8_t)(argb >> 16);
			o[13] = (uint8_t)(argb >> 8);
			o[14] = (uint8_t)argb;
			o[15] = (uint8_t)(argb >> 24);
			memcpy(o + 16, &s[3], 8);
			o += 24;
		}
		GSPGPU_FlushDataCache(partBuf + partUsed * 24, (u32)c->npv * 24);
		coloured_unifs();
		attrs(1, partBuf + partUsed * 24);
		C3D_DrawArrays(GPU_TRIANGLES, 0, c->npv);
		partUsed += c->npv;
		return;
	}
	gpu_geom *gg = c->geom->gpu;
	if (!gg)
		return;
	bool lit = c->lightType == SG_LIGHT_DIR || c->lightType == SG_LIGHT_HEMI;
	if (lit) {
		unif(1, u_light, c->lightObj[0], c->lightObj[1], c->lightObj[2], c->ambient);
		unif(2, u_lcolor, c->lightColor[0], c->lightColor[1], c->lightColor[2], 1);
	} else {
		unif(1, u_light, 0, 0, 0, 1);
		unif(2, u_lcolor, 1, 1, 1, 1);
	}
	if (c->lightType == SG_LIGHT_WHITE || c->lightType == SG_LIGHT_VCOLOR)
		unif(3, u_mcolor, 1, 1, 1, 1);
	else
		unif(3, u_mcolor, c->color[0], c->color[1], c->color[2], c->color[3]);
	unif(4, u_uvoff, c->uvOff[0], c->uvOff[1], 0, 0);
	unif(5, u_vscale, 0, 0, 0, 0);
	unif(6, u_vbias, 1, 1, 1, 1);
	attrs(0, gg->vbo);
	C3D_DrawElements(GPU_TRIANGLES, c->geom->ni, C3D_UNSIGNED_SHORT, gg->ibo);
}

/* ---- CPU batching of small meshes (billboards, planes, boxes): consecutive
 * commands with the same texture/blend/depth/cull/fog/camera are transformed to
 * world space, coloured with the vertex-shader formula (stages.md §3.3) on the
 * CPU and drawn with one call. Order is preserved, so the result is identical
 * up to 8-bit vertex-colour quantisation. ---- */
#define BATCH_MAX_NV 24
static const sg_cmd *bkey;
static int bfirst, bcount, bkeyTex;

/* Texture a batched command samples: its atlas when the mesh uv stay inside [0,1]
 * without scrolling (then uv are remapped into the atlas cell), else itself. */
static int eff_tex(const sg_cmd *c, const float **rect)
{
	int t = c->tex[0];
	*rect = NULL;
	if (t <= 0 || t > MAX_TEX || !texUsed[t] || !texAtlas[t] || c->uvOff[0] != 0 || c->uvOff[1] != 0)
		return t;
	const sg_geom *g = c->geom;
	for (int i = 0; i < g->nv; i++) {
		float u = g->vtx[i * 8 + 6], v = g->vtx[i * 8 + 7];
		if (u < 0 || u > 1 || v < 0 || v > 1)
			return t;
	}
	*rect = texRect[t];
	return texAtlas[t];
}

static bool batchable(const sg_cmd *c)
{
	return !c->clear && c->geom && c->geom->gpu && c->geom->nv <= BATCH_MAX_NV && c->fogType != SG_FOG_WORLD;
}

static bool same_key(const sg_cmd *a, const sg_cmd *b)
{
	if (a->blend != b->blend || a->zenable != b->zenable || a->cull != b->cull ||
	    (a->zenable && a->zwrite != b->zwrite) || fog_on(a) != fog_on(b))
		return false;
	if (fog_on(a) && (a->fogStart != b->fogStart || a->fogEnd != b->fogEnd || a->fogColor != b->fogColor))
		return false;
	return a->view == b->view && a->proj == b->proj;
}

static uint8_t q8(float v) { return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255.0f + 0.5f)); }

static void batch_flush(void)
{
	if (bkey && bcount) {
		sg_mat vp;
		sg_mat_mul(&vp, bkey->view, bkey->proj);
		mvp_cached(&vp);
		fog_unif(bkey, bkey->view);
		fixed_state(bkey, bkeyTex);
		coloured_unifs();
		GSPGPU_FlushDataCache(partBuf + bfirst * 24, (u32)bcount * 24);
		attrs(1, partBuf + bfirst * 24);
		C3D_DrawArrays(GPU_TRIANGLES, 0, bcount);
	}
	bkey = NULL;
	bcount = 0;
}

static void batch_add(const sg_cmd *c)
{
	const sg_geom *g = c->geom;
	const float *rect;
	int tex = eff_tex(c, &rect);
	if (bkey && (tex != bkeyTex || !same_key(bkey, c) || partUsed + g->ni > PART_MAX_VERTS))
		batch_flush();
	if (partUsed + g->ni > PART_MAX_VERTS)
		return;
	if (!bkey) {
		bkey = c;
		bkeyTex = tex;
		bfirst = partUsed;
		bcount = 0;
	}
	uint8_t col[BATCH_MAX_NV][4];
	for (int i = 0; i < g->nv; i++) {
		const float *v = g->vtx + i * 8;
		float r = 1, gr = 1, b = 1, a = 1;
		if (c->lightType == SG_LIGHT_DIR || c->lightType == SG_LIGHT_HEMI) {
			float d = v[3] * c->lightObj[0] + v[4] * c->lightObj[1] + v[5] * c->lightObj[2] + c->ambient;
			r = d * c->lightColor[0] * c->color[0];
			gr = d * c->lightColor[1] * c->color[1];
			b = d * c->lightColor[2] * c->color[2];
			a = c->color[3];
		} else if (c->lightType == SG_LIGHT_NONE) {
			r = c->color[0];
			gr = c->color[1];
			b = c->color[2];
			a = c->color[3];
		}
		col[i][0] = q8(r);
		col[i][1] = q8(gr);
		col[i][2] = q8(b);
		col[i][3] = q8(a);
	}
	float wp[BATCH_MAX_NV][3];
	const sg_mat *w = &c->world;
	for (int i = 0; i < g->nv; i++) {
		const float *v = g->vtx + i * 8;
		for (int j = 0; j < 3; j++)
			wp[i][j] = v[0] * w->m[0][j] + v[1] * w->m[1][j] + v[2] * w->m[2][j] + w->m[3][j];
	}
	uint8_t *o = partBuf + partUsed * 24;
	for (int k = 0; k < g->ni; k++) {
		int i = g->idx[k];
		const float *v = g->vtx + i * 8;
		const float *p = wp[i];
		float uv[2] = {v[6] + c->uvOff[0], v[7] + c->uvOff[1]};
		if (rect) {
			uv[0] = rect[0] + uv[0] * (rect[2] - rect[0]);
			uv[1] = rect[1] + uv[1] * (rect[3] - rect[1]);
		}
		memcpy(o, p, 12);
		memcpy(o + 12, col[i], 4);
		memcpy(o + 16, uv, 8);
		o += 24;
	}
	partUsed += g->ni;
	bcount += g->ni;
}

void stage3ds_draw(void)
{
	if (!ready || !stage_current())
		return;
	u64 t0 = svcGetSystemTick();
	stage_draw();
	stage3ds_record_us = (unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
	C2D_Flush();
	C3D_BindProgram(&prog);
	partUsed = 0;
	gs_reset();
	bkey = NULL;
	/* Leave room in the GPU command buffer for the 2D layer (a full buffer hangs the frame). */
	for (int i = 0; i < sg_cmds.n && C3D_GetCmdBufUsage() < 0.6f; i++) {
		const sg_cmd *c = &sg_cmds.cmd[i];
		if (batchable(c)) {
			batch_add(c);
		} else {
			batch_flush();
			draw_cmd(c);
		}
	}
	batch_flush();
	fullscreen(false, 0, true);
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C2D_Prepare();
	stage3ds_last_us = (unsigned)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
}
