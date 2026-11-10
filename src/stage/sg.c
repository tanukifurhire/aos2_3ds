/* Portable part of the stage renderer: see sg.h and docs/re/stages.md §3. */
#include "sg.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f
#define TWO_PI_F 6.2831855f /* DAT_00853b78 */

sg_cmdlist sg_cmds;
sg_backend sg_be;

/* ================================================================ math */

void sg_mat_identity(sg_mat *o)
{
	memset(o, 0, sizeof *o);
	o->m[0][0] = o->m[1][1] = o->m[2][2] = o->m[3][3] = 1;
}

void sg_mat_mul(sg_mat *o, const sg_mat *a, const sg_mat *b)
{
	sg_mat r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] +
			            a->m[i][3] * b->m[3][j];
	*o = r;
}

bool sg_mat_inverse(sg_mat *o, const sg_mat *a)
{
	float t[4][8];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			t[i][j] = a->m[i][j];
			t[i][j + 4] = i == j;
		}
	for (int c = 0; c < 4; c++) {
		int p = c;
		for (int r = c + 1; r < 4; r++)
			if (fabsf(t[r][c]) > fabsf(t[p][c]))
				p = r;
		if (fabsf(t[p][c]) < 1e-12f)
			return false;
		if (p != c)
			for (int j = 0; j < 8; j++) {
				float x = t[c][j];
				t[c][j] = t[p][j];
				t[p][j] = x;
			}
		float inv = 1.0f / t[c][c];
		for (int j = 0; j < 8; j++)
			t[c][j] *= inv;
		for (int r = 0; r < 4; r++)
			if (r != c && t[r][c] != 0) {
				float f = t[r][c];
				for (int j = 0; j < 8; j++)
					t[r][j] -= f * t[c][j];
			}
	}
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			o->m[i][j] = t[i][j + 4];
	return true;
}

uint16_t sg_rad2bam(float rad) { return (uint16_t)((int)(rad * 65536.0f / TWO_PI_F) & 0xffff); }
uint16_t sg_deg2bam(int deg) { return (uint16_t)(deg * 0x10000 / 360); }
/* DAT_00ddc0f0: 65536-entry sine table indexed by binary angle (built once). */
static float *sin_tab;
float sg_bam_sin(uint32_t bam)
{
	if (!sin_tab) {
		sin_tab = malloc(65536 * sizeof(float));
		for (int i = 0; i < 65536; i++)
			sin_tab[i] = (float)sin((double)i * (6.283185307179586 / 65536.0));
	}
	return sin_tab[bam & 0xffff];
}
float sg_bam_cos(uint32_t bam) { return sg_bam_sin(bam + 0x4000); }

static void normalize3(float v[3])
{
	float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 0) {
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
}

static void cross3(float o[3], const float a[3], const float b[3])
{
	float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	memcpy(o, r, sizeof r);
}

/* FUN_00493940 / 004939e0 / 00493a80 — D3DXMatrixRotationX/Y/Z layout, sine table. */
static void rot3(float r[3][3], int axis, uint32_t bam)
{
	float s = sg_bam_sin(bam), c = sg_bam_cos(bam);
	memset(r, 0, sizeof(float) * 9);
	switch (axis) {
	case 0: r[0][0] = 1; r[1][1] = c; r[1][2] = s; r[2][1] = -s; r[2][2] = c; break;
	case 1: r[1][1] = 1; r[0][0] = c; r[0][2] = -s; r[2][0] = s; r[2][2] = c; break;
	default: r[2][2] = 1; r[0][0] = c; r[0][1] = s; r[1][0] = -s; r[1][1] = c; break;
	}
}

/* new = R * old (the new rotation applies first, in model space). */
static void premul3(float m[3][3], int axis, uint32_t bam)
{
	float r[3][3], o[3][3];
	rot3(r, axis, bam);
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			o[i][j] = r[i][0] * m[0][j] + r[i][1] * m[1][j] + r[i][2] * m[2][j];
	memcpy(m, o, sizeof o);
}

/* ================================================================ allocation tracking */

#define SG_MAX_OBJS 256
static sg_model *g_models[SG_MAX_OBJS];
static sg_particles *g_parts[32];

static void track_model(sg_model *m)
{
	for (int i = 0; i < SG_MAX_OBJS; i++)
		if (!g_models[i]) {
			g_models[i] = m;
			return;
		}
}

/* ================================================================ textures */

#define SG_MAX_TEX 64
static struct {
	char key[40];
	int id, refs;
} g_tex[SG_MAX_TEX];

int sg_tex_load(int folder, const char *name)
{
	const char *f = folder == 3 ? "bgMdlAdd" : folder == 1000 ? "3d" : "bgMdl";
	char key[40];
	snprintf(key, sizeof key, "%s/%s", f, name);
	for (int i = 0; i < SG_MAX_TEX; i++)
		if (g_tex[i].refs > 0 && !strcmp(g_tex[i].key, key)) {
			g_tex[i].refs++;
			return i + 1;
		}
	for (int i = 0; i < SG_MAX_TEX; i++)
		if (g_tex[i].refs == 0) {
			int id = sg_be.tex_load ? sg_be.tex_load(f, name) : 0;
			if (id <= 0) {
				fprintf(stderr, "sg: texture %s missing\n", key);
				return 0;
			}
			snprintf(g_tex[i].key, sizeof g_tex[i].key, "%s", key);
			g_tex[i].id = id;
			g_tex[i].refs = 1;
			return i + 1;
		}
	return 0;
}

void sg_tex_free(int tex)
{
	if (tex <= 0 || tex > SG_MAX_TEX || g_tex[tex - 1].refs <= 0)
		return;
	if (--g_tex[tex - 1].refs == 0 && sg_be.tex_free)
		sg_be.tex_free(g_tex[tex - 1].id);
}

int sg_tex_backend_id(int tex); /* used by backends */
int sg_tex_backend_id(int tex)
{
	if (tex <= 0 || tex > SG_MAX_TEX || g_tex[tex - 1].refs <= 0)
		return 0;
	return g_tex[tex - 1].id;
}

/* ================================================================ models */

static void geom_bounds(sg_geom *g)
{
	for (int k = 0; k < 3; k++) {
		g->bmin[k] = 1e30f;
		g->bmax[k] = -1e30f;
	}
	for (int i = 0; i < g->nv; i++)
		for (int k = 0; k < 3; k++) {
			float v = g->vtx[i * 8 + k];
			if (v < g->bmin[k]) g->bmin[k] = v;
			if (v > g->bmax[k]) g->bmax[k] = v;
		}
}

static void geom_finish(sg_geom *g)
{
	geom_bounds(g);
	if (sg_be.geom_upload)
		sg_be.geom_upload(g);
}

static void mesh_defaults(sg_mesh *ms)
{
	ms->lightType = SG_LIGHT_DIR; /* FUN_00470aa0: +0x18c = 0x800, +0x194 = 0x10000000 */
	ms->fogType = SG_FOG_NONE;
	for (int s = 0; s < ms->nsub; s++)
		for (int k = 0; k < 4; k++)
			ms->sub[s].color[k] = 1;
}

/* One mesh with one material: the shape of every generated primitive. */
static sg_model *model_new(int nmesh, int nsubEach)
{
	sg_model *m = calloc(1, sizeof *m);
	m->mesh = calloc(nmesh, sizeof *m->mesh);
	m->nmesh = nmesh;
	for (int i = 0; i < nmesh; i++) {
		m->mesh[i].sub = calloc(nsubEach, sizeof(sg_subset));
		m->mesh[i].nsub = nsubEach;
		mesh_defaults(&m->mesh[i]);
	}
	sg_reset(m);
	track_model(m);
	return m;
}

static sg_geom *geom_alloc(sg_model *m, int nv, int ni)
{
	sg_geom *g = &m->mesh[0].sub[0].geom;
	g->vtx = calloc((size_t)nv * 8, sizeof(float));
	g->idx = calloc((size_t)ni, sizeof(uint16_t));
	g->nv = nv;
	g->ni = ni;
	return g;
}

static void setv(float *v, float x, float y, float z, float nx, float ny, float nz, float u, float t)
{
	v[0] = x; v[1] = y; v[2] = z;
	v[3] = nx; v[4] = ny; v[5] = nz;
	v[6] = u; v[7] = t;
}

/* FUN_00468d80: rows i < stacks: p0 = (0,1,0)*RotX(i*pi/(stacks-1)); columns j <= slices:
 * p = p0*RotY(2pi - j*2pi/slices); pos = p*(rx,ry,rz); normal = normalize(pos);
 * uv = (1 - j/slices, (1 - p.y)/2). Indices per quad: (v, v+1, v+s+1), (v+1, v+s+2, v+s+1). */
sg_model *sg_sphere(float rx, float ry, float rz, int stacks, int slices)
{
	sg_model *m = model_new(1, 1);
	if (stacks < 3 || slices < 3)
		return m;
	int cols = slices + 1;
	sg_geom *g = geom_alloc(m, stacks * cols, (stacks - 1) * slices * 6);
	float *v = g->vtx;
	for (int i = 0; i < stacks; i++) {
		float a = (float)i * (PI_F / (float)(stacks - 1));
		float y0 = cosf(a), z0 = sinf(a);
		for (int j = 0; j < cols; j++) {
			float b = TWO_PI_F - (float)j * (TWO_PI_F / (float)slices);
			float px = z0 * sinf(b), py = y0, pz = z0 * cosf(b);
			float n[3] = {px * rx, py * ry, pz * rz};
			normalize3(n);
			setv(v, px * rx, py * ry, pz * rz, n[0], n[1], n[2], 1.0f - (float)j / (float)slices,
			     (1.0f - py) * 0.5f);
			v += 8;
		}
	}
	uint16_t *ix = g->idx;
	int s = slices;
	for (int i = 0; i < stacks - 1; i++)
		for (int j = 0; j < s; j++) {
			int b = i * cols + j;
			*ix++ = b; *ix++ = b + 1; *ix++ = b + s + 1;
			*ix++ = b + 1; *ix++ = b + s + 2; *ix++ = b + s + 1;
		}
	geom_finish(g);
	return m;
}

/* FUN_004678c0: 4 vertices (-w/2,h/2) (w/2,h/2) (-w/2,-h/2) (w/2,-h/2), normal (0,0,-1),
 * uv (0,0) (1,0) (0,1) (1,1), triangles (0,1,2) (2,1,3). */
sg_model *sg_plane(float w, float h)
{
	sg_model *m = model_new(1, 1);
	sg_geom *g = geom_alloc(m, 4, 6);
	float x = w * 0.5f, y = h * 0.5f;
	setv(g->vtx + 0, -x, y, 0, 0, 0, -1, 0, 0);
	setv(g->vtx + 8, x, y, 0, 0, 0, -1, 1, 0);
	setv(g->vtx + 16, -x, -y, 0, 0, 0, -1, 0, 1);
	setv(g->vtx + 24, x, -y, 0, 0, 0, -1, 1, 1);
	static const uint16_t ix[6] = {0, 1, 2, 2, 1, 3};
	memcpy(g->idx, ix, sizeof ix);
	geom_finish(g);
	return m;
}

/* FUN_004681f0 (xz) / FUN_00467d00 (xy): nx*nz grid, normal (0,0,-1),
 * u = (i/(nx-1) - .5)*s + .5, v = ((1 - r/(nz-1)) - .5)*s + .5,
 * triangles (n, c+1, c) (n+1, c+1, n) with c = r*nx+i, n = c+nx. */
static sg_model *grid(float w, float d, int nx, int nz, float uvs, int vertical)
{
	sg_model *m = model_new(1, 1);
	sg_geom *g = geom_alloc(m, nx * nz, (nz - 1) * (nx - 1) * 6);
	float *v = g->vtx;
	for (int r = 0; r < nz; r++) {
		float fr = 1.0f - (float)r / (float)(nz - 1);
		float c2 = (float)r * d / (float)(nz - 1) - d * 0.5f;
		for (int i = 0; i < nx; i++) {
			float x = (float)i * w / (float)(nx - 1) - w * 0.5f;
			float u = ((float)i / (float)(nx - 1) - 0.5f) * uvs + 0.5f, t = (fr - 0.5f) * uvs + 0.5f;
			if (vertical)
				setv(v, x, c2, 0, 0, 0, -1, u, t);
			else
				setv(v, x, 0, c2, 0, 0, -1, u, t);
			v += 8;
		}
	}
	uint16_t *ix = g->idx;
	for (int r = 0; r < nz - 1; r++)
		for (int i = 0; i < nx - 1; i++) {
			int c = r * nx + i, n = c + nx;
			*ix++ = n; *ix++ = c + 1; *ix++ = c;
			*ix++ = n + 1; *ix++ = c + 1; *ix++ = n;
		}
	geom_finish(g);
	return m;
}

sg_model *sg_grid_xz(float w, float d, int nx, int nz, float uvScale) { return grid(w, d, nx, nz, uvScale, 0); }
sg_model *sg_grid_xy(float w, float h, int nx, int ny, float uvScale) { return grid(w, h, nx, ny, uvScale, 1); }

/* FUN_004686e0: 6 faces of 4 vertices (plane uv layout), outward normals,
 * face order +y, -y, -z, +z, +x, -x; triangles (0,1,2) (3,2,1) per face. */
sg_model *sg_box(float w, float h, float d)
{
	sg_model *m = model_new(1, 1);
	sg_geom *g = geom_alloc(m, 24, 36);
	float x = w * 0.5f, y = h * 0.5f, z = d * 0.5f;
	static const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	const float P[6][4][3] = {
		{{-x, y, z}, {x, y, z}, {-x, y, -z}, {x, y, -z}},
		{{x, -y, z}, {-x, -y, z}, {x, -y, -z}, {-x, -y, -z}},
		{{-x, y, -z}, {x, y, -z}, {-x, -y, -z}, {x, -y, -z}},
		{{x, y, z}, {-x, y, z}, {x, -y, z}, {-x, -y, z}},
		{{x, y, -z}, {x, y, z}, {x, -y, -z}, {x, -y, z}},
		{{-x, y, z}, {-x, y, -z}, {-x, -y, z}, {-x, -y, -z}},
	};
	static const float N[6][3] = {{0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {-1, 0, 0}};
	for (int f = 0; f < 6; f++) {
		for (int k = 0; k < 4; k++)
			setv(g->vtx + (f * 4 + k) * 8, P[f][k][0], P[f][k][1], P[f][k][2], N[f][0], N[f][1], N[f][2],
			     uv[k][0], uv[k][1]);
		uint16_t b = (uint16_t)(f * 4);
		uint16_t *ix = g->idx + f * 6;
		ix[0] = b; ix[1] = b + 1; ix[2] = b + 2;
		ix[3] = b + 3; ix[4] = b + 2; ix[5] = b + 1;
	}
	geom_finish(g);
	return m;
}

/* FUN_00469450: W*H vertices x = i*sx/(W-1) - sx/2, y = red*height/255,
 * z = j*sz/(H-1) - sz/2, uv = (i/(W-1)*rep, j/(H-1)*rep); triangles (n, c+1, c) (n+1, c+1, n)
 * hmm see stages.md; angle-weighted normals (CMesh vtable +0x28, FUN_00471b70);
 * then split into split*split chunk meshes (one CMesh each, (W-1)/split+1 vertices a side). */
sg_model *sg_heightfield(const char *map, int split, float sx, float sz, float height, int uvRepeat)
{
	char path[64];
	snprintf(path, sizeof path, "hf/%s.bin", map);
	int len = 0;
	uint8_t *d = sg_be.read_file ? sg_be.read_file(path, &len) : NULL;
	if (!d || len < 8) {
		fprintf(stderr, "sg: height map %s missing\n", map);
		free(d);
		return model_new(1, 1);
	}
	int W = d[0] | d[1] << 8, H = d[4] | d[5] << 8;
	const uint8_t *px = d + 8;
	int nv = W * H;
	float *V = calloc((size_t)nv * 8, sizeof(float));
	for (int j = 0; j < H; j++)
		for (int i = 0; i < W; i++) {
			float *v = V + (j * W + i) * 8;
			setv(v, (float)i * sx / (float)(W - 1) - sx * 0.5f, (float)px[j * W + i] * height / 255.0f,
			     (float)j * sz / (float)(H - 1) - sz * 0.5f, 0, 0, 0,
			     (float)i / (float)(W - 1) * (float)uvRepeat, (float)j / (float)(H - 1) * (float)uvRepeat);
		}
	/* angle-weighted normals over the full grid */
	for (int j = 0; j < H - 1; j++)
		for (int i = 0; i < W - 1; i++) {
			int c = j * W + i, n = c + W;
			int tri[2][3] = {{n, c + 1, c}, {n + 1, c + 1, n}};
			for (int t = 0; t < 2; t++) {
				float *p[3] = {V + tri[t][0] * 8, V + tri[t][1] * 8, V + tri[t][2] * 8};
				float e1[3], e2[3], fn[3];
				for (int k = 0; k < 3; k++) {
					e1[k] = p[1][k] - p[0][k];
					e2[k] = p[2][k] - p[0][k];
				}
				cross3(fn, e1, e2);
				normalize3(fn);
				for (int corner = 0; corner < 3; corner++) {
					float a[3], b[3];
					for (int k = 0; k < 3; k++) {
						a[k] = p[(corner + 1) % 3][k] - p[corner][k];
						b[k] = p[(corner + 2) % 3][k] - p[corner][k];
					}
					normalize3(a);
					normalize3(b);
					float cs = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
					float ang = acosf(cs < -1 ? -1 : cs > 1 ? 1 : cs);
					for (int k = 0; k < 3; k++)
						p[corner][3 + k] += fn[k] * ang;
				}
			}
		}
	for (int i = 0; i < nv; i++)
		normalize3(V + i * 8 + 3);
	free(d);
	int cells = (W - 1) / split, side = cells + 1;
	sg_model *m = model_new(split * split, 1);
	for (int cz = 0; cz < split; cz++)
		for (int cx = 0; cx < split; cx++) {
			sg_geom *g = &m->mesh[cz * split + cx].sub[0].geom;
			g->nv = side * side;
			g->ni = cells * cells * 6;
			g->vtx = malloc((size_t)g->nv * 8 * sizeof(float));
			g->idx = malloc((size_t)g->ni * sizeof(uint16_t));
			for (int j = 0; j < side; j++)
				memcpy(g->vtx + j * side * 8, V + ((cz * cells + j) * W + cx * cells) * 8,
				       (size_t)side * 8 * sizeof(float));
			uint16_t *ix = g->idx;
			for (int j = 0; j < cells; j++)
				for (int i = 0; i < cells; i++) {
					int c = j * side + i, n = c + side;
					*ix++ = n; *ix++ = c + 1; *ix++ = c;
					*ix++ = n + 1; *ix++ = c + 1; *ix++ = n;
				}
			geom_finish(g);
		}
	free(V);
	return m;
}

/* FUN_00469e50: .x model converted by tools/aos2mesh ("SGM1"). Materials keep the
 * file's diffuse colour and texture (3dtex.dat). */
sg_model *sg_xfile(const char *name)
{
	char path[64];
	snprintf(path, sizeof path, "mdl/%s.bin", name);
	int len = 0;
	uint8_t *d = sg_be.read_file ? sg_be.read_file(path, &len) : NULL;
	if (!d || len < 8 || memcmp(d, "SGM1", 4)) {
		fprintf(stderr, "sg: model %s missing\n", name);
		free(d);
		return model_new(1, 1);
	}
	uint32_t nsub;
	memcpy(&nsub, d + 4, 4);
	sg_model *m = model_new(1, (int)nsub);
	const uint8_t *p = d + 8;
	for (uint32_t s = 0; s < nsub; s++) {
		sg_subset *sb = &m->mesh[0].sub[s];
		char tex[17] = {0};
		memcpy(tex, p, 16);
		p += 16;
		memcpy(sb->color, p, 16);
		p += 16;
		uint32_t nv, ni;
		memcpy(&nv, p, 4);
		memcpy(&ni, p + 4, 4);
		p += 8;
		sb->geom.nv = (int)nv;
		sb->geom.ni = (int)ni;
		sb->geom.vtx = malloc(nv * 32);
		sb->geom.idx = malloc(ni * 2 + 2);
		memcpy(sb->geom.vtx, p, nv * 32);
		p += nv * 32;
		memcpy(sb->geom.idx, p, ni * 2);
		p += (ni * 2 + 3) & ~3u;
		if (tex[0])
			sb->tex[0] = sg_tex_load(1000, tex);
		geom_finish(&sb->geom);
	}
	free(d);
	return m;
}

void sg_model_free(sg_model *m)
{
	if (!m)
		return;
	for (int i = 0; i < SG_MAX_OBJS; i++)
		if (g_models[i] == m)
			g_models[i] = NULL;
	for (int i = 0; i < m->nmesh; i++) {
		for (int s = 0; s < m->mesh[i].nsub; s++) {
			sg_subset *sb = &m->mesh[i].sub[s];
			if (sg_be.geom_free)
				sg_be.geom_free(&sb->geom);
			free(sb->geom.vtx);
			free(sb->geom.idx);
		}
		free(m->mesh[i].sub);
	}
	free(m->mesh);
	free(m);
}

/* ---- CModel methods ---- */

void sg_reset(sg_model *m)
{
	memset(m->rot, 0, sizeof m->rot);
	m->rot[0][0] = m->rot[1][1] = m->rot[2][2] = 1;
	m->pos[0] = m->pos[1] = m->pos[2] = 0;
	m->scale[0] = m->scale[1] = m->scale[2] = 1;
}

void sg_rot_x(sg_model *m, uint32_t bam) { premul3(m->rot, 0, bam); }
void sg_rot_y(sg_model *m, uint32_t bam) { premul3(m->rot, 1, bam); }
void sg_rot_z(sg_model *m, uint32_t bam) { premul3(m->rot, 2, bam); }
void sg_pos(sg_model *m, float x, float y, float z) { m->pos[0] = x; m->pos[1] = y; m->pos[2] = z; }
void sg_scale(sg_model *m, float x, float y, float z) { m->scale[0] = x; m->scale[1] = y; m->scale[2] = z; }

#define FOR_MESH(m, mi, body)                                                                        \
	for (int _i = 0; _i < (m)->nmesh; _i++)                                                          \
		if ((mi) < 0 || (mi) >= (m)->nmesh || _i == (mi)) {                                          \
			sg_mesh *ms = &(m)->mesh[_i];                                                            \
			body;                                                                                    \
		}

void sg_set_tex(sg_model *m, int stage, int tex, int mesh, int mat)
{
	if (stage < 0 || stage > 1)
		return;
	FOR_MESH(m, mesh, {
		for (int s = 0; s < ms->nsub; s++)
			if (mat < 0 || mat >= ms->nsub || s == mat)
				ms->sub[s].tex[stage] = tex;
	});
}

void sg_uv_u(sg_model *m, float u, int mesh) { FOR_MESH(m, mesh, ms->uvOff[0] = u); }
void sg_uv_v(sg_model *m, float v, int mesh) { FOR_MESH(m, mesh, ms->uvOff[1] = v); }
void sg_light_type(sg_model *m, int type, int mesh) { FOR_MESH(m, mesh, ms->lightType = type); }
void sg_fog_type(sg_model *m, int type, int mesh) { FOR_MESH(m, mesh, ms->fogType = type); }

void sg_color(sg_model *m, float a, float r, float g, float b, int mesh)
{
	FOR_MESH(m, mesh, {
		for (int s = 0; s < ms->nsub; s++) {
			ms->sub[s].color[0] = r;
			ms->sub[s].color[1] = g;
			ms->sub[s].color[2] = b;
			ms->sub[s].color[3] = a;
		}
	});
}

/* ================================================================ render state */

static struct {
	int blend;
	int zenable, zwrite, cull, fog;
	float fogStart, fogEnd;
	uint32_t fogColor;
	float ambient;
	float lightColor[4][3];
	float lightDir[4][3];
	int lightOn[4];
	sg_mat view, proj, vp;
	float eye[3];
	int parentMode;
} st;

void sg_blend(int mode)
{
	if (mode >= 0 && mode <= 7 && mode != 3)
		st.blend = mode;
}

void sg_rs(int state, int value)
{
	switch (state) {
	case 7: st.zenable = value; break;
	case 0xe: st.zwrite = value; break;
	case 0x16: st.cull = value; break;
	case 0x1c: st.fog = value; break;
	default: break; /* 0x1b/0x13/0x14 are covered by sg_blend in every stage */
	}
}

void sg_fog(float start, float end, uint32_t rgb)
{
	st.fogStart = start;
	st.fogEnd = end;
	st.fogColor = rgb & 0xffffff;
}

void sg_ambient(float a) { st.ambient = a; }
void sg_light_color(int i, float r, float g, float b)
{
	if (i >= 0 && i < 4) {
		st.lightColor[i][0] = r;
		st.lightColor[i][1] = g;
		st.lightColor[i][2] = b;
	}
}
void sg_light_dir(int i, const float d[3])
{
	if (i >= 0 && i < 4) {
		memcpy(st.lightDir[i], d, sizeof st.lightDir[i]);
		normalize3(st.lightDir[i]);
	}
}
void sg_light_enable(int i, int on)
{
	if (i >= 0 && i < 4)
		st.lightOn[i] = on;
}

void sg_restore_2d(void)
{
	sg_blend(1);
	sg_rs(0x16, 1);
	sg_rs(0x1c, 0);
}

void sg_frame_begin(void)
{
	sg_cmds.n = 0;
	sg_cmds.ncam = 0;
	st.parentMode = 0;
}

static sg_cmd *cmd_new(void);

void sg_clear(uint32_t argb)
{
	sg_cmd *c = cmd_new();
	if (c) {
		c->clear = 1;
		c->fogColor = argb & 0xffffff;
	}
}

void sg_clear_z(void)
{
	sg_cmd *c = cmd_new();
	if (c)
		c->clear = 2;
}

/* ================================================================ rendering */

static void model_world(const sg_model *m, sg_mat *w)
{
	/* FUN_0046c580: world = Scale * [rot | pos] */
	sg_mat_identity(w);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++)
			w->m[i][j] = m->rot[i][j] * m->scale[i];
		w->m[3][i] = m->pos[i];
	}
}

static sg_cmd *cmd_new(void)
{
	if (sg_cmds.n >= SG_MAX_CMDS)
		return NULL;
	sg_cmd *c = &sg_cmds.cmd[sg_cmds.n++];
	/* every field the backends read is assigned here or by the caller */
	c->clear = 0;
	c->geom = NULL;
	c->pvtx = NULL;
	c->npv = 0;
	c->tex[0] = c->tex[1] = 0;
	c->uvOff[0] = c->uvOff[1] = 0;
	c->lightObj[0] = c->lightObj[1] = c->lightObj[2] = 0;
	c->lightType = SG_LIGHT_NONE;
	c->fogType = SG_FOG_NONE;
	if (sg_cmds.ncam == 0) { /* no camera applied yet this frame */
		sg_cmds.cam[0].view = st.view;
		sg_cmds.cam[0].proj = st.proj;
		sg_cmds.ncam = 1;
	}
	c->view = &sg_cmds.cam[sg_cmds.ncam - 1].view;
	c->proj = &sg_cmds.cam[sg_cmds.ncam - 1].proj;
	c->blend = st.blend;
	c->zenable = st.zenable;
	c->zwrite = st.zwrite;
	c->cull = st.cull;
	c->fogStart = st.fogStart;
	c->fogEnd = st.fogEnd;
	c->fogColor = st.fogColor;
	c->ambient = st.ambient;
	/* FUN_00472480 uploads light 0's colour whatever the enable flag (DAT_00e25d70 is never read) */
	memcpy(c->lightColor, st.lightColor[0], sizeof c->lightColor);
	return c;
}

/* Frustum test of a bounding box (the engine skips chunks outside the view,
 * FUN_00463d20 in FUN_00472cc0). */
static bool box_visible(const sg_geom *g, const sg_mat *wvp)
{
	/* clip(corner) = clip(centre) +- e.x*row0 +- e.y*row1 +- e.z*row2: no per-corner matrix
	 * products (culling only; the result does not change any pixel) */
	float cen[3], ext[3];
	for (int k = 0; k < 3; k++) {
		cen[k] = (g->bmin[k] + g->bmax[k]) * 0.5f;
		ext[k] = (g->bmax[k] - g->bmin[k]) * 0.5f;
	}
	float c0[4], ax[3][4];
	for (int j = 0; j < 4; j++) {
		c0[j] = cen[0] * wvp->m[0][j] + cen[1] * wvp->m[1][j] + cen[2] * wvp->m[2][j] + wvp->m[3][j];
		for (int k = 0; k < 3; k++)
			ax[k][j] = ext[k] * wvp->m[k][j];
	}
	int out[6] = {0};
	for (int k = 0; k < 8; k++) {
		float c[4];
		for (int j = 0; j < 4; j++)
			c[j] = c0[j] + (k & 1 ? ax[0][j] : -ax[0][j]) + (k & 2 ? ax[1][j] : -ax[1][j]) +
			       (k & 4 ? ax[2][j] : -ax[2][j]);
		out[0] += c[0] < -c[3];
		out[1] += c[0] > c[3];
		out[2] += c[1] < -c[3];
		out[3] += c[1] > c[3];
		out[4] += c[2] < 0;
		out[5] += c[2] > c[3];
	}
	for (int i = 0; i < 6; i++)
		if (out[i] == 8)
			return false;
	return true;
}

/* world * vp for an affine world (column 3 = (0,0,0,1)); diagonal fast path for
 * unrotated models (sprites). */
static void affine_mul(sg_mat *o, const sg_mat *w, const sg_mat *vp)
{
	bool diag = w->m[0][1] == 0 && w->m[0][2] == 0 && w->m[1][0] == 0 && w->m[1][2] == 0 && w->m[2][0] == 0 &&
	            w->m[2][1] == 0;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			o->m[i][j] = diag ? w->m[i][i] * vp->m[i][j]
			                  : w->m[i][0] * vp->m[0][j] + w->m[i][1] * vp->m[1][j] + w->m[i][2] * vp->m[2][j];
	for (int j = 0; j < 4; j++)
		o->m[3][j] = w->m[3][0] * vp->m[0][j] + w->m[3][1] * vp->m[1][j] + w->m[3][2] * vp->m[2][j] + vp->m[3][j];
}

static void render_meshes(sg_model *m, int only)
{
	sg_mat w, wvp;
	model_world(m, &w);
	bool lit = false;
	for (int i = 0; i < m->nmesh; i++)
		lit |= m->mesh[i].lightType == SG_LIGHT_DIR || m->mesh[i].lightType == SG_LIGHT_HEMI;
	/* light direction in object space, negated (FUN_00472480 / FUN_00473260:
	 * dir * inverse(world)); the world is affine, so the 3x3 cofactor inverse suffices */
	float lo[3] = {0, 0, 0};
	if (lit) {
		const float(*a)[4] = w.m;
		float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1], c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2],
		      c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
		float det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
		if (det != 0) {
			float id = 1.0f / det, inv[3][3] = {
				{c00 * id, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id, (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id},
				{c01 * id, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id, (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id},
				{c02 * id, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id, (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id},
			};
			for (int k = 0; k < 3; k++)
				lo[k] = -(st.lightDir[0][0] * inv[0][k] + st.lightDir[0][1] * inv[1][k] +
				          st.lightDir[0][2] * inv[2][k]);
		}
	}
	/* per-mesh frustum test (FUN_00463d20 in FUN_00472cc0); also keeps off-screen
	 * billboards out of the command list */
	affine_mul(&wvp, &w, &st.vp);
	for (int i = 0; i < m->nmesh; i++) {
		if (only >= 0 && i != only)
			continue;
		sg_mesh *ms = &m->mesh[i];
		for (int s = 0; s < ms->nsub; s++) {
			sg_subset *sb = &ms->sub[s];
			if (!sb->geom.ni || !box_visible(&sb->geom, &wvp))
				continue;
			sg_cmd *c = cmd_new();
			if (!c)
				return;
			c->geom = &sb->geom;
			c->world = w;
			memcpy(c->lightObj, lo, sizeof lo);
			memcpy(c->color, sb->color, sizeof c->color);
			memcpy(c->uvOff, ms->uvOff, sizeof c->uvOff);
			c->tex[0] = sg_tex_backend_id(sb->tex[0]);
			c->tex[1] = sg_tex_backend_id(sb->tex[1]);
			c->lightType = ms->lightType;
			c->fogType = ms->fogType;
		}
	}
}

void sg_render(sg_model *m) { render_meshes(m, -1); }
void sg_render_mesh(sg_model *m, int mesh) { render_meshes(m, mesh); }
void sg_render_end(sg_model *m) { (void)m; st.parentMode = 0; }
void sg_render_end_parent(sg_model *m) { (void)m; st.parentMode = 1; }

/* ================================================================ camera */

void sg_cam_init(sg_camera *c)
{
	memset(c, 0, sizeof *c);
	sg_mat_identity(&c->view);
	sg_mat_identity(&c->proj);
	sg_cam_reset(c);
}

void sg_cam_persp(sg_camera *c, float fovDeg, float nearZ, float farZ, float aspect)
{
	/* FUN_00465e50: fov = deg*2pi/360, D3DXMatrixPerspectiveFovLH */
	float fov = fovDeg * TWO_PI_F * 0.0027777778f;
	float ys = cosf(fov * 0.5f) / sinf(fov * 0.5f);
	float q = farZ / (farZ - nearZ);
	if (aspect == 0)
		aspect = 800.0f / 600.0f;
	memset(&c->proj, 0, sizeof c->proj);
	c->proj.m[0][0] = ys / aspect;
	c->proj.m[1][1] = ys;
	c->proj.m[2][2] = q;
	c->proj.m[2][3] = 1;
	c->proj.m[3][2] = -q * nearZ;
	c->fovRad = fov;
	c->nearZ = nearZ;
	c->farZ = farZ;
	c->aspect = aspect;
}

void sg_cam_reset(sg_camera *c)
{
	static const float f[3] = {0, 0, 1}, u[3] = {0, 1, 0}, r[3] = {1, 0, 0};
	memcpy(c->fwd, f, sizeof f);
	memcpy(c->up, u, sizeof u);
	memcpy(c->right, r, sizeof r);
	memset(c->eye, 0, sizeof c->eye);
}

void sg_cam_build(sg_camera *c)
{
	normalize3(c->right);
	normalize3(c->up);
	normalize3(c->fwd);
	sg_mat *v = &c->view;
	sg_mat_identity(v);
	for (int k = 0; k < 3; k++) {
		v->m[k][0] = c->right[k];
		v->m[k][1] = c->up[k];
		v->m[k][2] = c->fwd[k];
	}
	v->m[3][0] = -(c->right[0] * c->eye[0] + c->right[1] * c->eye[1] + c->right[2] * c->eye[2]);
	v->m[3][1] = -(c->up[0] * c->eye[0] + c->up[1] * c->eye[1] + c->up[2] * c->eye[2]);
	v->m[3][2] = -(c->fwd[0] * c->eye[0] + c->fwd[1] * c->eye[1] + c->fwd[2] * c->eye[2]);
}

static void cam_rot(sg_camera *c, int axis, uint32_t bam)
{
	float b[3][3];
	memcpy(b[0], c->right, sizeof b[0]);
	memcpy(b[1], c->up, sizeof b[1]);
	memcpy(b[2], c->fwd, sizeof b[2]);
	premul3(b, axis, bam);
	memcpy(c->right, b[0], sizeof b[0]);
	memcpy(c->up, b[1], sizeof b[1]);
	memcpy(c->fwd, b[2], sizeof b[2]);
}

void sg_cam_rot_x(sg_camera *c, uint32_t bam) { cam_rot(c, 0, bam); }
void sg_cam_rot_y(sg_camera *c, uint32_t bam) { cam_rot(c, 1, bam); }
void sg_cam_rot_z(sg_camera *c, uint32_t bam) { cam_rot(c, 2, bam); }

/* FUN_00466ca0: D3DXMatrixLookAtLH basis. */
void sg_cam_lookat(sg_camera *c, const float eye[3], const float at[3], const float *upv)
{
	float up[3] = {0, 1, 0};
	if (upv)
		memcpy(up, upv, sizeof up);
	float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
	normalize3(f);
	if (f[0] == 0 && f[2] == 0 && (f[1] == 1 || f[1] == -1)) {
		up[0] = 0;
		up[1] = 0;
		up[2] = -f[1];
	}
	float s[3], u[3];
	cross3(s, up, f);
	normalize3(s);
	cross3(u, f, s);
	normalize3(u);
	memcpy(c->fwd, f, sizeof f);
	memcpy(c->up, u, sizeof u);
	memcpy(c->right, s, sizeof s);
	memcpy(c->eye, eye, sizeof c->eye);
}

void sg_cam_apply(sg_camera *c)
{
	st.view = c->view;
	st.proj = c->proj;
	sg_mat_mul(&st.vp, &c->view, &c->proj);
	int k = sg_cmds.ncam < SG_MAX_CAMS ? sg_cmds.ncam++ : SG_MAX_CAMS - 1;
	sg_cmds.cam[k].view = c->view;
	sg_cmds.cam[k].proj = c->proj;
	memcpy(st.eye, c->eye, sizeof st.eye);
}

/* ================================================================ particles */

struct sg_particles {
	int max, n, tex;
	float *v; /* 6 vertices per particle: x y z u v rgba */
};

sg_particles *sg_particles_new(int max)
{
	sg_particles *p = calloc(1, sizeof *p);
	p->max = max;
	p->v = malloc((size_t)max * 6 * 6 * sizeof(float));
	for (int i = 0; i < 32; i++)
		if (!g_parts[i]) {
			g_parts[i] = p;
			break;
		}
	return p;
}

void sg_particles_free(sg_particles *p)
{
	if (!p)
		return;
	for (int i = 0; i < 32; i++)
		if (g_parts[i] == p)
			g_parts[i] = NULL;
	sg_tex_free(p->tex);
	free(p->v);
	free(p);
}

void sg_particles_tex(sg_particles *p, int tex) { p->tex = tex; }
void sg_particles_begin(sg_particles *p) { p->n = 0; }

/* FUN_00475d50 + FUN_004764c0: quad (-w/2..w/2, -h/2..h/2) oriented by a LookAtLH basis
 * from the camera eye to the particle; uv = (rect + 0.5)/size. */
void sg_particles_add(sg_particles *p, float x, float y, float z, float w, float h, const int rect[4],
                      uint32_t argb)
{
	if (p->n >= p->max)
		return;
	float f[3] = {x - st.eye[0], y - st.eye[1], z - st.eye[2]}, up[3] = {0, 1, 0}, s[3], u[3];
	normalize3(f);
	if (f[0] == 0 && f[2] == 0 && (f[1] == 1 || f[1] == -1)) {
		up[1] = 0;
		up[2] = -f[1];
	}
	cross3(s, up, f);
	normalize3(s);
	cross3(u, f, s);
	float tw = 64, th = 64; /* the stages' particle textures are 64x64 (gz_particle) */
	float u0 = ((float)rect[0] + 0.5f) / tw, v0 = ((float)rect[1] + 0.5f) / th;
	float u1 = ((float)rect[2] - 0.5f) / tw, v1 = ((float)rect[3] - 0.5f) / th;
	float hx = w * 0.5f, hy = h * 0.5f;
	const float cx[4] = {-hx, hx, -hx, hx}, cy[4] = {hy, hy, -hy, -hy};
	const float cu[4] = {u0, u1, u0, u1}, cv[4] = {v0, v0, v1, v1};
	static const int order[6] = {0, 1, 2, 2, 1, 3};
	float *o = p->v + p->n * 36;
	for (int k = 0; k < 6; k++) {
		int c = order[k];
		o[0] = x + s[0] * cx[c] + u[0] * cy[c];
		o[1] = y + s[1] * cx[c] + u[1] * cy[c];
		o[2] = z + s[2] * cx[c] + u[2] * cy[c];
		o[3] = cu[c];
		o[4] = cv[c];
		memcpy(&o[5], &argb, 4);
		o += 6;
	}
	p->n++;
}

void sg_particles_end(sg_particles *p)
{
	if (!p->n)
		return;
	sg_cmd *c = cmd_new();
	if (!c)
		return;
	/* FUN_00475b10: world = identity, lighting/fog off, cull none */
	sg_mat_identity(&c->world);
	c->pvtx = p->v;
	c->npv = p->n * 6;
	c->tex[0] = sg_tex_backend_id(p->tex);
	c->lightType = SG_LIGHT_NONE;
	c->fogType = SG_FOG_NONE;
	c->cull = 1;
	for (int k = 0; k < 4; k++)
		c->color[k] = 1;
}

void sg_free_all(void)
{
	for (int i = 0; i < SG_MAX_OBJS; i++)
		if (g_models[i])
			sg_model_free(g_models[i]);
	for (int i = 0; i < 32; i++)
		if (g_parts[i])
			sg_particles_free(g_parts[i]);
	for (int i = 0; i < SG_MAX_TEX; i++)
		if (g_tex[i].refs > 0) {
			if (sg_be.tex_free)
				sg_be.tex_free(g_tex[i].id);
			g_tex[i].refs = 0;
		}
	memset(&st, 0, sizeof st);
	sg_cmds.n = 0;
}

/* Custom geometry for stage-specific generators (one mesh, one material). */
sg_model *sg_custom(int nv, int ni)
{
	sg_model *m = model_new(1, 1);
	geom_alloc(m, nv, ni);
	return m;
}

void sg_custom_done(sg_model *m) { geom_finish(&m->mesh[0].sub[0].geom); }
