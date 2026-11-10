/*
 * Local-wireless versus: lobby/handshake over UDS, then a rollback match.
 *
 * Handshake (all messages resent every few frames until answered, see rollback.h):
 *   client -> host  HELLO {proto, game version, sim fingerprint, own char/palette/scheme}
 *   host  -> client START {same checks, final rb_match, input delay, nonce}
 *   client -> host  START_ACK {nonce}  (also re-sent for every repeated START)
 * The client starts its session on START, the host on START_ACK (or on the
 * client's first input packet if the ACK was lost). The start-time offset is
 * absorbed by the session's time sync.
 *
 * The lobby is non-blocking (netl_*, polled once per frame by the local-wireless
 * select scene netsel.c, which draws every state); the client's beacon scan runs
 * on a worker thread so the select keeps animating. Only the match itself
 * (netplay_match) owns the loop.
 *
 * UNTESTED ON HARDWARE (needs two consoles); the session logic underneath is
 * host-tested by tests/test_rollback.c.
 */
#include "netplay.h"

#include <stdio.h>
#include <string.h>

#include "../net/rollback.h"
#include "../net/uds_link.h"
#include "../input/hyper_touch.h"
#include "../platform/bgm.h"
#include "../platform/stage.h"
#include "../platform/panel.h"
#include "../platform/render.h"
#include "../sim/se_table.h"
#include "../sim/sim.h"
#include "audio.h"
#include "music.h"

static uint8_t pkt[0x600]; /* >= UDS_DATAFRAME_MAXSIZE */

static uint32_t now_ms(void) { return (uint32_t)osGetTime(); }

/* This frame's sounds, then the per-frame SE/BGM updates (main.c order). */
static void play_ids(const uint8_t *ids, const uint8_t *cds, int n)
{
	for (int i = 0; i < n; i++)
		audio_se(ids[i], cds[i]);
	audio_se_frame();
	music_update();
}

static void send_quit(void)
{
	uint8_t q[2] = {RB_PKT_QUIT, RB_PROTO_VERSION};
	for (int i = 0; i < 3; i++)
		uds_link_send(q, sizeof q);
}

static bool compatible(uint8_t proto, uint16_t gamever, uint32_t fp)
{
	return proto == RB_PROTO_VERSION && gamever == AOS_GAME_VERSION && fp == rb_sim_fingerprint();
}

/* ---- the match ---- */

#ifdef AOS_PROFILE
/* Session stats as bottom-panel debug lines (profile builds, shown while SELECT is held: panel.h). */
static void panel_status(const rb_session *s, int local, const char *conn)
{
	rb_stats st;
	rb_get_stats(s, &st);
	const rb_game *g = rb_state(s);
	const u32 grey = C2D_Color32(220, 220, 220, 255);
	panel_line(1, grey, "link %s  P%d  ping %d ms  adv %+.1f f  frame %ld", conn, local + 1, st.ping_ms,
	           (double)st.advantage, (long)st.frame);
	panel_line(2, grey, "rollbacks %lu (last %d, max %d)  resim %lu  waits %lu", (unsigned long)st.rollbacks,
	           st.last_depth, st.max_depth, (unsigned long)st.resim_frames, (unsigned long)st.wait_stalls);
	panel_line(3, grey, "pkts in %lu out %lu  err %lu  checksums %lu", (unsigned long)st.pkts_recv,
	           (unsigned long)st.pkts_sent, (unsigned long)uds_link_send_errors(), (unsigned long)st.crc_checked);
	if (st.desyncs)
		panel_line(4, C2D_Color32(255, 90, 90, 255), "DESYNC x%lu @%ld", (unsigned long)st.desyncs, (long)st.desync_frame);
	else
		panel_line(4, C2D_Color32(120, 255, 120, 255), "in sync");
	if (g->lastMatchWinner >= 0)
		panel_line(5, grey, "matches P1 %ld - %ld P2   last: P%ld wins", (long)g->matchWins[0], (long)g->matchWins[1],
		           (long)g->lastMatchWinner + 1);
	else
		panel_line(5, grey, "matches P1 %ld - %ld P2", (long)g->matchWins[0], (long)g->matchWins[1]);
}
#else
#define panel_status(s, local, conn) ((void)0)
#endif

/* Returns NULL when the local player left (START), else why the match ended (static text). */
static const char *net_match(C3D_RenderTarget *top, PrintConsole *con, bool host, const rb_start *st)
{
	rb_config cfg;
	rb_config_default(&cfg, host ? 0 : 1);
	cfg.input_delay = st->input_delay;
	rb_session *s = NULL;
	/* max_rollback is a local choice (only input_delay must match): shrink if memory is short */
	for (int mr = st->max_rollback; mr >= 2 && !s; mr -= 2) {
		cfg.max_rollback = mr;
		s = rb_create(&cfg, &st->match, now_ms());
	}
	if (!s) {
		send_quit();
		return "Out of memory for the rollback snapshots.";
	}
	const rb_game *g = rb_state(s);
	(void)render_load_match(&g->w); /* a failed atlas only loses sprites; the session still runs */
	/* both consoles play the host's pick (START.bgm), see docs/re/music.md §5 */
	music_battle_start(st->bgm);
	/* both consoles draw the host's stage (START.stage, random already resolved) */
	stage3ds_begin(st->stage);
	audio_se_prepare(st->match.ids[0], st->match.ids[1], stage_current()); /* SE residency (audio.h) */
	int32_t match_mark = g->matchEndFrame, stage_frame = g->w.frame;
	panel_begin(con); /* bottom screen: the local player's touch hyper buttons (panel.h) */
	panel_match_begin(&g->w, cfg.local_player);
#ifdef AOS_PROFILE
	panel_line(0, C2D_Color32(110, 220, 255, 255), "%s vs %s (wireless)  delay %d f  START: leave",
	           render_char_name(st->match.ids[0]), render_char_name(st->match.ids[1]), cfg.input_delay);
#endif
	panel_status(s, cfg.local_player, "connected");

	aos_input in;
	aos_input_init(&in, NULL);
	ht_state ht; /* touch hyper buttons -> the local mask, sent like any other input */
	ht_init(&ht);
	const char *why = NULL;
	int ui = 0;
	while (aptMainLoop()) {
		hidScanInput();
		u32 kd = hidKeysDown(), kh = hidKeysHeld();
		if (kd & KEY_START) {
			send_quit();
			break;
		}
		circlePosition cp;
		hidCircleRead(&cp);
		aos_input_out o = aos_input_update(&in, aos_input_from_3ds(kh));
		const aos_player *me = &g->w.p[cfg.local_player];
		uint16_t mask = ht_apply(&ht, me->id, me->scheme, panel_touch(&g->w), to_mask(&o, kh, &cp));

		int n;
		while ((n = uds_link_recv(pkt, sizeof pkt)) > 0) {
			if (pkt[0] == RB_PKT_INPUT) {
				rb_receive(s, pkt, (size_t)n, now_ms());
			} else if (pkt[0] == RB_PKT_START && !host) {
				rb_start again;
				uint8_t ack[8];
				size_t k;
				if (rb_decode_start(pkt, (size_t)n, &again) && (k = rb_encode_start_ack(again.nonce, ack, sizeof ack)))
					uds_link_send(ack, k); /* our earlier ACK was lost */
			} else if (pkt[0] == RB_PKT_QUIT) {
				why = "The other player left the match.";
			}
		}
		if (n < 0)
			why = uds_link_error();
		uds_link_state ls = uds_link_poll();
		if (ls == UDS_LINK_LOST)
			why = "The connection was lost.";
		if (why)
			break;

		rb_status r = rb_advance(s, mask, now_ms());
		if (r == RB_DISCONNECTED) {
			why = "No data from the other player (timeout).";
			break;
		}
		size_t len = rb_build_packet(s, pkt, sizeof pkt, now_ms());
		if (len)
			uds_link_send(pkt, len);
		uint8_t ids[64], cds[64];
		music_watch(&g->w);
		play_ids(ids, cds, rb_take_sounds_cd(s, ids, cds, (int)sizeof ids));
		if (g->matchEndFrame > match_mark) { /* a match ended (confirmed or predicted): restart the track */
			match_mark = g->matchEndFrame;
			music_battle_start(st->bgm);
		}

		if (g->w.frame != stage_frame) { /* stage update once per new displayed tick (cosmetic) */
			stage_frame = g->w.frame;
			stage3ds_tick(g->w.obj.zoomView, g->w.frame);
		}
		if (++ui >= 15) {
			ui = 0;
			panel_status(s, cfg.local_player, r == RB_WAIT_REMOTE ? "waiting" : "connected");
		}
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		render_world(top, &g->w);
		panel_draw(&g->w);
		C3D_FrameEnd(0);
	}
	panel_match_end();
	panel_end(con);
	stage3ds_end();
	render_unload_match();
	music_battle_exit();
	rb_destroy(s);
	return why;
}

/* ---- lobby (non-blocking, netplay.h netl_*) ---- */

static struct {
	netl_state st;
	bool host;
	rb_hello me;
	rb_start start;
	bool have_start;
	int delay, stage, bgm;
	int peer;          /* opponent's engine id once known, -1 before */
	int peer_color;
	int t;             /* frames since netl_begin */
	bool mismatch;     /* client: saw a host running another build */
	char err[128];
	/* client: beacon scan on a worker thread (uds_link_join blocks ~0.5-1 s) */
	Thread scan;
	volatile bool scan_busy, scan_ok, scan_mm;
#ifdef AOS_PROFILE
	int stub; /* sdmc:/aos2_netstub.txt: fake peer for UI tests (no UDS), 0 off, 1 ok, 2 wireless off, 3 drop */
	int stub_id, stub_color;
#endif
} L;

static void fail(const char *msg)
{
	snprintf(L.err, sizeof L.err, "%s", msg);
	L.st = NETL_FAILED;
}

static void scan_fn(void *arg)
{
	(void)arg;
	bool mm = false;
	L.scan_ok = uds_link_join(rb_sim_fingerprint(), &mm);
	L.scan_mm = mm;
	L.scan_busy = false;
}

static void scan_wait(void)
{
	if (!L.scan)
		return;
	threadJoin(L.scan, U64_MAX);
	threadFree(L.scan);
	L.scan = NULL;
}

static void scan_start(void)
{
	scan_wait();
	L.scan_busy = true;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	/* lower priority than the game loop, same core: the scan mostly waits on the wireless service */
	L.scan = threadCreate(scan_fn, NULL, 0x4000, prio + 1 > 0x3f ? 0x3f : prio + 1, -2, false);
	if (!L.scan)
		scan_fn(NULL); /* no thread: scan inline (the frame stalls once) */
}

#ifdef AOS_PROFILE
static void stub_load(void)
{
	L.stub = 0;
	FILE *f = fopen("sdmc:/aos2_netstub.txt", "r");
	if (!f)
		return;
	int mode = 1, id = 3, col = 0;
	if (fscanf(f, "%d %d %d", &mode, &id, &col) >= 1)
		L.stub = mode, L.stub_id = id, L.stub_color = col;
	fclose(f);
}
#endif

bool netl_begin(bool host, int id, int color, int scheme, int delay, int stage, int bgm)
{
	netl_cancel();
	memset(&L.me, 0, sizeof L.me);
	memset(&L.start, 0, sizeof L.start);
	L.host = host;
	L.have_start = false;
	L.delay = delay;
	L.stage = stage;
	L.bgm = bgm;
	L.peer = -1;
	L.peer_color = 0;
	L.t = 0;
	L.mismatch = false;
	L.err[0] = 0;
	L.me.proto = RB_PROTO_VERSION;
	L.me.game_version = AOS_GAME_VERSION;
	L.me.fingerprint = rb_sim_fingerprint();
	L.me.id = id;
	L.me.color = color;
	L.me.scheme = scheme;
	L.st = host ? NETL_HOSTING : NETL_SEARCHING;
#ifdef AOS_PROFILE
	stub_load();
	if (L.stub == 2) {
		fail("Wireless communication is off.");
		return false;
	}
	if (L.stub)
		return true;
#endif
	if (!uds_link_init()) {
		fail("Wireless communication is off (or not available).");
		return false;
	}
	if (host) {
		if (!uds_link_host(L.me.fingerprint)) {
			char e[96];
			snprintf(e, sizeof e, "Could not create the network: %s", uds_link_error());
			fail(e);
			return false;
		}
		L.start.nonce = (uint32_t)svcGetSystemTick() ^ now_ms();
	}
	return true;
}

/* Host: the client's HELLO -> START (settings of both consoles). */
static void host_hello(const rb_hello *h)
{
	if (!compatible(h->proto, h->game_version, h->fingerprint)) {
		send_quit();
		fail("The other console runs a different build.");
		return;
	}
	rb_start *st = &L.start;
	st->proto = RB_PROTO_VERSION;
	st->game_version = AOS_GAME_VERSION;
	st->fingerprint = L.me.fingerprint;
	rb_match_from_picks(&st->match, &L.me, h, 180, 2);
	st->input_delay = (uint8_t)L.delay;
	st->max_rollback = RB_MAX_ROLLBACK;
	st->bgm = (uint8_t)L.bgm;
	st->stage = (uint8_t)L.stage;
	L.have_start = true;
	L.peer = h->id;
	L.peer_color = st->match.colors[1];
}

static bool client_start(void)
{
	rb_start *st = &L.start;
	if (!compatible(st->proto, st->game_version, st->fingerprint) || st->input_delay > RB_MAX_DELAY ||
	    st->match.ids[0] < 3 || st->match.ids[0] >= AOS_NUM_CHARS || st->match.ids[1] < 3 ||
	    st->match.ids[1] >= AOS_NUM_CHARS) {
		send_quit();
		fail("The host runs a different build.");
		return false;
	}
	uint8_t ack[8];
	size_t k = rb_encode_start_ack(st->nonce, ack, sizeof ack);
	uds_link_send(ack, k);
	L.peer = st->match.ids[0];
	L.peer_color = st->match.colors[0];
	return true;
}

#ifdef AOS_PROFILE
static netl_state stub_poll(void)
{
	if (L.st == NETL_READY || L.st == NETL_FAILED)
		return L.st;
	if (L.t > 90 && L.st != NETL_CONNECTED)
		L.st = NETL_CONNECTED;
	if (L.t > 150) {
		rb_hello h = L.me;
		h.id = L.stub_id;
		h.color = L.stub_color;
		if (L.host) {
			host_hello(&h);
		} else {
			rb_hello me = L.me;
			rb_match_from_picks(&L.start.match, &h, &me, 180, 2);
			L.start.stage = (uint8_t)(L.stage > 1 ? L.stage : 15);
			L.start.bgm = (uint8_t)L.bgm;
			L.peer = h.id;
			L.peer_color = L.start.match.colors[0];
		}
		L.st = NETL_READY;
	}
	return L.st;
}
#endif

netl_state netl_poll(void)
{
	L.t++;
#ifdef AOS_PROFILE
	if (L.stub)
		return stub_poll();
#endif
	if (L.st == NETL_OFF || L.st == NETL_FAILED || L.st == NETL_READY)
		return L.st;
	if (!L.host && L.st == NETL_SEARCHING) {
		if (L.scan_busy)
			return L.st;
		if (L.scan) { /* a scan finished */
			scan_wait();
			if (L.scan_mm)
				L.mismatch = true;
			if (L.scan_ok)
				L.st = NETL_CONNECTED;
		}
		if (L.st == NETL_SEARCHING) {
			if (L.t >= 2) /* the first frames draw "searching" before the first scan */
				scan_start();
			return L.st;
		}
	}
	uds_link_state ls = uds_link_poll();
	if (ls == UDS_LINK_LOST) {
		fail(L.host ? "The other player left." : "The connection to the host was lost.");
		return L.st;
	}
	if (L.host && ls == UDS_LINK_CONNECTED && L.st == NETL_HOSTING)
		L.st = NETL_CONNECTED;
	if (ls != UDS_LINK_CONNECTED)
		return L.st;
	int n = 0;
	while (L.st != NETL_FAILED && L.st != NETL_READY && (n = uds_link_recv(pkt, sizeof pkt)) > 0) {
		if (pkt[0] == RB_PKT_QUIT) {
			fail(L.host ? "The other player left." : "The host refused or left.");
			break;
		}
		if (L.host) {
			rb_hello h;
			uint32_t nonce;
			if (!L.have_start && rb_decode_hello(pkt, (size_t)n, &h))
				host_hello(&h);
			else if (L.have_start && ((rb_decode_start_ack(pkt, (size_t)n, &nonce) && nonce == L.start.nonce) ||
			                          pkt[0] == RB_PKT_INPUT))
				L.st = NETL_READY;
		} else if (rb_decode_start(pkt, (size_t)n, &L.start)) {
			if (client_start())
				L.st = NETL_READY;
		}
	}
	if (n < 0 && L.st != NETL_FAILED && L.st != NETL_READY) {
		char e[110];
		snprintf(e, sizeof e, "Wireless error: %s", uds_link_error());
		fail(e);
	}
	/* resend our side of the handshake */
	if (L.st == NETL_CONNECTED && L.t % 4 == 0) {
		size_t k = L.host ? (L.have_start ? rb_encode_start(&L.start, pkt, sizeof pkt) : 0)
		                  : rb_encode_hello(&L.me, pkt, sizeof pkt);
		if (k)
			uds_link_send(pkt, k);
	}
	return L.st;
}

void netl_hold(void)
{
#ifdef AOS_PROFILE
	if (L.stub)
		return;
#endif
	if (L.st != NETL_READY)
		return;
	if (uds_link_poll() == UDS_LINK_LOST) {
		fail("The connection was lost.");
		return;
	}
	int n;
	while ((n = uds_link_recv(pkt, sizeof pkt)) > 0) {
		rb_start again;
		uint8_t ack[8];
		size_t k;
		if (pkt[0] == RB_PKT_QUIT) {
			fail("The other player left.");
			return;
		}
		if (!L.host && rb_decode_start(pkt, (size_t)n, &again) &&
		    (k = rb_encode_start_ack(again.nonce, ack, sizeof ack)))
			uds_link_send(ack, k); /* our ACK was lost: the host repeats START */
	}
}

void netl_cancel(void)
{
	scan_wait();
#ifdef AOS_PROFILE
	if (L.stub) {
		L.st = NETL_OFF;
		return;
	}
#endif
	if (L.st == NETL_CONNECTED || L.st == NETL_READY)
		send_quit();
	if (L.st != NETL_OFF)
		uds_link_close();
	L.st = NETL_OFF;
}

netl_state netl_get(void) { return L.st; }
bool netl_mismatch(void) { return L.mismatch; }
const char *netl_error(void) { return L.err; }
int netl_peer(void) { return L.peer; }
int netl_peer_color(void) { return L.peer_color; }
const rb_start *netl_start(void) { return &L.start; }

const char *netplay_match(C3D_RenderTarget *top, PrintConsole *con)
{
	static char why[128];
	const char *r;
#ifdef AOS_PROFILE
	if (L.stub) {
		L.st = NETL_OFF;
		if (L.stub == 3)
			return "The connection was lost. (test stub)";
		return NULL;
	}
#endif
	r = L.st == NETL_READY ? net_match(top, con, L.host, &L.start) : "The connection was lost.";
	if (r) {
		snprintf(why, sizeof why, "%s", r); /* uds_link_error() is cleared by the close */
		r = why;
	}
	uds_link_close();
	L.st = NETL_OFF;
	return r;
}
