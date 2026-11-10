/* texusage: which atlas frames are drawn how (memory-plan input, docs/PERF.md "Texture memory plan").
 *
 * Runs CPU vs CPU matches (difficulty 2, hyper pinned at 3.0) for the given pairings, builds the draw
 * list every frame like the 3DS renderer (src/platform/render.c: same quad size, zoom and off-screen
 * cull) and accumulates, per texture name:
 *   - the blend modes it is drawn with (AOS_BLEND_KEEP resolved to the previous mode),
 *   - covered screen pixels (fill), draw count,
 *   - magnification m = screen pixels per stored texel at the current atlas scale (aos2pack -scale 0.5);
 *     m < 1 = minified. Halving the atlas doubles m.
 * Frame sizes come from build/romfs/gfx/<folder>.idx ("name page idx w h ow oh").
 * Usage: texusage <frames> <idx dir> <out.tsv> a b [a b ...]
 * Output: per folder summary on stdout; per texture rows in out.tsv:
 *   folder name draws px blendmask mP10 mP50 mP90 mMax   (m percentiles weighted by covered pixels) */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 8192
#define NB 24 /* m histogram: bucket k covers m in [2^((k-12)/2), 2^((k-11)/2)) */

typedef struct {
	char folder[32], name[32];
	int w, h, ow, oh;
	double px, draws;
	unsigned blendmask;
	double hist[NB];
	float mmax;
} texinfo;

static texinfo T[MAXT];
static int nT;
static int by_tex[8192]; /* aos_tex id -> T index + 1 */

static int load_idx(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		size_t L = strlen(e->d_name);
		if (L < 5 || strcmp(e->d_name + L - 4, ".idx"))
			continue;
		char folder[64];
		snprintf(folder, sizeof folder, "%.*s", (int)(L - 4), e->d_name);
		/* palette B/C folders duplicate A's names: keep A (same sizes) */
		size_t fl = strlen(folder);
		if (!strncmp(folder, "chara", 5) && fl > 6 && (folder[fl - 1] == 'B' || folder[fl - 1] == 'C') &&
		    strcmp(folder, "charaCmnA"))
			continue;
		char path[512];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		char name[64];
		int pg, ix, w, h, ow, oh;
		while (nT < MAXT && fscanf(f, "%63s %d %d %d %d %d %d", name, &pg, &ix, &w, &h, &ow, &oh) == 7) {
			texinfo *t = &T[nT++];
			snprintf(t->folder, sizeof t->folder, "%s", folder);
			snprintf(t->name, sizeof t->name, "%s", name);
			t->w = w, t->h = h, t->ow = ow, t->oh = oh;
		}
		fclose(f);
	}
	closedir(d);
	return nT;
}

static texinfo *find(int tex)
{
	if (tex < 0 || tex >= 8192)
		return NULL;
	if (by_tex[tex])
		return by_tex[tex] > 0 ? &T[by_tex[tex] - 1] : NULL;
	const char *n = aos_tex_name(tex);
	int best = -1;
	for (int i = 0; i < nT; i++)
		if (!strcmp(T[i].name, n)) {
			/* a name in several folders: prefer a character folder (render.c looks in the player's first) */
			if (best < 0 || !strncmp(T[i].folder, "chara", 5))
				best = i;
		}
	by_tex[tex] = best >= 0 ? best + 1 : -1;
	return best >= 0 ? &T[best] : NULL;
}

static aos_world W;

static void account(const aos_world *w)
{
	float Z = w->obj.zoomView > 0 ? w->obj.zoomView : (w->obj.zoom > 0 ? w->obj.zoom : 1.0f);
	float zoomDiv = Z / 0.4f;
	int cur = AOS_BLEND_ALPHA;
	for (int i = 0; i < aos_dl.n; i++) {
		const aos_dcmd *c = &aos_dl.cmd[i];
		const char *tn = aos_tex_name(c->tex);
		if (c->blend != AOS_BLEND_KEEP)
			cur = c->blend;
		if (tn[0] == '$')
			continue;
		texinfo *t = find(c->tex);
		if (!t || !t->ow || !t->oh)
			continue;
		float wW, wH, texW;
		if (c->halfW > 0) {
			wW = 2.0f * c->halfW;
			wH = 2.0f * (c->halfH > 0 ? c->halfH : c->halfW);
		} else if (c->hasSrc) {
			wW = c->src[2] - c->src[0];
			wH = c->src[3] - c->src[1];
		} else {
			wW = (float)t->ow;
			wH = (float)t->oh;
		}
		texW = c->hasSrc ? fabsf(c->src[2] - c->src[0]) * (float)t->w / (float)t->ow : (float)t->w;
		wW *= c->sx;
		wH *= c->sy;
		float sx = 200 + (c->x - w->obj.camX) / zoomDiv, sy = 120 - (c->y - w->obj.camY) / zoomDiv;
		float pw = fabsf(wW / zoomDiv), ph = fabsf(wH / zoomDiv);
		if (pw < 0.5f || ph < 0.5f)
			continue;
		float ex = pw, ey = ph;
		if (c->rotZ)
			ex = ey = ex + ey;
		if (sx + ex < -64 || sx - ex > 464 || sy + ey < -64 || sy - ey > 304)
			continue;
		/* clipped covered area (bounding box of the rotated quad, scaled back to the quad's area) */
		float a = -(float)c->rotZ * (6.2831855f / 65536.0f), ca = fabsf(cosf(a)), sa = fabsf(sinf(a));
		float bw = pw * ca + ph * sa, bh = pw * sa + ph * ca;
		float x0 = fmaxf(0, sx - bw / 2), x1 = fminf(400, sx + bw / 2), y0 = fmaxf(0, sy - bh / 2), y1 = fminf(240, sy + bh / 2);
		double area = (x1 > x0 && y1 > y0) ? (double)(x1 - x0) * (y1 - y0) * (pw * ph) / (bw * bh) : 0;
		float m = texW > 0 ? pw / texW : 1;
		int k = (int)floorf(2.0f * log2f(m)) + 12;
		k = k < 0 ? 0 : k >= NB ? NB - 1 : k;
		t->hist[k] += area + 1e-3; /* off-screen-but-not-culled quads still count a little */
		t->px += area;
		t->draws += 1;
		t->blendmask |= 1u << cur;
		if (m > t->mmax)
			t->mmax = m;
	}
}

static float pct(const double *h, double q)
{
	double tot = 0, run = 0;
	for (int k = 0; k < NB; k++)
		tot += h[k];
	if (tot <= 0)
		return 0;
	for (int k = 0; k < NB; k++) {
		run += h[k];
		if (run >= q * tot)
			return exp2f((float)(k - 11) / 2.0f); /* bucket upper bound */
	}
	return exp2f((float)(NB - 11) / 2.0f);
}

int main(int argc, char **argv)
{
	if (argc < 6) {
		fprintf(stderr, "usage: texusage <frames> <idx dir> <out.tsv> a b [a b ...]\n");
		return 2;
	}
	aos_register_all();
	int frames = atoi(argv[1]);
	if (!load_idx(argv[2])) {
		fprintf(stderr, "no .idx in %s\n", argv[2]);
		return 1;
	}
	for (int ai = 4; ai + 1 < argc; ai += 2) {
		int a = atoi(argv[ai]), b = atoi(argv[ai + 1]);
		int32_t ids[2] = {a, b}, sch[2] = {0, 0}, col[2] = {0, a == b ? 1 : 0};
		aos_match_init(&W, ids, sch, col, 180, 2);
		W.difficulty = 2;
		W.p[0].inputSrc = W.p[1].inputSrc = 10;
		uint16_t m[2] = {0, 0};
		for (int f = 1; f <= frames; f++) {
			W.p[0].hyper = W.p[1].hyper = 3.0f;
			aos_tick(&W, m);
			aos_draw_begin();
			aos_draw_world(&W);
			account(&W);
			if (W.flow.matchEnd)
				aos_match_restart(&W);
		}
		fprintf(stderr, "%d vs %d done\n", a, b);
	}
	FILE *o = fopen(argv[3], "w");
	/* per folder */
	struct {
		char folder[32];
		double px, draws, hist[NB];
		unsigned used, total;
	} F[128];
	int nF = 0;
	for (int i = 0; i < nT; i++) {
		texinfo *t = &T[i];
		int j = 0;
		for (; j < nF && strcmp(F[j].folder, t->folder); j++)
			;
		if (j == nF) {
			memset(&F[nF], 0, sizeof F[nF]);
			snprintf(F[nF].folder, sizeof F[nF].folder, "%s", t->folder);
			nF++;
		}
		F[j].total++;
		if (!t->draws)
			continue;
		F[j].used++;
		F[j].px += t->px;
		F[j].draws += t->draws;
		for (int k = 0; k < NB; k++)
			F[j].hist[k] += t->hist[k];
		if (o)
			fprintf(o, "%s\t%s\t%.0f\t%.0f\t%x\t%.2f\t%.2f\t%.2f\t%.2f\t%d\t%d\n", t->folder, t->name, t->draws, t->px,
			        t->blendmask, (double)pct(t->hist, 0.1), (double)pct(t->hist, 0.5), (double)pct(t->hist, 0.9),
			        (double)t->mmax, t->w, t->h);
	}
	if (o)
		fclose(o);
	printf("folder            used/total  draws/frame  kpx/frame  m P10   P50   P90 (screen px per stored texel, by covered px)\n");
	double nfr = (double)frames * (double)((argc - 4) / 2);
	for (int j = 0; j < nF; j++)
		if (F[j].used)
			printf("%-16s %5u/%-5u  %10.1f  %12.1f  %5.2f %5.2f %5.2f\n", F[j].folder, F[j].used, F[j].total,
			       F[j].draws / nfr, F[j].px / nfr / 1000.0, (double)pct(F[j].hist, 0.1),
			       (double)pct(F[j].hist, 0.5), (double)pct(F[j].hist, 0.9));
	return 0;
}
