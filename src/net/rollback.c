/*
 * Rollback netplay session — see rollback.h for the contract.
 *
 * Frame bookkeeping (f = session frame, cur = next frame to simulate):
 *  - local input for frame f is known for f < cur + input_delay (frames below
 *    input_delay are 0 on both sides, so they are "known" from the start);
 *  - remote input: `confirmed` = number of leading frames whose remote input
 *    is known. Frames >= confirmed that get simulated use a prediction (the
 *    last confirmed remote mask) recorded in the ring;
 *  - a snapshot of the state *before* frame f is saved iff frame f is
 *    simulated with a predicted remote input (or f is a pending checksum
 *    checkpoint), so with enough input delay no snapshot copies happen at all;
 *  - when a remote input arrives for an already simulated frame and differs
 *    from the prediction, the earliest such frame is rolled back to and
 *    everything up to the present is re-simulated (inside the next rb_sync /
 *    rb_advance, so several late inputs cost one rollback);
 *  - we never run more than max_rollback frames past `confirmed`, which bounds
 *    the rollback depth and the snapshot ring (max_rollback + 1 slots).
 *
 * Compact snapshots: aos_world is ~816 KB, almost all of it object pools that
 * are mostly empty (a heavy hyper scene has ~40-190 KB live). The simulation
 * keeps every pool slot past its count zeroed (objects.h AOS_CLEAR_DEAD), so a
 * snapshot stores the fixed part of rb_game plus only the live prefix of each
 * pool, and a restore copies that back and re-zeroes the slots the current
 * state used beyond the snapshot's counts: the restored rb_game is byte-for-byte
 * the saved one. The checkpoint hash covers the same byte stream, so it can be
 * computed on a snapshot without expanding it.
 */
#include "rollback.h"

#include <stdlib.h>
#include <string.h>

#define IN_RING 128 /* input history (power of two) */
#define SE_RING 32  /* per-frame "already played" sound sets */
#define CRC_RING 16
#define MAX_INPUTS_PER_PKT 40
#define SE_OUT_MAX 64
#define NO_FRAME INT32_MAX
#define FRAME_MS (1000.0f / 60.0f)
#define RB_SNAP_RESERVE (256 * 1024) /* initial bytes per snapshot slot */

typedef struct {
	int32_t frame;
	uint16_t mask;
	uint8_t known;
} rb_in;

typedef struct {
	int32_t frame;
	uint8_t n;
	uint8_t ids[32];
} rb_se_rec;

typedef struct {
	int32_t frame;
	uint32_t crc;
} rb_crc;

typedef struct {
	uint8_t *buf;       /* compact image (see "Compact snapshots" above) */
	size_t len, cap;
} rb_snap;

struct rb_session {
	rb_config cfg;
	rb_game *g;         /* current state */
	rb_snap *snap;      /* ring of nsnap compact states */
	int32_t *snap_frame;
	int nsnap;

	rb_in local[IN_RING];
	rb_in remote[IN_RING];
	int32_t local_end;  /* local inputs known for frames < local_end */
	int32_t confirmed;  /* remote inputs known for frames < confirmed */
	int32_t rollback_to;
	int32_t next_crc;

	/* peer state from its packets */
	int32_t peer_ack;   /* peer knows our inputs for frames < peer_ack */
	int32_t remote_cur;
	uint32_t remote_cur_ms;
	float remote_adv;
	uint32_t peer_stamp, peer_stamp_rx_ms;
	bool have_peer_stamp, have_remote;
	uint32_t last_rx_ms;
	float rtt_ms;
	int32_t last_skip;

	rb_crc crc_local[CRC_RING], crc_remote[CRC_RING];

	rb_se_rec se_played[SE_RING];
	uint8_t se_out[SE_OUT_MAX];
	uint8_t se_out_cd[SE_OUT_MAX]; /* aos_world.se_cd of each queued id */
	int se_nout;
	int32_t se_newest;

	rb_stats st;
};

/* ------------------------------------------------------------------ */
/* game flow                                                          */

void rb_game_init(rb_game *g, const rb_match *m)
{
	memset(g, 0, sizeof *g); /* deterministic padding bytes too (hashed) */
	aos_match_params mp;
	aos_match_params_default(&mp);
	for (int i = 0; i < 2; i++) {
		mp.ids[i] = m->ids[i];
		mp.schemes[i] = m->schemes[i];
		mp.colors[i] = m->colors[i];
		mp.handicap[i] = m->handicap[i];
	}
	mp.roundSeconds = m->roundSeconds;
	mp.roundsToWin = m->roundsToWin;
	aos_match_init_ex(&g->w, &mp);
	g->lastMatchWinner = -1;
	g->matchEndFrame = -1;
}

/* Same match flow as the local versus loop in main.c, inside the snapshot: rounds
 * advance in aos_tick (FUN_00608a00, src/sim/flow.c); when the match-end wipe
 * is done (PC: result screen) the session starts the next match. */
void rb_game_step(rb_game *g, const uint16_t masks[2])
{
	aos_world *w = &g->w;
	aos_tick(w, masks);
	if (w->flow.matchEnd) {
		int win = aos_match_winner(w);
		if (win >= 0) {
			g->matchWins[win]++;
			g->lastMatchWinner = win;
		}
		g->matchEndFrame = g->frame;
		aos_match_restart(w);
	}
	g->frame++;
}

/* ------------------------------------------------------------------ */
/* compact layout                                                     */

#define POOL(arr, cnt) {offsetof(rb_game, w.obj.arr), sizeof(((aos_objects *)0)->arr[0]), offsetof(rb_game, w.obj.cnt), \
                      (int32_t)(sizeof(((aos_objects *)0)->arr) / sizeof(((aos_objects *)0)->arr[0]))}
static const struct {
	size_t off, esz, coff;
	int32_t cap;
} rb_pool[] = { /* in memory order; popup/hitreg (3 KB) stay in the fixed part */
	POOL(beam, nbeam), POOL(missile, nmissile), POOL(hitbox, nhitbox), POOL(laser, nlaser), POOL(enemy, nenemy),
	POOL(effect, neffect), POOL(whip, nwhip), POOL(particle, nparticle), POOL(spark, nspark),
};
#define RB_NPOOL ((int)(sizeof rb_pool / sizeof rb_pool[0]))
_Static_assert(offsetof(aos_objects, nspark) < offsetof(aos_objects, beam), "pool counts precede the pools");
_Static_assert(offsetof(aos_objects, beam) < offsetof(aos_objects, missile) &&
               offsetof(aos_objects, missile) < offsetof(aos_objects, hitbox) &&
               offsetof(aos_objects, hitbox) < offsetof(aos_objects, laser) &&
               offsetof(aos_objects, laser) < offsetof(aos_objects, enemy) &&
               offsetof(aos_objects, enemy) < offsetof(aos_objects, effect) &&
               offsetof(aos_objects, effect) < offsetof(aos_objects, whip) &&
               offsetof(aos_objects, whip) < offsetof(aos_objects, particle) &&
               offsetof(aos_objects, particle) < offsetof(aos_objects, spark) &&
               offsetof(aos_objects, spark) < offsetof(aos_objects, fx), "pool order");

static int32_t pool_count(const rb_game *g, int k)
{
	int32_t n;
	memcpy(&n, (const uint8_t *)g + rb_pool[k].coff, sizeof n);
	return n < 0 ? 0 : n > rb_pool[k].cap ? rb_pool[k].cap : n;
}

/* Calls fn(ctx, offset, length) for each run of rb_game bytes in the compact
 * image, in order: fixed part, live pool prefix, fixed part, ..., tail. */
static void for_each_run(const rb_game *g, void (*fn)(void *, size_t, size_t), void *ctx)
{
	size_t at = 0;
	for (int k = 0; k < RB_NPOOL; k++) {
		fn(ctx, at, rb_pool[k].off - at);
		fn(ctx, rb_pool[k].off, rb_pool[k].esz * (size_t)pool_count(g, k));
		at = rb_pool[k].off + rb_pool[k].esz * (size_t)rb_pool[k].cap;
	}
	fn(ctx, at, sizeof(rb_game) - at);
}

static size_t compact_size(const rb_game *g)
{
	size_t n = sizeof(rb_game);
	for (int k = 0; k < RB_NPOOL; k++)
		n -= rb_pool[k].esz * (size_t)(rb_pool[k].cap - pool_count(g, k));
	return n;
}

typedef struct {
	const rb_game *g;
	uint8_t *dst;
} save_ctx;

static void save_run(void *c, size_t off, size_t len)
{
	save_ctx *x = c;
	memcpy(x->dst, (const uint8_t *)x->g + off, len);
	x->dst += len;
}

/* Restore a compact image into g (whose dead slots are zero, objects.h). */
static void compact_restore(rb_game *g, const uint8_t *src)
{
	int32_t cur[RB_NPOOL];
	for (int k = 0; k < RB_NPOOL; k++)
		cur[k] = pool_count(g, k);
	size_t at = 0;
	for (int k = 0; k < RB_NPOOL; k++) {
		/* fixed part before this pool (the first one holds all the counts) */
		memcpy((uint8_t *)g + at, src, rb_pool[k].off - at);
		src += rb_pool[k].off - at;
		int32_t n = pool_count(g, k);
		size_t live = rb_pool[k].esz * (size_t)n;
		memcpy((uint8_t *)g + rb_pool[k].off, src, live);
		src += live;
		if (cur[k] > n) /* slots the current state used past the snapshot's count */
			memset((uint8_t *)g + rb_pool[k].off + live, 0, rb_pool[k].esz * (size_t)(cur[k] - n));
		at = rb_pool[k].off + rb_pool[k].esz * (size_t)rb_pool[k].cap;
	}
	memcpy((uint8_t *)g + at, src, sizeof(rb_game) - at);
}

/* ------------------------------------------------------------------ */
/* hashing                                                            */

/* rb_hash over a byte stream fed in pieces (same result as one call on the
 * concatenation). */
typedef struct {
	uint32_t h1, h2;
	uint8_t pend[4];
	int np;
} hstream;

static void hs_init(hstream *s, size_t total)
{
	s->h1 = 0x811c9dc5u;
	s->h2 = 0x9e3779b9u ^ (uint32_t)total;
	s->np = 0;
}

static void hs_word(hstream *s, uint32_t w)
{
	s->h1 = (s->h1 ^ w) * 0x01000193u;
	s->h2 = ((s->h2 << 5) | (s->h2 >> 27)) + w * 0x85ebca6bu;
}

static void hs_feed(hstream *s, const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t w;
	while (s->np && n) {
		s->pend[s->np++] = *b++;
		n--;
		if (s->np == 4) {
			memcpy(&w, s->pend, 4);
			hs_word(s, w);
			s->np = 0;
		}
	}
	for (; n >= 4; n -= 4, b += 4) {
		memcpy(&w, b, 4);
		hs_word(s, w);
	}
	while (n--)
		s->pend[s->np++] = *b++;
}

static uint32_t hs_end(hstream *s)
{
	for (int i = 0; i < s->np; i++)
		s->h1 = (s->h1 ^ s->pend[i]) * 0x01000193u;
	s->h1 ^= s->h2 * 0xc2b2ae35u;
	s->h1 ^= s->h1 >> 16;
	return s->h1;
}

uint32_t rb_hash(const void *p, size_t n)
{
	const uint8_t *b = p;
	uint32_t h1 = 0x811c9dc5u, h2 = 0x9e3779b9u ^ (uint32_t)n;
	size_t i = 0;
	for (; i + 4 <= n; i += 4) {
		uint32_t w;
		memcpy(&w, b + i, 4);
		h1 = (h1 ^ w) * 0x01000193u;
		h2 = ((h2 << 5) | (h2 >> 27)) + w * 0x85ebca6bu;
	}
	for (; i < n; i++)
		h1 = (h1 ^ b[i]) * 0x01000193u;
	h1 ^= h2 * 0xc2b2ae35u;
	h1 ^= h1 >> 16;
	return h1;
}

/* Draw code writes w.obj.fx.blurCaptured (aos_blur_draw marks the blur source
 * as captured). It never feeds back into aos_tick, but it differs between a
 * console that drew a frame and one that only re-simulated it, so it is left
 * out of checkpoint hashes (engine request: keep it outside aos_world). */
#define PRES_OFF \
	(offsetof(rb_game, w) + offsetof(aos_world, obj) + offsetof(aos_objects, fx) + offsetof(aos_fxstate, blurCaptured))
#define PRES_LEN sizeof(((aos_fxstate *)0)->blurCaptured)

_Static_assert(PRES_OFF > offsetof(rb_game, w.obj.spark) + sizeof(((aos_objects *)0)->spark),
               "presentation byte lies in the fixed tail of the compact image");

/* Hash of a compact image (the same bytes rb_game_hash covers). */
static uint32_t image_hash(const uint8_t *img, size_t len)
{
	size_t pres = len - (sizeof(rb_game) - PRES_OFF);
	hstream h;
	hs_init(&h, len - PRES_LEN);
	hs_feed(&h, img, pres);
	hs_feed(&h, img + pres + PRES_LEN, len - pres - PRES_LEN);
	return hs_end(&h);
}

typedef struct {
	const rb_game *g;
	hstream h;
} hash_ctx;

static void hash_run(void *c, size_t off, size_t len)
{
	hash_ctx *x = c;
	const uint8_t *b = (const uint8_t *)x->g;
	if (off <= PRES_OFF && PRES_OFF < off + len) {
		hs_feed(&x->h, b + off, PRES_OFF - off);
		hs_feed(&x->h, b + PRES_OFF + PRES_LEN, off + len - PRES_OFF - PRES_LEN);
	} else {
		hs_feed(&x->h, b + off, len);
	}
}

/* Checkpoint hash: the compact image of g (fixed part + live pool prefixes,
 * i.e. all simulation state; dead slots are zero by invariant) minus the
 * presentation byte. Equal to image_hash() of a snapshot of g. */
uint32_t rb_game_hash(const rb_game *g)
{
	hash_ctx x;
	x.g = g;
	hs_init(&x.h, compact_size(g) - PRES_LEN);
	for_each_run(g, hash_run, &x);
	return hs_end(&x.h);
}

void rb_clear_presentation(rb_game *g)
{
	memset((uint8_t *)g + PRES_OFF, 0, PRES_LEN);
}

uint32_t rb_sim_fingerprint(void)
{
	uint32_t v[8];
	v[0] = (uint32_t)sizeof(aos_world);
	v[1] = (uint32_t)sizeof(rb_game);
	v[2] = AOS_GAME_VERSION;
	v[3] = rb_hash(aos_tab.stats, sizeof aos_tab.stats);
	v[4] = rb_hash(aos_tab.cost, sizeof aos_tab.cost);
	v[5] = rb_hash(aos_tab.firstState, sizeof aos_tab.firstState) ^
	       rb_hash(aos_tab.initialState, sizeof aos_tab.initialState) * 3u;
	/* which handlers exist (pointer values differ between builds; presence must not) */
	uint8_t bits[AOS_NUM_STATES * 2 + AOS_NUM_CHARS * 7];
	size_t k = 0;
	for (int i = 0; i < AOS_NUM_STATES; i++) {
		bits[k++] = aos_tab.T2[i] != NULL;
		bits[k++] = aos_tab.T1[i] != NULL;
	}
	for (int i = 0; i < AOS_NUM_CHARS; i++) {
		bits[k++] = aos_tab.inputA[i] != NULL;
		bits[k++] = aos_tab.inputB[i] != NULL;
		bits[k++] = aos_tab.barrierHook[i] != NULL;
		for (int d = 0; d < 4; d++)
			bits[k++] = aos_tab.cpu[i][d] != NULL;
	}
	v[6] = rb_hash(bits, k);
	v[7] = RB_PROTO_VERSION;
	return rb_hash(v, sizeof v);
}

/* ------------------------------------------------------------------ */
/* little-endian helpers                                              */

static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v)
{
	b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t get32(const uint8_t *b)
{
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* ------------------------------------------------------------------ */
/* session                                                            */

void rb_config_default(rb_config *c, int local_player)
{
	memset(c, 0, sizeof *c);
	c->local_player = local_player;
	c->input_delay = 2;
	c->max_rollback = RB_MAX_ROLLBACK;
	c->crc_interval = 60;
	c->se_max_late = 3;
	c->timeout_ms = 3000;
	c->sync_min_gap = 20;
}

rb_session *rb_create(const rb_config *cfg, const rb_match *m, uint32_t now_ms)
{
	if (cfg->local_player < 0 || cfg->local_player > 1 || cfg->input_delay < 0 ||
	    cfg->input_delay > RB_MAX_DELAY || cfg->max_rollback < 1 || cfg->max_rollback > RB_MAX_ROLLBACK ||
	    cfg->crc_interval < 0)
		return NULL;
	rb_session *s = calloc(1, sizeof *s);
	if (!s)
		return NULL;
	s->cfg = *cfg;
	s->nsnap = cfg->max_rollback + 1;
	s->g = malloc(sizeof *s->g);
	s->snap = calloc((size_t)s->nsnap, sizeof *s->snap);
	s->snap_frame = malloc(sizeof *s->snap_frame * (size_t)s->nsnap);
	if (!s->g || !s->snap || !s->snap_frame) {
		rb_destroy(s);
		return NULL;
	}
	for (int i = 0; i < s->nsnap; i++) {
		s->snap_frame[i] = -1;
		/* Reserve room for heavy scenes up front (tests/perf_proxy: worst live
		 * image seen ~190 KB); a bigger state grows its slot on demand. */
		s->snap[i].buf = malloc(RB_SNAP_RESERVE);
		if (!s->snap[i].buf) {
			rb_destroy(s);
			return NULL;
		}
		s->snap[i].cap = RB_SNAP_RESERVE;
	}
	rb_game_init(s->g, m);

	for (int i = 0; i < IN_RING; i++)
		s->local[i].frame = s->remote[i].frame = -1;
	for (int f = 0; f < cfg->input_delay; f++) {
		s->local[f].frame = s->remote[f].frame = f;
		s->local[f].known = s->remote[f].known = 1;
	}
	s->local_end = s->confirmed = cfg->input_delay;
	s->rollback_to = NO_FRAME;
	s->next_crc = cfg->crc_interval;
	s->peer_ack = 0;
	s->remote_cur = 0;
	s->last_rx_ms = now_ms;
	s->rtt_ms = -1;
	s->last_skip = -1000;
	for (int i = 0; i < CRC_RING; i++)
		s->crc_local[i].frame = s->crc_remote[i].frame = -1;
	for (int i = 0; i < SE_RING; i++)
		s->se_played[i].frame = -1;
	s->st.desync_frame = -1;
	s->st.ping_ms = -1;
	return s;
}

void rb_destroy(rb_session *s)
{
	if (!s)
		return;
	free(s->g);
	if (s->snap)
		for (int i = 0; i < s->nsnap; i++)
			free(s->snap[i].buf);
	free(s->snap);
	free(s->snap_frame);
	free(s);
}

const rb_game *rb_state(const rb_session *s) { return s->g; }

size_t rb_memory_bytes(const rb_session *s)
{
	size_t n = sizeof *s + sizeof *s->g + (sizeof *s->snap + sizeof *s->snap_frame) * (size_t)s->nsnap;
	for (int i = 0; i < s->nsnap; i++)
		n += s->snap[i].cap;
	return n;
}

void rb_get_stats(const rb_session *s, rb_stats *out)
{
	*out = s->st;
	out->frame = s->g->frame;
	out->confirmed = s->confirmed;
	out->remote_frame = s->remote_cur;
	out->ping_ms = s->rtt_ms < 0 ? -1 : (int)(s->rtt_ms + 0.5f);
}

/* ---- checksums ---- */

static void crc_compare(rb_session *s, int32_t frame, uint32_t a, uint32_t b)
{
	(void)frame;
	s->st.crc_checked++;
	if (a != b) {
		s->st.desyncs++;
		if (s->st.desync_frame < 0)
			s->st.desync_frame = frame;
	}
}

static void crc_add(rb_session *s, rb_crc *mine, rb_crc *theirs, int32_t frame, uint32_t crc, bool local)
{
	int iv = s->cfg.crc_interval > 0 ? s->cfg.crc_interval : 1;
	int k = (int)((frame / iv) % CRC_RING);
	if (mine[k].frame == frame)
		return; /* duplicate (redundant packets) */
	mine[k].frame = frame;
	mine[k].crc = crc;
	if (theirs[k].frame == frame)
		crc_compare(s, frame, local ? crc : theirs[k].crc, local ? theirs[k].crc : crc);
}

static void crc_local(rb_session *s, int32_t frame, const rb_game *g)
{
	crc_add(s, s->crc_local, s->crc_remote, frame, rb_game_hash(g), true);
}

/* ---- inputs ---- */

static uint16_t local_input(const rb_session *s, int32_t f)
{
	const rb_in *e = &s->local[f & (IN_RING - 1)];
	return e->frame == f ? e->mask : 0;
}

/* Remote input for frame f: known value, or the prediction (recorded). */
static uint16_t remote_input(rb_session *s, int32_t f, bool *predicted)
{
	rb_in *e = &s->remote[f & (IN_RING - 1)];
	if (e->frame == f && e->known) {
		*predicted = false;
		return e->mask;
	}
	uint16_t pred = 0;
	if (s->confirmed > 0) {
		const rb_in *l = &s->remote[(s->confirmed - 1) & (IN_RING - 1)];
		pred = l->frame == s->confirmed - 1 ? l->mask : 0;
	}
	e->frame = f;
	e->mask = pred;
	e->known = 0;
	*predicted = true;
	return pred;
}

static void remote_set(rb_session *s, int32_t f, uint16_t mask)
{
	if (f < s->confirmed || f >= s->confirmed + IN_RING - 1)
		return; /* already known / absurdly far ahead */
	rb_in *e = &s->remote[f & (IN_RING - 1)];
	if (e->frame == f && e->known)
		return;
	if (e->frame == f && f < s->g->frame && e->mask != mask && f < s->rollback_to)
		s->rollback_to = f; /* simulated with a wrong prediction */
	e->frame = f;
	e->mask = mask;
	e->known = 1;
	while (1) {
		const rb_in *n = &s->remote[s->confirmed & (IN_RING - 1)];
		if (n->frame != s->confirmed || !n->known)
			break;
		s->confirmed++;
	}
}

/* ---- sounds ---- */

static void se_collect(rb_session *s, int32_t f)
{
	const aos_world *w = &s->g->w;
	rb_se_rec *r = &s->se_played[f & (SE_RING - 1)];
	if (r->frame != f) {
		r->frame = f;
		r->n = 0;
	}
	bool fresh = s->se_newest - f <= s->cfg.se_max_late;
	for (int i = 0; i < w->se_count && i < (int)sizeof w->se; i++) {
		uint8_t id = w->se[i];
		bool played = false;
		for (int j = 0; j < r->n; j++)
			if (r->ids[j] == id)
				played = true;
		if (played)
			continue;
		if (r->n < sizeof r->ids)
			r->ids[r->n++] = id;
		if (!fresh)
			continue;
		bool queued = false;
		for (int j = 0; j < s->se_nout; j++)
			if (s->se_out[j] == id)
				queued = true;
		if (!queued && s->se_nout < SE_OUT_MAX) {
			s->se_out_cd[s->se_nout] = w->se_cd[i];
			s->se_out[s->se_nout++] = id;
		}
	}
}

int rb_take_sounds_cd(rb_session *s, uint8_t *ids, uint8_t *cds, int cap)
{
	int n = s->se_nout < cap ? s->se_nout : cap;
	memcpy(ids, s->se_out, (size_t)n);
	if (cds)
		memcpy(cds, s->se_out_cd, (size_t)n);
	s->se_nout = 0;
	return n;
}

int rb_take_sounds(rb_session *s, uint8_t *ids, int cap) { return rb_take_sounds_cd(s, ids, NULL, cap); }

/* ---- simulation ---- */

static rb_snap *snap_slot(rb_session *s, int32_t f, int32_t **tag)
{
	int k = (int)(f % s->nsnap);
	*tag = &s->snap_frame[k];
	return &s->snap[k];
}

static void save_snapshot(rb_session *s)
{
	int32_t *tag;
	rb_snap *d = snap_slot(s, s->g->frame, &tag);
	size_t need = compact_size(s->g);
	if (need > d->cap) {
		size_t cap = need + 64 * 1024 < sizeof(rb_game) ? need + 64 * 1024 : sizeof(rb_game);
		uint8_t *nb = realloc(d->buf, cap);
		if (!nb) { /* out of memory: this frame can't be rolled back to (shows as a desync) */
			*tag = -1;
			s->st.desyncs++;
			if (s->st.desync_frame < 0)
				s->st.desync_frame = s->g->frame;
			return;
		}
		d->buf = nb;
		d->cap = cap;
	}
	save_ctx x = {s->g, d->buf};
	for_each_run(s->g, save_run, &x);
	d->len = need;
	*tag = s->g->frame;
	s->st.snapshots++;
}

/* Simulate frame g->frame (first time or re-simulation). */
static void sim_frame(rb_session *s)
{
	int32_t f = s->g->frame;
	bool predicted;
	uint16_t masks[2];
	masks[s->cfg.local_player] = local_input(s, f);
	masks[1 - s->cfg.local_player] = remote_input(s, f, &predicted);
	bool checkpoint = s->cfg.crc_interval > 0 && f == s->next_crc;
	if (checkpoint && s->confirmed >= f) {
		crc_local(s, f, s->g); /* all inputs before f known: this state is final */
		s->next_crc += s->cfg.crc_interval;
		checkpoint = false;
	}
	if (predicted || checkpoint)
		save_snapshot(s);
	rb_game_step(s->g, masks);
	se_collect(s, f);
}

void rb_sync(rb_session *s)
{
	if (s->rollback_to != NO_FRAME) {
		int32_t to = s->rollback_to, target = s->g->frame;
		s->rollback_to = NO_FRAME;
		int32_t *tag;
		rb_snap *src = snap_slot(s, to, &tag);
		if (*tag == to) {
			compact_restore(s->g, src->buf);
			s->se_newest = target - 1;
			while (s->g->frame < target)
				sim_frame(s);
			int depth = target - to;
			s->st.rollbacks++;
			s->st.resim_frames += (uint32_t)depth;
			s->st.last_depth = depth;
			if (depth > s->st.max_depth)
				s->st.max_depth = depth;
		} else {
			/* Cannot happen while the window invariant holds; count it as a desync so it is visible. */
			s->st.desyncs++;
			if (s->st.desync_frame < 0)
				s->st.desync_frame = to;
		}
	}
	/* Checkpoints simulated with predictions whose inputs are now all known. */
	while (s->cfg.crc_interval > 0 && s->next_crc < s->g->frame && s->confirmed >= s->next_crc) {
		int32_t *tag;
		rb_snap *src = snap_slot(s, s->next_crc, &tag);
		if (*tag == s->next_crc)
			crc_add(s, s->crc_local, s->crc_remote, s->next_crc, image_hash(src->buf, src->len), true);
		s->next_crc += s->cfg.crc_interval;
	}
}

rb_status rb_advance(rb_session *s, uint16_t local_mask, uint32_t now_ms)
{
	if (now_ms - s->last_rx_ms > s->cfg.timeout_ms)
		return RB_DISCONNECTED;
	rb_sync(s);
	int32_t cur = s->g->frame;

	/* Time sync (GGPO style): compare our advantage with the peer's; the side
	 * that runs ahead holds a frame now and then so neither keeps rolling back. */
	if (s->have_remote) {
		float rtt = s->rtt_ms < 0 ? 0 : s->rtt_ms;
		float remote_now = (float)s->remote_cur + ((float)(now_ms - s->remote_cur_ms) + rtt * 0.5f) / FRAME_MS;
		float adv = (float)cur - remote_now;
		s->st.advantage = s->st.advantage * 0.9f + adv * 0.1f;
	}
	if (cur + 1 - s->confirmed > s->cfg.max_rollback) {
		s->st.wait_stalls++;
		return RB_WAIT_REMOTE;
	}
	if (s->have_remote && (s->st.advantage - s->remote_adv) * 0.5f >= 1.0f &&
	    cur - s->last_skip >= s->cfg.sync_min_gap) {
		s->last_skip = cur;
		s->st.sync_skips++;
		return RB_SYNC_SKIP;
	}

	int32_t lf = cur + s->cfg.input_delay;
	if (lf >= s->local_end) { /* always true; guards against API misuse */
		rb_in *e = &s->local[lf & (IN_RING - 1)];
		e->frame = lf;
		e->mask = local_mask;
		e->known = 1;
		s->local_end = lf + 1;
	}
	s->se_newest = cur;
	sim_frame(s);
	return RB_TICKED;
}

/* ---- wire format ----
 * 'I' input packet (little endian):
 *  0 u8 type  1 u8 proto  2 i32 cur  6 i32 ack  10 i16 adv*16  12 u32 stamp
 * 16 u32 echo  20 u16 echo_hold_ms  22 u8 flags(bit0 echo valid, bit1 crc valid)
 * 23 i32 crc_frame  27 u32 crc  31 i32 first  35 u8 count  36 u16 masks[count] */
#define PKT_HDR 36

size_t rb_build_packet(rb_session *s, void *buf, size_t cap, uint32_t now_ms)
{
	uint8_t *b = buf;
	int32_t first = s->peer_ack, end = s->local_end;
	if (end - first > MAX_INPUTS_PER_PKT)
		first = end - MAX_INPUTS_PER_PKT;
	if (first < 0)
		first = 0;
	int count = end - first;
	if (count < 0)
		count = 0;
	if (cap < (size_t)(PKT_HDR + 2 * count))
		return 0;
	b[0] = RB_PKT_INPUT;
	b[1] = RB_PROTO_VERSION;
	put32(b + 2, (uint32_t)s->g->frame);
	put32(b + 6, (uint32_t)s->confirmed);
	float a = s->st.advantage * 16.0f;
	a = a > 32767 ? 32767 : a < -32767 ? -32767 : a;
	put16(b + 10, (uint16_t)(int16_t)a);
	put32(b + 12, now_ms);
	put32(b + 16, s->peer_stamp);
	uint32_t hold = now_ms - s->peer_stamp_rx_ms;
	put16(b + 20, (uint16_t)(hold > 65535 ? 65535 : hold));
	/* newest local checksum */
	const rb_crc *best = NULL;
	for (int i = 0; i < CRC_RING; i++)
		if (s->crc_local[i].frame >= 0 && (!best || s->crc_local[i].frame > best->frame))
			best = &s->crc_local[i];
	b[22] = (uint8_t)((s->have_peer_stamp ? 1 : 0) | (best ? 2 : 0));
	put32(b + 23, best ? (uint32_t)best->frame : 0);
	put32(b + 27, best ? best->crc : 0);
	put32(b + 31, (uint32_t)first);
	b[35] = (uint8_t)count;
	for (int i = 0; i < count; i++)
		put16(b + PKT_HDR + 2 * i, local_input(s, first + i));
	s->st.pkts_sent++;
	return (size_t)(PKT_HDR + 2 * count);
}

bool rb_receive(rb_session *s, const void *pkt, size_t len, uint32_t now_ms)
{
	const uint8_t *b = pkt;
	if (len < PKT_HDR || b[0] != RB_PKT_INPUT)
		return false;
	if (b[1] != RB_PROTO_VERSION || len < (size_t)(PKT_HDR + 2 * b[35])) {
		s->st.pkts_bad++;
		return false;
	}
	s->st.pkts_recv++;
	s->last_rx_ms = now_ms;
	int32_t cur = (int32_t)get32(b + 2), ack = (int32_t)get32(b + 6);
	if (ack > s->peer_ack && ack <= s->local_end)
		s->peer_ack = ack;
	if (!s->have_remote || cur >= s->remote_cur) {
		s->remote_cur = cur;
		s->remote_cur_ms = now_ms;
		s->remote_adv = (float)(int16_t)get16(b + 10) / 16.0f;
		s->have_remote = true;
	}
	uint32_t stamp = get32(b + 12);
	if (!s->have_peer_stamp || (int32_t)(stamp - s->peer_stamp) > 0) {
		s->peer_stamp = stamp;
		s->peer_stamp_rx_ms = now_ms;
		s->have_peer_stamp = true;
	}
	if (b[22] & 1) {
		uint32_t rtt = now_ms - get32(b + 16) - get16(b + 20);
		if (rtt < 10000)
			s->rtt_ms = s->rtt_ms < 0 ? (float)rtt : s->rtt_ms * 0.9f + (float)rtt * 0.1f;
	}
	if (b[22] & 2) {
		int32_t cf = (int32_t)get32(b + 23);
		if (cf > 0)
			crc_add(s, s->crc_remote, s->crc_local, cf, get32(b + 27), false);
	}
	int32_t first = (int32_t)get32(b + 31);
	for (int i = 0; i < b[35]; i++)
		remote_set(s, first + i, get16(b + PKT_HDR + 2 * i));
	return true;
}

/* ------------------------------------------------------------------ */
/* handshake                                                          */

static void put_i32s(uint8_t *b, const int32_t *v, int n)
{
	for (int i = 0; i < n; i++)
		put32(b + 4 * i, (uint32_t)v[i]);
}
static void get_i32s(const uint8_t *b, int32_t *v, int n)
{
	for (int i = 0; i < n; i++)
		v[i] = (int32_t)get32(b + 4 * i);
}

/* HELLO: 0 'H' 1 proto 2 u16 gamever 4 u32 fp 8 i32 id 12 i32 color 16 i32 scheme */
size_t rb_encode_hello(const rb_hello *h, uint8_t *buf, size_t cap)
{
	if (cap < 20)
		return 0;
	buf[0] = RB_PKT_HELLO;
	buf[1] = h->proto;
	put16(buf + 2, h->game_version);
	put32(buf + 4, h->fingerprint);
	int32_t v[3] = {h->id, h->color, h->scheme};
	put_i32s(buf + 8, v, 3);
	return 20;
}

bool rb_decode_hello(const uint8_t *buf, size_t len, rb_hello *h)
{
	if (len < 20 || buf[0] != RB_PKT_HELLO)
		return false;
	h->proto = buf[1];
	h->game_version = get16(buf + 2);
	h->fingerprint = get32(buf + 4);
	int32_t v[3];
	get_i32s(buf + 8, v, 3);
	h->id = v[0];
	h->color = v[1];
	h->scheme = v[2];
	return true;
}

/* START: 0 'S' 1 proto 2 u16 gamever 4 u32 fp 8 i32[8] match 40 u8 delay 41 u8 maxrb 42 u32 nonce
 *        46 u8 bgm 47 u8 stage 48 u8 handicap P1 49 u8 handicap P2 (proto 4) */
size_t rb_encode_start(const rb_start *st, uint8_t *buf, size_t cap)
{
	if (cap < 50)
		return 0;
	buf[0] = RB_PKT_START;
	buf[1] = st->proto;
	put16(buf + 2, st->game_version);
	put32(buf + 4, st->fingerprint);
	const rb_match *m = &st->match;
	int32_t v[8] = {m->ids[0], m->ids[1], m->schemes[0], m->schemes[1],
	                m->colors[0], m->colors[1], m->roundSeconds, m->roundsToWin};
	put_i32s(buf + 8, v, 8);
	buf[40] = st->input_delay;
	buf[41] = st->max_rollback;
	put32(buf + 42, st->nonce);
	buf[46] = st->bgm;
	buf[47] = st->stage;
	buf[48] = (uint8_t)m->handicap[0];
	buf[49] = (uint8_t)m->handicap[1];
	return 50;
}

bool rb_decode_start(const uint8_t *buf, size_t len, rb_start *st)
{
	if (len < 50 || buf[0] != RB_PKT_START)
		return false;
	st->proto = buf[1];
	st->game_version = get16(buf + 2);
	st->fingerprint = get32(buf + 4);
	int32_t v[8];
	get_i32s(buf + 8, v, 8);
	rb_match *m = &st->match;
	m->ids[0] = v[0]; m->ids[1] = v[1];
	m->schemes[0] = v[2]; m->schemes[1] = v[3];
	m->colors[0] = v[4]; m->colors[1] = v[5];
	m->roundSeconds = v[6];
	m->roundsToWin = v[7];
	st->input_delay = buf[40];
	st->max_rollback = buf[41];
	st->nonce = get32(buf + 42);
	st->bgm = buf[46];
	st->stage = buf[47];
	m->handicap[0] = buf[48];
	m->handicap[1] = buf[49];
	return true;
}

size_t rb_encode_start_ack(uint32_t nonce, uint8_t *buf, size_t cap)
{
	if (cap < 6)
		return 0;
	buf[0] = RB_PKT_START_ACK;
	buf[1] = RB_PROTO_VERSION;
	put32(buf + 2, nonce);
	return 6;
}

bool rb_decode_start_ack(const uint8_t *buf, size_t len, uint32_t *nonce)
{
	if (len < 6 || buf[0] != RB_PKT_START_ACK || buf[1] != RB_PROTO_VERSION)
		return false;
	*nonce = get32(buf + 2);
	return true;
}

void rb_match_from_picks(rb_match *m, const rb_hello *host, const rb_hello *client,
                         int32_t roundSeconds, int32_t roundsToWin)
{
	memset(m, 0, sizeof *m);
	m->ids[0] = host->id;
	m->ids[1] = client->id;
	m->schemes[0] = host->scheme;
	m->schemes[1] = client->scheme;
	m->colors[0] = host->color;
	m->colors[1] = client->color;
	if (m->ids[0] == m->ids[1] && m->colors[0] == m->colors[1])
		m->colors[1] = m->colors[0] == 0 ? 1 : 0;
	m->roundSeconds = roundSeconds;
	m->roundsToWin = roundsToWin;
	m->handicap[0] = m->handicap[1] = AOS_HANDICAP_DEFAULT; /* every PC menu path sets 2 (modes.md §0.5) */
}
