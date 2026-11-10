/* Rollback netplay test: two rb_sessions in one process over a fake link with
 * latency, jitter (=> reordering), packet loss, a late-starting peer and clock
 * drift. Both peers feed random inputs (a pure function of player and frame,
 * so a reference run can reproduce them). Asserts:
 *  - both peers end with byte-identical rb_game (incl. aos_world) at frame N;
 *  - that state equals a reference run without any network (zero latency);
 *  - drawing the presented world never changes it (render runs on predicted
 *    states only on one console, so a mutating draw would desync);
 *  - no desync reports; an injected corruption IS reported;
 *  - with latency covered by the input delay: no rollbacks, and the sounds
 *    played equal the reference sounds frame for frame.
 * Usage: test_rollback [p1char] [p2char] [frames]   (engine ids, default 10 3 4000) */
#include "../src/net/rollback.h"
#include "../src/sim/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t mix(uint32_t x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}

/* Random but plausible input: held for blocks of 4..19 frames, with taps. */
static uint16_t gen_input(uint32_t seed, int player, int32_t f)
{
	uint32_t blk = (uint32_t)f / 12u;
	uint32_t h = mix(seed * 31u + (uint32_t)player * 0x9e3779b9u + blk * 0x85ebca6bu);
	uint16_t m = (uint16_t)(h & 0x3ff);
	if ((h >> 10 & 3) == 0)
		m &= 0x0f; /* sometimes just move */
	if ((mix(h ^ (uint32_t)f) & 15) == 0)
		m ^= (uint16_t)(1u << (mix((uint32_t)f + h) % 10)); /* tap */
	return m;
}

static uint16_t ref_input(uint32_t seed, int player, int32_t f, int delay)
{
	return f < delay ? 0 : gen_input(seed, player, f);
}

/* ---- fake link ---- */
typedef struct {
	double t;
	int to;
	size_t len;
	uint8_t data[RB_MAX_PACKET];
} pkt;

#define MAXQ 8192
static pkt q[MAXQ];
static int nq;
static uint32_t rng_state = 1;
static double frand(void) { rng_state = rng_state * 1664525u + 1013904223u; return (rng_state >> 8) / 16777216.0; }

typedef struct {
	const char *name;
	double latency_ms, jitter_ms, loss;
	int delay, max_rollback;
	double start_b_ms;   /* peer B starts later */
	double period_b_ms;  /* B's frame period (drift) */
	int expect_no_rollback;
	int corrupt_at;      /* >0: flip a byte in peer 1's world at this frame (desync detection) */
	int draw;            /* run the draw handlers on presented states */
} scenario;

static void send_pkt(int to, const uint8_t *d, size_t n, double now, const scenario *sc)
{
	if (frand() < sc->loss || nq >= MAXQ)
		return;
	pkt *p = &q[nq++];
	p->t = now + sc->latency_ms + frand() * sc->jitter_ms;
	p->to = to;
	p->len = n;
	memcpy(p->data, d, n);
}

/* reference sound log */
#define SE_LOG 200000
static uint8_t ref_se[SE_LOG][2]; /* (frame & 0xff, id) in play order */
static int ref_se_n;
static rb_game ref;
static uint32_t ref_seed;

static void build_reference(const rb_match *m, int frames, int delay, uint32_t seed)
{
	rb_game_init(&ref, m);
	ref_se_n = 0;
	for (int32_t f = 0; f < frames; f++) {
		uint16_t masks[2] = {ref_input(seed, 0, f, delay), ref_input(seed, 1, f, delay)};
		rb_game_step(&ref, masks);
		for (int i = 0; i < ref.w.se_count && ref_se_n < SE_LOG; i++) {
			ref_se[ref_se_n][0] = (uint8_t)f;
			ref_se[ref_se_n++][1] = ref.w.se[i];
		}
	}
}

static int draw_mutations;
static void draw_check(rb_session *s)
{
	const rb_game *g = rb_state(s);
	uint32_t before = rb_game_hash(g);
	aos_draw_begin();
	aos_draw_world((aos_world *)&g->w); /* what render_world does */
	if (rb_game_hash(g) != before) /* anything beyond the known presentation byte */
		draw_mutations++;
}

static int run(const scenario *sc, const rb_match *m, int frames, uint32_t seed)
{
	printf("-- %s: latency %.0f ms, jitter %.0f ms, loss %.0f%%, delay %d, max rollback %d\n", sc->name,
	       sc->latency_ms, sc->jitter_ms, sc->loss * 100, sc->delay, sc->max_rollback);
	int fail0 = failures;
	nq = 0;
	rng_state = seed ^ 0xabcdef;
	build_reference(m, frames, sc->delay, seed);

	rb_session *s[2];
	for (int i = 0; i < 2; i++) {
		rb_config c;
		rb_config_default(&c, i);
		c.input_delay = sc->delay;
		c.max_rollback = sc->max_rollback;
		c.timeout_ms = 1u << 30;
		s[i] = rb_create(&c, m, 0);
		CHECK(s[i] != NULL, "rb_create failed");
		if (!s[i])
			return 1;
	}
	double next[2] = {0, sc->start_b_ms}, period[2] = {1000.0 / 60.0, sc->period_b_ms};
	int se_n[2] = {0, 0}, se_match = 1;
	int32_t se_ref_pos[2] = {0, 0};
	bool corrupted = false;
	int display_frames[2] = {0, 0};
	double t = 0;
	for (int step = 0; step < 400000; step++, t += 1.0) {
		/* deliver */
		for (int k = 0; k < nq;) {
			if (q[k].t <= t) {
				rb_receive(s[q[k].to], q[k].data, q[k].len, (uint32_t)t);
				q[k] = q[--nq];
			} else
				k++;
		}
		bool done = true;
		for (int i = 0; i < 2; i++) {
			const rb_game *g = rb_state(s[i]);
			rb_stats st;
			rb_get_stats(s[i], &st);
			if (g->frame < frames || st.confirmed < frames + sc->delay)
				done = false;
			if (t < next[i])
				continue;
			next[i] += period[i];
			display_frames[i]++;
			if (g->frame < frames) {
				int32_t lf = g->frame + sc->delay;
				rb_status r = rb_advance(s[i], gen_input(seed, i, lf), (uint32_t)t);
				CHECK(r != RB_DISCONNECTED, "peer %d disconnected", i);
			} else {
				rb_sync(s[i]);
			}
			uint8_t ids[64];
			int n = rb_take_sounds(s[i], ids, 64);
			for (int k = 0; k < n; k++) {
				/* sounds in reference order? (only exact when nothing is mispredicted) */
				if (se_ref_pos[i] < ref_se_n && ref_se[se_ref_pos[i]][1] == ids[k])
					se_ref_pos[i]++;
				else
					se_match = 0;
			}
			se_n[i] += n;
			if (sc->draw && i == 0)
				draw_check(s[i]);
			if (sc->corrupt_at && i == 1 && !corrupted && g->frame == sc->corrupt_at) {
				((rb_game *)g)->w.p[0].hp -= 1; /* simulated desync */
				corrupted = true;
			}
			uint8_t buf[RB_MAX_PACKET];
			size_t len = rb_build_packet(s[i], buf, sizeof buf, (uint32_t)t);
			CHECK(len > 0, "packet build failed");
			send_pkt(1 - i, buf, len, t, sc);
		}
		if (done)
			break;
	}
	rb_sync(s[0]);
	rb_sync(s[1]);
	rb_stats st[2];
	for (int i = 0; i < 2; i++) {
		rb_get_stats(s[i], &st[i]);
		printf("   P%d: frame %d confirmed %d, rollbacks %u (resim %u, max depth %d), waits %u, sync skips %u, "
		       "ping %d ms, crc checks %u, desyncs %u, sounds %d, snapshots %u, %u/%u pkts\n",
		       i + 1, st[i].frame, st[i].confirmed, st[i].rollbacks, st[i].resim_frames, st[i].max_depth,
		       st[i].wait_stalls, st[i].sync_skips, st[i].ping_ms, st[i].crc_checked, st[i].desyncs, se_n[i],
		       st[i].snapshots, st[i].pkts_recv, st[i].pkts_sent);
	}
	const rb_game *a = rb_state(s[0]), *b = rb_state(s[1]);
	CHECK(a->frame == frames && b->frame == frames, "frames %d %d (wanted %d)", a->frame, b->frame, frames);
	if (sc->corrupt_at) {
		CHECK(st[0].desyncs > 0 && st[1].desyncs > 0, "injected desync not detected (%u, %u)", st[0].desyncs,
		      st[1].desyncs);
		CHECK(st[0].desync_frame > sc->corrupt_at, "desync frame %d", st[0].desync_frame);
	} else {
		/* byte-identical, except the presentation byte the draw code writes (only peer 0 draws) */
		static rb_game ca, cb, cr;
		memcpy(&ca, a, sizeof ca);
		memcpy(&cb, b, sizeof cb);
		memcpy(&cr, &ref, sizeof cr);
		rb_clear_presentation(&ca);
		rb_clear_presentation(&cb);
		rb_clear_presentation(&cr); /* the reference's blur byte too (set when a blur is live at the end) */
		CHECK(!memcmp(&ca, &cb, sizeof ca), "peers diverged");
		CHECK(!memcmp(&ca, &cr, sizeof ca), "peer state differs from the zero-latency reference");
		if (!sc->draw)
			CHECK(!memcmp(a, b, sizeof *a) && !memcmp(a, &ref, sizeof *a), "raw bytes differ");
		CHECK(st[0].desyncs == 0 && st[1].desyncs == 0, "desync reported (%u, %u) at %d/%d", st[0].desyncs,
		      st[1].desyncs, st[0].desync_frame, st[1].desync_frame);
		CHECK(st[0].crc_checked > 0 && st[1].crc_checked > 0, "no checksums compared");
	}
	if (sc->expect_no_rollback) {
		CHECK(st[0].rollbacks == 0 && st[1].rollbacks == 0, "unexpected rollbacks");
		CHECK(se_match && se_n[0] == ref_se_n && se_n[1] == ref_se_n,
		      "sounds differ from reference (%d %d vs %d)", se_n[0], se_n[1], ref_se_n);
	} else {
		/* rollbacks: allow mispredicted extras / dropped stale ones, but no runaway duplicates */
		for (int i = 0; i < 2; i++)
			CHECK(se_n[i] <= ref_se_n + ref_se_n / 4 + 8, "P%d played %d sounds, reference %d", i + 1, se_n[i],
			      ref_se_n);
	}
	if (sc->draw)
		CHECK(draw_mutations == 0, "draw changed the world %d times", draw_mutations);
	printf("   memory per session: %zu bytes; %s\n", rb_memory_bytes(s[0]), failures == fail0 ? "ok" : "FAIL");
	rb_destroy(s[0]);
	rb_destroy(s[1]);
	return failures != fail0;
}

int main(int argc, char **argv)
{
	aos_register_all();
	rb_match m;
	memset(&m, 0, sizeof m);
	m.ids[0] = argc > 1 ? atoi(argv[1]) : 10;
	m.ids[1] = argc > 2 ? atoi(argv[2]) : 3;
	int frames = argc > 3 ? atoi(argv[3]) : 4000;
	m.colors[1] = m.ids[0] == m.ids[1];
	m.roundSeconds = 30; /* short rounds so round/match transitions get rolled back too */
	m.roundsToWin = 2;
	m.handicap[0] = m.handicap[1] = AOS_HANDICAP_DEFAULT;
	ref_seed = 777;
	printf("sizeof(aos_world) %zu, sizeof(rb_game) %zu, fingerprint %08x\n", sizeof(aos_world),
	       sizeof(rb_game), rb_sim_fingerprint());

	static const scenario sc[] = {
		{"lan, delay covers latency", 0, 0, 0, 2, 8, 0, 1000.0 / 60.0, 1, 0, 1},
		{"wireless typical", 25, 20, 0.05, 1, 8, 0, 1000.0 / 60.0, 0, 0, 0},
		{"bad link, late start, drift", 60, 50, 0.20, 0, 8, 170, 1000.0 / 59.5, 0, 0, 0},
		{"latency beyond window", 140, 30, 0.10, 1, 4, 40, 1000.0 / 60.0, 0, 0, 0},
		{"small window, heavy loss", 30, 60, 0.40, 2, 2, 0, 1000.0 / 60.2, 0, 0, 0},
		{"desync detection", 20, 10, 0.05, 1, 8, 0, 1000.0 / 60.0, 0, 1000, 0},
	};
	int fail = 0;
	for (size_t i = 0; i < sizeof sc / sizeof sc[0]; i++)
		fail |= run(&sc[i], &m, frames, ref_seed + (uint32_t)i);

	/* handshake round trip */
	{
		rb_hello h = {RB_PROTO_VERSION, AOS_GAME_VERSION, rb_sim_fingerprint(), 10, 2, 1}, h2;
		uint8_t buf[64];
		size_t n = rb_encode_hello(&h, buf, sizeof buf);
		CHECK(n && rb_decode_hello(buf, n, &h2) && h2.id == 10 && h2.color == 2 && h2.scheme == 1 &&
		      h2.fingerprint == h.fingerprint && h2.proto == RB_PROTO_VERSION, "hello");
		rb_hello c = {RB_PROTO_VERSION, AOS_GAME_VERSION, h.fingerprint, 10, 2, 0};
		rb_start st, st2;
		memset(&st, 0, sizeof st);
		st.proto = RB_PROTO_VERSION;
		st.game_version = AOS_GAME_VERSION;
		st.fingerprint = h.fingerprint;
		st.input_delay = 2;
		st.max_rollback = 8;
		st.nonce = 0xdeadbeef;
		st.bgm = 37;
		st.stage = 16;
		rb_match_from_picks(&st.match, &h, &c, 180, 2);
		CHECK(st.match.colors[1] != st.match.colors[0], "palette clash not resolved");
		CHECK(st.match.handicap[0] == AOS_HANDICAP_DEFAULT && st.match.handicap[1] == AOS_HANDICAP_DEFAULT, "default handicap");
		st.match.handicap[1] = 4;
		n = rb_encode_start(&st, buf, sizeof buf);
		CHECK(n && rb_decode_start(buf, n, &st2) && !memcmp(&st.match, &st2.match, sizeof st.match) &&
		      st2.nonce == 0xdeadbeef && st2.input_delay == 2 && st2.max_rollback == 8 && st2.bgm == 37 && st2.stage == 16, "start");
		uint32_t nonce = 0;
		n = rb_encode_start_ack(42, buf, sizeof buf);
		CHECK(n && rb_decode_start_ack(buf, n, &nonce) && nonce == 42, "start ack");
	}
	printf("%d checks, %d failures: %s\n", checks, failures, failures || fail ? "FAIL" : "ok");
	return failures || fail;
}
