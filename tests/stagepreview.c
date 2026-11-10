/*
 * Host preview of the stage backgrounds: runs a stage's load/init/update
 * (src/stage) for N ticks and renders its sg command list with a small
 * software rasterizer that implements the same vertex/pixel math as the
 * original shaders (stages.md §3.3), then writes a PNG.
 *
 *   stagepreview <stageId> <ticks> <zoom> <out.png> [-3ds] [-romfs build/romfs/stage]
 *                [-tex build/stagehost/tex]
 *
 * -3ds renders 400x240 with the 3DS aspect instead of the PC's 800x600.
 * Assets come from tools/aos2mesh (tests/run_stagepreview.sh builds everything).
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/stage/stage.h"

int sg_tex_backend_id(int tex);

static const char *romfs = "build/romfs/stage", *texroot = "build/stagehost/tex";

/* ---------------------------------------------------------------- assets */

static void *read_file(const char *rel, int *len)
{
	char p[512];
	snprintf(p, sizeof p, "%s/%s", romfs, rel);
	FILE *f = fopen(p, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *b = malloc((size_t)n + 1);
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		fclose(f);
		free(b);
		return NULL;
	}
	fclose(f);
	*len = (int)n;
	return b;
}

typedef struct {
	int w, h;
	uint8_t *px;
} htex;
static htex texs[128];
static int ntex = 1;

static int tex_load(const char *folder, const char *name)
{
	char p[512];
	snprintf(p, sizeof p, "%s/%s/%s.rgba", texroot, folder, name);
	int len;
	FILE *f = fopen(p, "rb");
	if (!f || ntex >= 128) {
		if (f)
			fclose(f);
		return 0;
	}
	uint32_t wh[2];
	if (fread(wh, 4, 2, f) != 2) {
		fclose(f);
		return 0;
	}
	len = (int)(wh[0] * wh[1] * 4);
	htex *t = &texs[ntex];
	t->w = (int)wh[0];
	t->h = (int)wh[1];
	t->px = malloc((size_t)len);
	if (fread(t->px, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return ntex++;
}

static void tex_free(int id) { (void)id; }

/* ---------------------------------------------------------------- raster */

static int W = 800, H = 600;
static float *fb, *zb;

typedef struct {
	float x, y, z, w;      /* clip */
	float u, v;
	float c[4];            /* vertex colour */
	float fog;             /* fog factor (1 = no fog) */
} rv;

static void sample(const htex *t, float u, float v, float out[4])
{
	if (!t || !t->px) {
		out[0] = out[1] = out[2] = out[3] = 1;
		return;
	}
	float fx = u * (float)t->w - 0.5f, fy = v * (float)t->h - 0.5f;
	int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
	float ax = fx - (float)x0, ay = fy - (float)y0;
	float acc[4] = {0};
	for (int k = 0; k < 4; k++) {
		int sx = ((x0 + (k & 1)) % t->w + t->w) % t->w, sy = ((y0 + (k >> 1)) % t->h + t->h) % t->h;
		float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
		const uint8_t *p = t->px + (sy * t->w + sx) * 4;
		for (int c = 0; c < 4; c++)
			acc[c] += wgt * (float)p[c] / 255.0f;
	}
	memcpy(out, acc, sizeof acc);
}

static void blend_px(float *d, const float s[4], int mode)
{
	float a = s[3];
	for (int c = 0; c < 3; c++) {
		float r;
		switch (mode) {
		case 0: r = s[c]; break;
		case 2: r = s[c] * a + d[c]; break;
		case 4: r = d[c] * s[c]; break;
		case 5: r = s[c] + d[c]; break;
		case 6: r = s[c] * d[c] + d[c]; break;
		case 7: r = (1 - d[c]) * s[c]; break;
		default: r = s[c] * a + d[c] * (1 - a); break;
		}
		d[c] = r > 1 ? 1 : r < 0 ? 0 : r;
	}
}

static void raster_tri(const rv *v0, const rv *v1, const rv *v2, const sg_cmd *c, const htex *t)
{
	/* near-plane (z >= 0) clipping is done by the caller; perspective divide here */
	float sx[3], sy[3], sz[3], iw[3];
	const rv *v[3] = {v0, v1, v2};
	for (int i = 0; i < 3; i++) {
		iw[i] = 1.0f / v[i]->w;
		sx[i] = (v[i]->x * iw[i] * 0.5f + 0.5f) * (float)W;
		sy[i] = (0.5f - v[i]->y * iw[i] * 0.5f) * (float)H;
		sz[i] = v[i]->z * iw[i];
	}
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
	if (area == 0)
		return;
	/* D3D: front = clockwise on screen (y down) => area > 0 */
	if ((c->cull == 3 && area < 0) || (c->cull == 2 && area > 0))
		return;
	int minx = (int)floorf(fminf(sx[0], fminf(sx[1], sx[2]))), maxx = (int)ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2])));
	int miny = (int)floorf(fminf(sy[0], fminf(sy[1], sy[2]))), maxy = (int)ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2])));
	if (minx < 0) minx = 0;
	if (miny < 0) miny = 0;
	if (maxx > W - 1) maxx = W - 1;
	if (maxy > H - 1) maxy = H - 1;
	float fc = (float)(c->fogColor >> 16 & 255) / 255.0f, fcg = (float)(c->fogColor >> 8 & 255) / 255.0f,
	      fcb = (float)(c->fogColor & 255) / 255.0f;
	for (int y = miny; y <= maxy; y++)
		for (int x = minx; x <= maxx; x++) {
			float px = (float)x + 0.5f, py = (float)y + 0.5f;
			float b0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
			float b1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
			float b2 = 1 - b0 - b1;
			if (b0 < 0 || b1 < 0 || b2 < 0)
				continue;
			float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
			float *zp = &zb[y * W + x];
			if (c->zenable && z > *zp)
				continue;
			float p0 = b0 * iw[0], p1 = b1 * iw[1], p2 = b2 * iw[2], ps = p0 + p1 + p2;
			p0 /= ps;
			p1 /= ps;
			p2 /= ps;
			float u = p0 * v0->u + p1 * v1->u + p2 * v2->u, vv = p0 * v0->v + p1 * v1->v + p2 * v2->v;
			float col[4], tx[4];
			for (int k = 0; k < 4; k++)
				col[k] = p0 * v0->c[k] + p1 * v1->c[k] + p2 * v2->c[k];
			float f = p0 * v0->fog + p1 * v1->fog + p2 * v2->fog;
			sample(t, u, vv, tx);
			for (int k = 0; k < 4; k++)
				col[k] *= tx[k];
			if (f < 1) {
				col[0] = col[0] * f + fc * (1 - f);
				col[1] = col[1] * f + fcg * (1 - f);
				col[2] = col[2] * f + fcb * (1 - f);
			}
			blend_px(&fb[(y * W + x) * 3], col, c->blend);
			if (c->zenable && c->zwrite)
				*zp = z;
		}
}

static void clip_tri(rv v[3], const sg_cmd *c, const htex *t)
{
	/* clip against z >= 0 (D3D near plane) */
	rv in[4], out[4];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		const rv *a = &v[i], *b = &v[(i + 1) % 3];
		bool ai = a->z >= 0, bi = b->z >= 0;
		if (ai)
			out[n++] = *a;
		if (ai != bi) {
			float s = a->z / (a->z - b->z);
			rv r;
			float *pa = (float *)a, *pb = (float *)b, *pr = (float *)&r;
			for (size_t k = 0; k < sizeof(rv) / sizeof(float); k++)
				pr[k] = pa[k] + (pb[k] - pa[k]) * s;
			out[n++] = r;
		}
	}
	memcpy(in, out, sizeof out);
	for (int i = 1; i + 1 < n; i++)
		raster_tri(&in[0], &in[i], &in[i + 1], c, t);
}

static void xform(const float p[3], const sg_mat *m, float o[4])
{
	for (int j = 0; j < 4; j++)
		o[j] = p[0] * m->m[0][j] + p[1] * m->m[1][j] + p[2] * m->m[2][j] + m->m[3][j];
}

static void draw_cmd(const sg_cmd *c)
{
	if (c->clear == 1) {
		for (int i = 0; i < W * H; i++) {
			fb[i * 3] = (float)(c->fogColor >> 16 & 255) / 255.0f;
			fb[i * 3 + 1] = (float)(c->fogColor >> 8 & 255) / 255.0f;
			fb[i * 3 + 2] = (float)(c->fogColor & 255) / 255.0f;
		}
		return;
	}
	if (c->clear == 2) {
		for (int i = 0; i < W * H; i++)
			zb[i] = 1.0f;
		return;
	}
	sg_mat wv, wvp;
	sg_mat_mul(&wv, &c->world, c->view);
	sg_mat_mul(&wvp, &wv, c->proj);
	const htex *t = c->tex[0] ? &texs[c->tex[0]] : NULL;
	if (c->pvtx) {
		for (int i = 0; i + 2 < c->npv; i += 3) {
			rv v[3];
			for (int k = 0; k < 3; k++) {
				const float *s = c->pvtx + (i + k) * 6;
				float o[4];
				xform(s, &wvp, o);
				v[k].x = o[0]; v[k].y = o[1]; v[k].z = o[2]; v[k].w = o[3];
				v[k].u = s[3];
				v[k].v = s[4];
				uint32_t argb;
				memcpy(&argb, &s[5], 4);
				v[k].c[0] = (float)(argb >> 16 & 255) / 255.0f;
				v[k].c[1] = (float)(argb >> 8 & 255) / 255.0f;
				v[k].c[2] = (float)(argb & 255) / 255.0f;
				v[k].c[3] = (float)(argb >> 24) / 255.0f;
				v[k].fog = 1;
			}
			clip_tri(v, c, t);
		}
		return;
	}
	const sg_geom *g = c->geom;
	rv *tv = malloc(sizeof(rv) * (size_t)g->nv);
	float fa = 0, fbb = 1;
	if (c->fogEnd != c->fogStart) {
		fa = -1.0f / (c->fogEnd - c->fogStart);
		fbb = c->fogEnd / (c->fogEnd - c->fogStart);
	}
	for (int i = 0; i < g->nv; i++) {
		const float *s = g->vtx + i * 8;
		float o[4];
		xform(s, &wvp, o);
		rv *r = &tv[i];
		r->x = o[0]; r->y = o[1]; r->z = o[2]; r->w = o[3];
		r->u = s[6] + c->uvOff[0];
		r->v = s[7] + c->uvOff[1];
		switch (c->lightType) {
		case SG_LIGHT_NONE:
			memcpy(r->c, c->color, sizeof r->c);
			break;
		case SG_LIGHT_DIR:
		case SG_LIGHT_HEMI: {
			float d = s[3] * c->lightObj[0] + s[4] * c->lightObj[1] + s[5] * c->lightObj[2] + c->ambient;
			for (int k = 0; k < 3; k++) {
				float v = d * c->lightColor[k] * c->color[k];
				r->c[k] = v > 1 ? 1 : v < 0 ? 0 : v;
			}
			r->c[3] = c->color[3] > 1 ? 1 : c->color[3];
			break;
		}
		default:
			r->c[0] = r->c[1] = r->c[2] = r->c[3] = 1;
		}
		r->fog = 1;
		if (c->fogType == SG_FOG_VIEW) {
			float vz[4];
			xform(s, &wv, vz);
			r->fog = vz[2] * fa + fbb;
		} else if (c->fogType == SG_FOG_WORLD) {
			float wp[4];
			xform(s, &c->world, wp);
			r->fog = (wp[0] * wp[0] + wp[1] * wp[1] + wp[2] * wp[2]) * fa + fbb;
		}
		if (r->fog > 1) r->fog = 1;
		if (r->fog < 0) r->fog = 0;
	}
	for (int i = 0; i + 2 < g->ni; i += 3) {
		rv v[3] = {tv[g->idx[i]], tv[g->idx[i + 1]], tv[g->idx[i + 2]]};
		clip_tri(v, c, t);
	}
	free(tv);
}

/* ---------------------------------------------------------------- PNG */

static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const uint8_t *b, size_t n)
{
	if (!crc_tab[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t r = i;
			for (int k = 0; k < 8; k++)
				r = r & 1 ? 0xedb88320u ^ (r >> 1) : r >> 1;
			crc_tab[i] = r;
		}
	c = ~c;
	for (size_t i = 0; i < n; i++)
		c = crc_tab[(c ^ b[i]) & 255] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
	uint8_t h[8];
	put32(h, n);
	memcpy(h + 4, type, 4);
	fwrite(h, 1, 8, f);
	fwrite(d, 1, n, f);
	uint32_t c = crc(crc(0, (const uint8_t *)type, 4), d, n);
	put32(h, c);
	fwrite(h, 1, 4, f);
}

static void write_png(const char *path)
{
	size_t raw = (size_t)H * (W * 3 + 1);
	uint8_t *r = malloc(raw);
	for (int y = 0; y < H; y++) {
		r[y * (W * 3 + 1)] = 0;
		for (int x = 0; x < W * 3; x++)
			r[y * (W * 3 + 1) + 1 + x] = (uint8_t)(fb[y * W * 3 + x] * 255.0f + 0.5f);
	}
	size_t nb = (raw + 65534) / 65535;
	uint8_t *z = malloc(raw + nb * 5 + 6);
	size_t o = 0;
	z[o++] = 0x78;
	z[o++] = 1;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw; i++) {
		a = (a + r[i]) % 65521;
		b = (b + a) % 65521;
	}
	for (size_t p = 0; p < raw; p += 65535) {
		size_t n = raw - p < 65535 ? raw - p : 65535;
		z[o++] = p + n >= raw;
		z[o++] = n & 255;
		z[o++] = n >> 8;
		z[o++] = ~n & 255;
		z[o++] = (~n >> 8) & 255;
		memcpy(z + o, r + p, n);
		o += n;
	}
	put32(z + o, b << 16 | a);
	o += 4;
	FILE *f = fopen(path, "wb");
	if (!f) {
		perror(path);
		exit(1);
	}
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	uint8_t ih[13];
	put32(ih, (uint32_t)W);
	put32(ih + 4, (uint32_t)H);
	ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
	chunk(f, "IHDR", ih, 13);
	chunk(f, "IDAT", z, (uint32_t)o);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(r);
	free(z);
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: stagepreview <stageId> <ticks> <zoom> <out.png> [-3ds] [-romfs dir] [-tex dir]\n");
		return 2;
	}
	int id = atoi(argv[1]), ticks = atoi(argv[2]);
	float zoom = (float)atof(argv[3]);
	stage_g.lod = STAGE_LOD_EXACT; /* reference renderer: original behaviour unless -lod */
	for (int i = 5; i < argc; i++) {
		if (!strcmp(argv[i], "-3ds")) {
			W = 400;
			H = 240;
		} else if (!strcmp(argv[i], "-romfs") && i + 1 < argc)
			romfs = argv[++i];
		else if (!strcmp(argv[i], "-lod") && i + 1 < argc)
			stage_g.lod = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-tex") && i + 1 < argc)
			texroot = argv[++i];
	}
	sg_be.read_file = read_file;
	sg_be.tex_load = tex_load;
	sg_be.tex_free = tex_free;
	aos_mt_seed(&stage_g.rng, 5489);
	stage_g.gameMode = 2;
	stage_g.zoom = zoom;
	stage_g.aspect = (float)W / (float)H;
	stage_begin(id);
	for (int t = 0; t < ticks; t++) {
		stage_g.matchFrame = t;
		stage_update();
	}
	stage_draw();
	fb = calloc((size_t)W * H * 3, sizeof(float));
	zb = malloc((size_t)W * H * sizeof(float));
	for (int i = 0; i < W * H; i++)
		zb[i] = 1.0f;
	/* gameplay layer clear colour 0xff0055aa (FUN_005f91d0); stages may clear again */
	float clr[3] = {0, 0x55 / 255.0f, 0xaa / 255.0f};
	for (int i = 0; i < W * H; i++)
		memcpy(&fb[i * 3], clr, sizeof clr);
	for (int i = 0; i < sg_cmds.n; i++)
		draw_cmd(&sg_cmds.cmd[i]);
	write_png(argv[4]);
	printf("%s: stage %d (%s), %d commands\n", argv[4], stage_current(), stage_name(stage_current()), sg_cmds.n);
	stage_end();
	return 0;
}
