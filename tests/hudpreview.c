/* hudpreview: run a CPU-vs-CPU match on the host and dump the HUD primitives
 * (src/platform/hud.c through a recording hud_gfx.h backend) of chosen frames
 * as TSV for tools/drawpng -hud, which renders them with the original PC
 * textures. Lets the HUD port be checked at the PC's 800x600 and at the 3DS
 * 400x240 layout without a console.
 *
 * Usage: hudpreview <p1> <p2> <frames> <W> <H> <out.tsv> [setup]
 *   <p1>/<p2>: engine ids (10 = Suguri ...), <frames>: comma list of frames or
 *   ranges a-b, setup: comma list of
 *     "hyper=<f>" both players' hyper, "heat=<f>", "hp=<n>" P2 hp,
 *     "combo" show a combo counter on P1, "won=<a>/<b>" rounds won,
 *     "timer=<n>" round seconds, "ko@<frame>" KO P2 at that frame,
 *     "timeout@<frame>" round timer to 1 at that frame, "panel" draw the 3DS
 *     bottom-screen panel instead of the HUD (W,H = 320,240).
 * Line format: see tools/drawpng/hud.go. */
#include "../src/platform/hud.h"
#include "../src/platform/hud_gfx.h"
#include "../src/sim/sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;

static const char *texname(int t)
{
	static const char *const n[HT_COUNT] = {
		[HT_GAUGE] = "systemMdl/gauge", [HT_DAMAGE] = "systemMdl/damage", [HT_BGMNAME] = "systemMdl/bgm_name",
		[HT_FADE] = "systemMdl/fade", [HT_HYPER_B] = "systemMdl/hyperb", [HT_HYPER_G] = "systemMdl/hyperg",
		[HT_HYPER_R] = "systemMdl/hyperr", [HT_NOISE0] = "systemMdl/noise0", [HT_NOISE0 + 1] = "systemMdl/noise1",
		[HT_NOISE0 + 2] = "systemMdl/noise2", [HT_NOISE0 + 3] = "systemMdl/noise3", [HT_NOISE0 + 4] = "systemMdl/noise4",
		[HT_NOISE0 + 5] = "systemMdl/noise5", [HT_NOISE0 + 6] = "systemMdl/noise6", [HT_NOISE0 + 7] = "systemMdl/noise7",
		[HT_SYSTEM] = "systemAddMdl/system", [HT_SYSTEM2] = "systemAddMdl/system2",
		[HT_CROWN_W] = "msgMdl/msg_leadercrownw", [HT_CROWN_L] = "msgMdl/msg_leadercrownl",
		[HT_PORTRAIT0] = "P0", [HT_PORTRAIT1] = "P1",
	};
	return t >= 0 && t < HT_COUNT && n[t] ? n[t] : "?";
}

void hg_image(int t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float angle,
              uint32_t argb, int blend)
{
	fprintf(out, "I\t%s\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%08x\t%d\n", texname(t), x0, y0, x1, y1, u0, v0, u1, v1,
	        angle, argb, blend);
}
void hg_quad(int t, const float x[4], const float y[4], float u0, float v0, float u1, float v1, uint32_t argb, int blend)
{
	fprintf(out, "Q\t%s\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%08x\t%d\n", texname(t), x[0], y[0], x[1], y[1],
	        x[2], y[2], x[3], y[3], u0, v0, u1, v1, argb, blend);
}
void hg_tri(const float x[3], const float y[3], const uint32_t c[3], int blend)
{
	fprintf(out, "T\t%g\t%g\t%g\t%g\t%g\t%g\t%08x\t%08x\t%08x\t%d\n", x[0], y[0], x[1], y[1], x[2], y[2], c[0], c[1], c[2],
	        blend);
}
void hg_tri_tex(int t, const float x[3], const float y[3], const float u[3], const float v[3], uint32_t argb, int blend)
{
	fprintf(out, "X\t%s\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%g\t%08x\t%d\n", texname(t), x[0], y[0], x[1], y[1],
	        x[2], y[2], u[0], v[0], u[1], v[1], u[2], v[2], argb, blend);
}
void hg_rect(float l, float t, float r, float b, uint32_t argb, int blend)
{
	fprintf(out, "R\t%g\t%g\t%g\t%g\t%08x\t%d\n", l, t, r, b, argb, blend);
}
void hg_line(float x0, float y0, float x1, float y1, uint32_t argb, int blend)
{
	fprintf(out, "L\t%g\t%g\t%g\t%g\t%08x\t%d\n", x0, y0, x1, y1, argb, blend);
}

static int want(const char *spec, int f)
{
	const char *s = spec;
	while (*s) {
		int a = atoi(s), b = a;
		while (*s && *s != ',' && *s != '-')
			s++;
		if (*s == '-')
			b = atoi(++s);
		while (*s && *s != ',')
			s++;
		if (*s == ',')
			s++;
		if (f >= a && f <= b)
			return 1;
	}
	return 0;
}

static int at_frame(const char *setup, const char *key, int f)
{
	const char *p = setup;
	size_t n = strlen(key);
	while ((p = strstr(p, key)) != NULL) {
		if (atoi(p + n) == f)
			return 1;
		p += n;
	}
	return 0;
}

static float setf(const char *setup, const char *key, float def)
{
	const char *p = strstr(setup, key);
	return p ? (float)atof(p + strlen(key)) : def;
}

static aos_world W;

int main(int argc, char **argv)
{
	if (argc < 7) {
		fprintf(stderr, "usage: hudpreview p1 p2 frames W H out.tsv [setup]\n");
		return 2;
	}
	const char *setup = argc > 7 ? argv[7] : "";
	aos_register_all();
	int32_t ids[2] = {atoi(argv[1]), atoi(argv[2])}, sch[2] = {0, 0}, col[2] = {0, ids[0] == ids[1]};
	aos_match_init(&W, ids, sch, col, (int32_t)setf(setup, "timer=", 180), 2);
	W.p[0].inputSrc = W.p[1].inputSrc = 10;
	W.difficulty = 2;
	const char *won = strstr(setup, "won=");
	if (won) {
		W.p[0].roundsWon = atoi(won + 4);
		const char *sl = strchr(won, '/');
		W.p[1].roundsWon = sl ? atoi(sl + 1) : 0;
		W.flow.firstInit = 0;
		W.flow.roundIndex = W.p[0].roundsWon + W.p[1].roundsWon - 1;
		aos_round_init(&W);
	}
	out = fopen(argv[6], "w");
	if (!out)
		return 1;
	int scrW = atoi(argv[4]), scrH = atoi(argv[5]);
	hud_screen sc = hud_screen_for(scrW, scrH);
	int last = 0;
	for (const char *s = argv[3]; *s; s++)
		if ((*s >= '0' && *s <= '9') && (s == argv[3] || s[-1] == ',' || s[-1] == '-'))
			if (atoi(s) > last)
				last = atoi(s);
	uint16_t none[2] = {0, 0};
	for (int f = 0; f <= last; f++) {
		if (at_frame(setup, "ko@", f))
			aos_damage(&W, &W.p[1], 100000, 0, 0);
		if (at_frame(setup, "timeout@", f))
			W.roundTimer = 1;
		aos_tick(&W, none);
		if (strstr(setup, "hyper="))
			W.p[0].hyper = W.p[1].hyper = setf(setup, "hyper=", 0);
		if (strstr(setup, "heat="))
			W.p[0].heat = W.p[1].heat = setf(setup, "heat=", 0);
		if (strstr(setup, "hp="))
			W.p[1].hp = W.p[1].redHp = (int)setf(setup, "hp=", 4500);
		if (strstr(setup, "combo")) {
			W.p[0].comboShowDmg = 1234;
			W.p[0].comboShowHits = 7;
			W.p[0].comboFadeA = W.p[0].comboFadeB = 1.0f;
		}
		if (!want(argv[3], f))
			continue;
		float Z = W.obj.zoomView > 0 ? W.obj.zoomView : (W.obj.zoom > 0 ? W.obj.zoom : 1.0f);
		hud_camera cam = {W.obj.camX, W.obj.camY, Z / sc.s};
		fprintf(out, "#hud %d %d %d p1 %d %d p2 %d %d\n", f, scrW, scrH, W.p[0].id, W.p[0].color, W.p[1].id,
		        W.p[1].color);
		if (strstr(setup, "panel"))
			hud_panel_draw(&W, scrW, scrH); /* 3DS bottom-screen panel (use W,H = 320,240) */
		else
			hud_draw(&W, &sc, &cam);
	}
	fclose(out);
	return 0;
}
