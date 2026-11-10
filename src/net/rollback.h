/*
 * Rollback netplay session (platform independent, host-testable).
 *
 * The PC game plays online over Steam P2P with rollback (core.md §1.1, §8):
 * it keeps snapshots of the match state (incl. the game RNG), predicts the
 * remote player's input, and on a late input that differs from the
 * prediction restores the snapshot and re-simulates to the present. This
 * module does the same on top of aos_world, with its own wire format so any
 * transport (UDS on 3DS, a fake link in tests) can carry it.
 *
 * Per display frame the caller does:
 *     while (packet arrives) rb_receive(s, pkt, len, now_ms);
 *     st = rb_advance(s, local_mask, now_ms);    // rollback + at most one tick
 *     n  = rb_build_packet(s, buf, sizeof buf, now_ms); send(buf, n);
 *     k  = rb_take_sounds(s, ids, cap);          // play these SE ids
 *     draw(&rb_state(s)->w);
 *
 * Determinism contract: both peers run the same binary, call rb_create with
 * the same rb_match and the same input_delay; local_player is 0 on the host
 * (P1) and 1 on the client (P2). Everything that affects gameplay lives in
 * rb_game (aos_world + the round/match flow), which is snapshotted whole.
 */
#ifndef AOS_NET_ROLLBACK_H
#define AOS_NET_ROLLBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../sim/sim.h"

#define RB_PROTO_VERSION 3 /* 2: START carries the battle BGM id; 3: and the stage id */
#define RB_MAX_ROLLBACK 8     /* hard cap on predicted frames (snapshot ring = this + 1) */
#define RB_MAX_DELAY 8
#define RB_MAX_PACKET 160     /* largest packet rb_build_packet produces */

/* Packet type bytes (first byte of every datagram). */
enum { RB_PKT_INPUT = 'I', RB_PKT_HELLO = 'H', RB_PKT_START = 'S', RB_PKT_START_ACK = 'K', RB_PKT_QUIT = 'Q' };

/* Match settings: must be identical on both consoles. */
typedef struct {
	int32_t ids[2], schemes[2], colors[2];
	int32_t roundSeconds, roundsToWin;
} rb_match;

/* Everything that is rolled back: the world plus the round/match flow that the
 * single-player loop keeps in main.c locals. */
typedef struct {
	aos_world w;
	int32_t frame;        /* session frame (never reset; == number of steps) */
	int32_t matchWins[2]; /* matches won this session */
	int32_t lastMatchWinner; /* -1 none, else player index */
	int32_t matchEndFrame;   /* session frame of the last match end (UI banner), -1 none */
} rb_game;

void rb_game_init(rb_game *g, const rb_match *m);
void rb_game_step(rb_game *g, const uint16_t masks[2]);

typedef struct {
	int local_player;     /* 0 = P1 (host), 1 = P2 (client) */
	int input_delay;      /* frames, 0..RB_MAX_DELAY (default 2) */
	int max_rollback;     /* frames, 1..RB_MAX_ROLLBACK (default 8) */
	int crc_interval;     /* desync check every N frames, 0 = off (default 60) */
	int se_max_late;      /* re-simulated sounds later than this many frames are dropped (default 3) */
	uint32_t timeout_ms;  /* no packet for this long = disconnected (default 3000) */
	int sync_min_gap;     /* min frames between time-sync skips (default 20) */
} rb_config;

void rb_config_default(rb_config *c, int local_player);

typedef enum {
	RB_TICKED = 0,        /* simulated one new frame */
	RB_WAIT_REMOTE,       /* too far ahead of the remote's confirmed input: frame held */
	RB_SYNC_SKIP,         /* ahead of the peer's clock: frame held to let it catch up */
	RB_DISCONNECTED,
} rb_status;

typedef struct {
	int32_t frame;          /* next frame to simulate */
	int32_t confirmed;      /* frames whose remote input is known (all < this) */
	int32_t remote_frame;   /* peer's last reported frame */
	uint32_t rollbacks;     /* rollbacks performed */
	uint32_t resim_frames;  /* frames re-simulated in total */
	int last_depth, max_depth;
	uint32_t wait_stalls, sync_skips;
	int ping_ms;            /* smoothed round trip, -1 unknown */
	float advantage;        /* local frame advantage (smoothed) */
	uint32_t crc_checked;   /* checkpoints compared with the peer */
	uint32_t desyncs;       /* mismatching checkpoints */
	int32_t desync_frame;   /* first mismatching checkpoint, -1 none */
	uint32_t pkts_sent, pkts_recv, pkts_bad;
	uint32_t snapshots;     /* snapshots saved */
} rb_stats;

typedef struct rb_session rb_session;

/* Allocates the session (snapshot ring ≈ (max_rollback + 2) * sizeof(rb_game)).
 * Returns NULL on bad config or out of memory. now_ms starts the timeout clock. */
rb_session *rb_create(const rb_config *cfg, const rb_match *m, uint32_t now_ms);
void rb_destroy(rb_session *s);

/* Feed one received datagram (any type; non-input types are ignored and return false). */
bool rb_receive(rb_session *s, const void *pkt, size_t len, uint32_t now_ms);
/* Apply pending rollbacks / checkpoint work without advancing. */
void rb_sync(rb_session *s);
/* Roll back if needed, then simulate at most one new frame with local_mask
 * (which is scheduled for frame + input_delay). */
rb_status rb_advance(rb_session *s, uint16_t local_mask, uint32_t now_ms);
/* Build this frame's input packet (redundant: all inputs the peer hasn't acked). */
size_t rb_build_packet(rb_session *s, void *buf, size_t cap, uint32_t now_ms);
/* Sound ids to play now (each id once; never replays a sound already played
 * for the same frame after a rollback). Clears the queue. */
int rb_take_sounds(rb_session *s, uint8_t *ids, int cap);
/* Same, plus each id's retrigger lockout (aos_world.se_cd) for the mixer. */
int rb_take_sounds_cd(rb_session *s, uint8_t *ids, uint8_t *cds, int cap);

const rb_game *rb_state(const rb_session *s);
void rb_get_stats(const rb_session *s, rb_stats *out);
size_t rb_memory_bytes(const rb_session *s);

uint32_t rb_hash(const void *p, size_t n); /* checkpoint hash (word-wise, fast) */
/* Checkpoint hash of a game state, skipping presentation-only bytes that the
 * draw code writes (see rb_clear_presentation). */
uint32_t rb_game_hash(const rb_game *g);
/* Zero the presentation-only bytes inside the snapshot (currently
 * w.obj.fx.blurCaptured, set by aos_blur_draw). Only for comparing states. */
void rb_clear_presentation(rb_game *g);

/* ---- handshake messages (sent by the transport layer before the session) ---- */
typedef struct {
	uint8_t proto;          /* RB_PROTO_VERSION */
	uint16_t game_version;  /* AOS_GAME_VERSION */
	uint32_t fingerprint;   /* rb_sim_fingerprint(): same engine tables + struct sizes */
	int32_t id, color, scheme; /* the sender's own pick */
} rb_hello;

typedef struct {
	uint8_t proto;
	uint16_t game_version;
	uint32_t fingerprint;
	rb_match match;
	uint8_t input_delay;
	uint8_t max_rollback;
	uint32_t nonce;         /* host-chosen session id, echoed in START_ACK */
	uint8_t bgm;            /* battle BGM id chosen by the host (0 = none); like the
	                         * original's online mode, the host decides (docs/re/music.md) */
	uint8_t stage;          /* stage id chosen by the host, "Random" already resolved
	                         * (FUN_007aaf90); 0 = none. docs/re/stages.md §2.2 */
} rb_start;

/* Hash of everything both consoles must agree on for deterministic play:
 * struct sizes, game version, stats/cost tables, which handlers are registered. */
uint32_t rb_sim_fingerprint(void);

size_t rb_encode_hello(const rb_hello *h, uint8_t *buf, size_t cap);
bool rb_decode_hello(const uint8_t *buf, size_t len, rb_hello *h);
size_t rb_encode_start(const rb_start *st, uint8_t *buf, size_t cap);
bool rb_decode_start(const uint8_t *buf, size_t len, rb_start *st);
size_t rb_encode_start_ack(uint32_t nonce, uint8_t *buf, size_t cap);
bool rb_decode_start_ack(const uint8_t *buf, size_t len, uint32_t *nonce);

/* Host side: merge both picks into match settings (host = P1). Same character
 * and palette -> P2 gets the next palette, as in the local versus. */
void rb_match_from_picks(rb_match *m, const rb_hello *host, const rb_hello *client,
                         int32_t roundSeconds, int32_t roundsToWin);

#endif
