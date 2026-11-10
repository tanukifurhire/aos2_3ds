/* drawdump: run a scripted match on the host and dump the draw list of chosen
 * frames as TSV for tools/drawpng (renders PNGs with the real game textures).
 *
 * Usage: drawdump <p1> <p2> <script> <frames-to-dump> [out.tsv]
 *   <script>: comma-separated steps "N:keys" = hold keys for N frames, keys from
 *     U D L R (directions) a b s d h g (g = shield) or '-' for none;
 *     prefix "2/" to the keys for P2, e.g. "220:-,1:a,40:-,10:2/b".
 *     Special steps: "hyper" (both players hyper=3), "near" (P2 placed 120 px
 *     right of P1), "far" (P2 placed 400 px right), "cpu1"/"cpu2" (P2 = CPU at
 *     that difficulty), "state:<hex>" (force P1 into a state: aos_set_state).
 *   <frames-to-dump>: comma-separated absolute frame numbers or ranges a-b.
 * Output TSV: one header line per dumped frame
 *   "#frame <n> cam <x> <y> <zoom> p1 <id> <col> p2 <id> <col> st <s1> <t1> <s2> <t2>"
 * then one line per command:
 *   tex bank blend x y halfW halfH sx sy rotZ flipX rgba hasSrc s0 s1 s2 s3 */
#include "../src/sim/draw.h"
#include "../src/sim/sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *name; int id; } chars[] = {
	{"sora", 3}, {"alte", 4}, {"tsih", 5}, {"mira", 6}, {"sham", 7}, {"nath", 8},
	{"starbreaker", 9}, {"suguri", 10}, {"saki", 11}, {"iru", 12}, {"nanako", 13},
	{"kae", 14}, {"kyoko", 15}, {"hime", 16}, {"sumika", 17}, {"sumika2", 18},
};
static int char_id(const char *n)
{
	for (size_t i = 0; i < sizeof chars / sizeof chars[0]; i++)
		if (!strcmp(chars[i].name, n))
			return chars[i].id;
	return atoi(n);
}

static uint16_t keys_mask(const char *k)
{
	uint16_t m = 0;
	for (; *k && *k != ','; k++)
		switch (*k) {
		case 'U': m |= AOS_IN_UP; break;
		case 'D': m |= AOS_IN_DOWN; break;
		case 'L': m |= AOS_IN_LEFT; break;
		case 'R': m |= AOS_IN_RIGHT; break;
		case 'a': m |= AOS_IN_A; break;
		case 'b': m |= AOS_IN_B; break;
		case 's': m |= AOS_IN_S; break;
		case 'd': m |= AOS_IN_D; break;
		case 'h': m |= AOS_IN_H; break;
		case 'g': m |= AOS_IN_SHIELD; break;
		default: break;
		}
	return m;
}

static int want(const char *spec, int f)
{
	const char *s = spec;
	while (*s) {
		int a = atoi(s), b = a;
		const char *dash = strchr(s, '-'), *comma = strchr(s, ',');
		if (dash && (!comma || dash < comma))
			b = atoi(dash + 1);
		if (f >= a && f <= b)
			return 1;
		if (!comma)
			break;
		s = comma + 1;
	}
	return 0;
}

static aos_world W;

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: drawdump <p1> <p2> <script> <frames> [out.tsv]\n");
		return 2;
	}
	aos_register_all();
	int32_t ids[2] = {char_id(argv[1]), char_id(argv[2])}, sch[2] = {0, 0}, col[2] = {0, ids[0] == ids[1]};
	aos_match_init(&W, ids, sch, col, 999, 2);
	FILE *out = argc > 5 ? fopen(argv[5], "w") : stdout;
	if (!out)
		return 1;
	char *script = strdup(argv[3]);
	int frame = 0;
	for (char *tok = strtok(script, ","); tok; tok = strtok(NULL, ",")) {
		if (!strcmp(tok, "hyper")) { W.p[0].hyper = W.p[1].hyper = 3.0f; continue; }
		if (!strcmp(tok, "near")) { W.p[1].x = W.p[0].x + 120; W.p[1].y = W.p[0].y; continue; }
		if (!strcmp(tok, "far")) { W.p[1].x = W.p[0].x + 400; W.p[1].y = W.p[0].y; continue; }
		if (!strncmp(tok, "cpu", 3)) { W.p[1].inputSrc = 10; W.difficulty = atoi(tok + 3); continue; }
		if (!strncmp(tok, "state:", 6)) { aos_set_state(&W, &W.p[0], (int32_t)strtol(tok + 6, NULL, 16)); continue; }
		int n = atoi(tok);
		char *c = strchr(tok, ':');
		const char *k = c ? c + 1 : "-";
		int p2 = !strncmp(k, "2/", 2);
		uint16_t m = keys_mask(p2 ? k + 2 : k);
		for (int i = 0; i < n; i++, frame++) {
			uint16_t masks[2] = {p2 ? 0 : m, p2 ? m : 0};
			aos_tick(&W, masks);
			if (want(argv[4], frame)) {
				aos_draw_begin();
				aos_draw_world(&W);
				fprintf(out, "#frame %d cam %.2f %.2f %.4f p1 %d %d p2 %d %d st %x %d %x %d\n", frame,
				        W.obj.camX, W.obj.camY, W.obj.zoomView > 0 ? W.obj.zoomView : W.obj.zoom,
				        (int)W.p[0].id, (int)W.p[0].color, (int)W.p[1].id, (int)W.p[1].color,
				        (unsigned)W.p[0].state, (int)W.p[0].timer, (unsigned)W.p[1].state, (int)W.p[1].timer);
				for (int j = 0; j < aos_dl.n; j++) {
					const aos_dcmd *d = &aos_dl.cmd[j];
					fprintf(out, "%s\t%d\t%u\t%.2f\t%.2f\t%.2f\t%.2f\t%.4f\t%.4f\t%u\t%u\t%08x\t%u\t%.1f\t%.1f\t%.1f\t%.1f\n",
					        aos_tex_name(d->tex), d->bank, d->blend, d->x, d->y, d->halfW, d->halfH, d->sx, d->sy,
					        d->rotZ, d->flipX, (unsigned)d->rgba, d->hasSrc, d->src[0], d->src[1], d->src[2], d->src[3]);
					if (!strcmp(aos_tex_name(d->tex), "$tri"))
						fprintf(out, "#tri %.2f %.2f %.2f %.2f %.2f %.2f %08x %08x %08x\n", d->tri[0], d->tri[1], d->tri[2],
						        d->tri[3], d->tri[4], d->tri[5], (unsigned)d->triRgba[0], (unsigned)d->triRgba[1], (unsigned)d->triRgba[2]);
				}
			}
		}
	}
	if (out != stdout)
		fclose(out);
	return 0;
}
