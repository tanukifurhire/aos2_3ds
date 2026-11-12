/*
 * Two-console versus: lobby browser + lobby/handshake over Local Wireless (UDS) or LAN (UDP over
 * soc, same router), then a rollback match. Both transports implement net_link (src/net/link.h).
 *
 * Handshake (all messages resent every few frames until answered, see rollback.h):
 *   client -> host  HELLO {proto, game version, sim fingerprint, own char/palette/scheme}
 *   host  -> client START {same checks, final rb_match, input delay, nonce}
 *   client -> host  START_ACK {nonce}  (also re-sent for every repeated START)
 * The client starts its session on START, the host on START_ACK (or on the
 * client's first input packet if the ACK was lost). The start-time offset is
 * absorbed by the session's time sync.
 *
 * The lobby and the browser are non-blocking (netl_* / netb_*, polled once per frame by the
 * select scene netsel.c, which draws every state); UDS beacon scans (and the UDS connect) run
 * on a worker thread so the select keeps animating; LAN discovery is a non-blocking socket.
 * Only the match itself (netplay_match) owns the loop.
 *
 * UNTESTED ON HARDWARE (needs two consoles); the session logic underneath is
 * host-tested by tests/test_rollback.c.
 */
#include "netplay.h"

#include <stdio.h>
#include <string.h>

#include "../net/rollback.h"
#include "../net/lan_link.h"
#include "../net/link.h"
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

static const net_link *LK = &uds_link_ops; /* link of the current lobby / match */

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
		LK->send(q, sizeof q);
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
	           (unsigned long)st.pkts_sent, (unsigned long)LK->send_errors(), (unsigned long)st.crc_checked);
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
/* Profile builds, for two-emulator tests: sdmc:/aos2_netbot.txt "frames" makes the local player
 * press random buttons (12-frame blocks, like tests/test_rollback.c) and leave after `frames`
 * session frames; every session logs its stats to sdmc:/aos2_net.txt once a second. */
static struct {
	FILE *log;
	int frames; /* -1 off */
	uint32_t rng;
	uint16_t mask;
} BOT;

static void bot_begin(int local, const char *link)
{
	BOT.frames = -1;
	FILE *b = fopen("sdmc:/aos2_netbot.txt", "r");
	if (b) {
		if (fscanf(b, "%d", &BOT.frames) != 1)
			BOT.frames = 3600;
		fclose(b);
	}
	BOT.rng = (uint32_t)svcGetSystemTick() | 1u;
	BOT.log = fopen("sdmc:/aos2_net.txt", "a");
	if (BOT.log)
		fprintf(BOT.log, "session start: P%d over %s, bot %d frames\n", local + 1, link, BOT.frames);
}

static uint16_t bot_mask(uint16_t mask, int32_t frame)
{
	if (BOT.frames < 0)
		return mask;
	if (frame % 12 == 0) {
		BOT.rng = BOT.rng * 1664525u + 1013904223u;
		BOT.mask = (uint16_t)(BOT.rng >> 12 & 0x3ff);
		if ((BOT.rng >> 24 & 3) == 0)
			BOT.mask &= 0x0f;
	}
	return BOT.mask;
}

static void bot_log(const rb_session *s, bool force)
{
	rb_stats st;
	rb_get_stats(s, &st);
	static int32_t last = -1;
	if (!BOT.log || (!force && (st.frame % 60 || st.frame == last)))
		return;
	last = st.frame;
	const rb_game *g = rb_state(s);
	fprintf(BOT.log,
	        "frame %ld confirmed %ld ping %d adv %+.1f rollbacks %lu (max %d) resim %lu waits %lu skips %lu "
	        "pkts %lu/%lu bad %lu crc %lu desync %lu hp %ld/%ld wins %ld-%ld\n",
	        (long)st.frame, (long)st.confirmed, st.ping_ms, (double)st.advantage, (unsigned long)st.rollbacks,
	        st.max_depth, (unsigned long)st.resim_frames, (unsigned long)st.wait_stalls, (unsigned long)st.sync_skips,
	        (unsigned long)st.pkts_recv, (unsigned long)st.pkts_sent, (unsigned long)st.pkts_bad,
	        (unsigned long)st.crc_checked, (unsigned long)st.desyncs, (long)g->w.p[0].hp, (long)g->w.p[1].hp,
	        (long)g->matchWins[0], (long)g->matchWins[1]);
	fflush(BOT.log);
}

static bool bot_done(const rb_session *s)
{
	return BOT.frames >= 0 && rb_state(s)->frame >= BOT.frames;
}

static void bot_end(const char *why)
{
	if (BOT.log) {
		fprintf(BOT.log, "session end: %s\n", why ? why : "left (local)");
		fclose(BOT.log);
	}
	BOT.log = NULL;
}
#else
#define panel_status(s, local, conn) ((void)0)
#define bot_begin(local, link) ((void)0)
#define bot_mask(mask, frame) (mask)
#define bot_log(s, force) ((void)0)
#define bot_done(s) false
#define bot_end(why) ((void)0)
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
	panel_line(0, C2D_Color32(110, 220, 255, 255), "%s vs %s (%s)  delay %d f  START: leave",
	           render_char_name(st->match.ids[0]), render_char_name(st->match.ids[1]), LK->name, cfg.input_delay);
#endif
	panel_status(s, cfg.local_player, "connected");
	bot_begin(cfg.local_player, LK->name);
	if (host)
		LK->set_status(LOBBY_INMATCH, 2);

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
		mask = bot_mask(mask, g->frame);
		if (bot_done(s)) {
			bot_log(s, true);
			send_quit();
			break;
		}

		int n;
		while ((n = LK->recv(pkt, sizeof pkt)) > 0) {
			if (pkt[0] == RB_PKT_INPUT) {
				rb_receive(s, pkt, (size_t)n, now_ms());
			} else if (pkt[0] == RB_PKT_START && !host) {
				rb_start again;
				uint8_t ack[8];
				size_t k;
				if (rb_decode_start(pkt, (size_t)n, &again) && (k = rb_encode_start_ack(again.nonce, ack, sizeof ack)))
					LK->send(ack, k); /* our earlier ACK was lost */
			} else if (pkt[0] == RB_PKT_QUIT) {
				why = "The other player left the match.";
			}
		}
		if (n < 0)
			why = LK->error();
		if (LK->poll() == LINK_LOST)
			why = "The connection was lost.";
		if (why)
			break;

		rb_status r = rb_advance(s, mask, now_ms());
		if (r == RB_DISCONNECTED) {
			why = "No data from the other player (timeout).";
			break;
		}
		bot_log(s, false);
		size_t len = rb_build_packet(s, pkt, sizeof pkt, now_ms());
		if (len)
			LK->send(pkt, len);
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
	bot_end(why);
	panel_match_end();
	panel_end(con);
	stage3ds_end();
	render_unload_match();
	music_battle_exit();
	rb_destroy(s);
	return why;
}

/* ---- transports ---- */

static int transport = -1; /* lobby_kind that is initialised, -1 none */

static void transport_down(int kind)
{
	if (kind == LOBBY_UDS)
		uds_link_exit();
	else if (kind == LOBBY_LAN)
		lan_link_exit();
}

/* Bring up `kind` (shutting the other one down first). NULL = ok, else a message. */
static const char *transport_use(int kind)
{
	static char e[128];
	if (transport == kind)
		return NULL;
	if (transport >= 0)
		transport_down(transport);
	transport = -1;
	if (kind == LOBBY_UDS) {
		if (!uds_link_init()) {
			uds_link_exit();
			return "Wireless communication is off (or not available).";
		}
	} else {
		lan_init_result r = lan_link_init();
		if (r != LAN_OK) {
			snprintf(e, sizeof e, "%s",
			         r == LAN_ERR_NOWIFI ? "Not connected to a Wi-Fi network. Both consoles must be on the same router."
			                             : lan_link_error());
			lan_link_exit();
			return e;
		}
	}
	transport = kind;
	LK = kind == LOBBY_LAN ? &lan_link_ops : &uds_link_ops;
	return NULL;
}

static const char *my_name(void)
{
	static char name[LOBBY_NAME_MAX];
	if (name[0])
		return name;
	bool ok = false;
	if (R_SUCCEEDED(cfguInit())) {
		uint16_t u[0x1c / 2 + 1];
		memset(u, 0, sizeof u);
		if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(0x1c, 0x000A0000, u))) /* profile nickname, UTF-16 */
			ok = lobby_name_from_utf16(name, u, 10);
		u64 h = 0;
		if (!ok && R_SUCCEEDED(CFGU_GenHashConsoleUnique(0x41325, &h))) {
			lobby_name_from_id(name, (uint32_t)(h ^ h >> 32));
			ok = true;
		}
		cfguExit();
	}
	if (!ok)
		lobby_name_from_id(name, (uint32_t)svcGetSystemTick());
	return name;
}

const char *netl_my_name(void) { return my_name(); }

/* ---- UDS worker thread: beacon scans (and the connect) block ~0.5-1 s ---- */

enum { JOB_SCAN, JOB_QUICK, JOB_CONNECT };

static struct {
	Thread th;
	volatile bool busy;
	int job;
	lobby_entry target;
	int n;                 /* lobbies found by the scan, -1 error */
	lobby_entry found[LOBBY_LIST_MAX];
	bool ok, mm, full, gone;
	lobby_entry joined;
} W;

static void worker_fn(void *arg)
{
	(void)arg;
	W.ok = W.mm = W.full = false;
	W.gone = W.job == JOB_CONNECT;
	W.n = uds_link_scan(W.found, LOBBY_LIST_MAX, now_ms());
	for (int i = 0; W.job != JOB_SCAN && i < W.n && !W.ok; i++) {
		const lobby_entry *e = &W.found[i];
		/* by console only: after a match the host reopens its lobby with a new session id */
		if (W.job == JOB_CONNECT && e->host != W.target.host)
			continue;
		W.gone = false;
		if (!lobby_compatible(e, RB_PROTO_VERSION, AOS_GAME_VERSION, rb_sim_fingerprint())) {
			W.mm = true;
			continue;
		}
		if (!lobby_joinable(e, RB_PROTO_VERSION, AOS_GAME_VERSION, rb_sim_fingerprint())) {
			W.full = true;
			continue;
		}
		if (uds_link_connect(e->host, e->session)) {
			W.ok = true;
			W.joined = *e;
		}
	}
	W.busy = false;
}

static void worker_wait(void)
{
	if (!W.th)
		return;
	threadJoin(W.th, U64_MAX);
	threadFree(W.th);
	W.th = NULL;
}

static void worker_start(int job, const lobby_entry *target)
{
	worker_wait();
	W.job = job;
	if (target)
		W.target = *target;
	W.busy = true;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	/* lower priority than the game loop, same core: the scan mostly waits on the wireless service */
	W.th = threadCreate(worker_fn, NULL, 0x4000, prio + 1 > 0x3f ? 0x3f : prio + 1, -2, false);
	if (!W.th)
		worker_fn(NULL); /* no thread: scan inline (the frame stalls once) */
}

/* ---- profile-build stub (sdmc:/aos2_netstub.txt): fake peer + fake lobbies for UI tests ----
 * line 1: "mode id color"  mode 0 off, 1 ok, 2 wireless off (both transports), 3 drop after the
 *         match, 4 LAN: not connected to Wi-Fi;  id/color = the fake opponent's pick
 * then any number of lobby lines (listed by the browser, joinable through the stub):
 *   "lobby <u|l> <chara> <stage> <status> <signal 0-3> <from frame> <until frame, 0 = always> <name>"
 *   status 0 open, 1 full, 2 in match, 9 open but another build */
#ifdef AOS_PROFILE
#define STUB_LOBBIES 12
static struct {
	int mode, id, color;
	int n;
	struct {
		lobby_entry e;
		int from, until;
	} lob[STUB_LOBBIES];
} S;

static void stub_load(void)
{
	memset(&S, 0, sizeof S);
	FILE *f = fopen("sdmc:/aos2_netstub.txt", "r");
	if (!f)
		return;
	char line[160];
	S.mode = 1, S.id = 3;
	if (fgets(line, sizeof line, f))
		sscanf(line, "%d %d %d", &S.mode, &S.id, &S.color);
	while (fgets(line, sizeof line, f) && S.n < STUB_LOBBIES) {
		char k = 0;
		int c, st, status, sig, from, until, used = 0;
		if (sscanf(line, "lobby %c %d %d %d %d %d %d %n", &k, &c, &st, &status, &sig, &from, &until, &used) < 7 ||
		    !used)
			continue;
		lobby_entry *e = &S.lob[S.n].e;
		memset(e, 0, sizeof *e);
		e->kind = k == 'l' ? LOBBY_LAN : LOBBY_UDS;
		e->proto = RB_PROTO_VERSION;
		e->game_version = AOS_GAME_VERSION;
		e->fingerprint = status == 9 ? 1 : rb_sim_fingerprint(); /* status 9: another build */
		e->session = 0x1000u + (uint32_t)S.n;
		e->host = 0x0a000000u + (uint32_t)S.n;
		e->chara = (uint8_t)c;
		e->stage = (uint8_t)st;
		e->status = (uint8_t)(status == 9 ? LOBBY_OPEN : status);
		e->players = status == LOBBY_OPEN || status == 9 ? 1 : 2;
		e->max_players = 2;
		e->port = LOBBY_LAN_GAME_PORT;
		e->signal = (int8_t)sig;
		snprintf(e->name, sizeof e->name, "%s", line + used);
		e->name[strcspn(e->name, "\r\n")] = 0;
		S.lob[S.n].from = from;
		S.lob[S.n].until = until;
		S.n++;
	}
	fclose(f);
}
#define STUB (S.mode != 0)
#else
#define STUB false
#endif

/* ---- browser ---- */

static struct {
	bool on;
	int kind;
	lobby_list list;
	char err[128];
	int t, scans;
} B;

bool netb_begin(int kind)
{
	netl_cancel();
	netb_end();
	lobby_list_clear(&B.list);
	B.kind = kind;
	B.t = B.scans = 0;
	B.err[0] = 0;
#ifdef AOS_PROFILE
	stub_load();
	if (STUB) {
		if (S.mode == 2 || (S.mode == 4 && kind == LOBBY_LAN)) {
			snprintf(B.err, sizeof B.err, "%s",
			         kind == LOBBY_UDS ? "Wireless communication is off (or not available)."
			                           : "Not connected to a Wi-Fi network. Both consoles must be on the same router.");
			return false;
		}
		B.on = true;
		return true;
	}
#endif
	const char *e = transport_use(kind);
	if (!e && kind == LOBBY_LAN && !lan_browse_begin())
		e = lan_link_error();
	if (e) {
		snprintf(B.err, sizeof B.err, "%s", e);
		return false;
	}
	B.on = true;
	return true;
}

void netb_poll(void)
{
	if (!B.on)
		return;
	B.t++;
	uint32_t t = now_ms();
#ifdef AOS_PROFILE
	if (STUB) {
		for (int i = 0; i < S.n; i++)
			if (S.lob[i].e.kind == B.kind && B.t >= S.lob[i].from && (!S.lob[i].until || B.t < S.lob[i].until)) {
				int k = lobby_list_seen(&B.list, &S.lob[i].e, t);
				if (k >= 0)
					B.list.e[k].signal = B.kind == LOBBY_UDS ? S.lob[i].e.signal : -1;
			}
		lobby_list_expire(&B.list, t, 1000); /* fake lobbies past "until" vanish after ~1 s */
		B.scans = B.t / 60;
		return;
	}
#endif
	if (B.kind == LOBBY_UDS) {
		if (W.busy)
			return;
		if (W.th) { /* a scan finished: merge it */
			worker_wait();
			if (W.n < 0) {
				snprintf(B.err, sizeof B.err, "%s", uds_link_error());
			} else {
				B.err[0] = 0;
				for (int i = 0; i < W.n; i++)
					lobby_list_seen(&B.list, &W.found[i], t);
				lobby_list_scan_done(&B.list, LOBBY_UDS, t);
				B.scans++;
			}
		}
		if (B.t >= 2) /* the first frames draw before the first scan */
			worker_start(JOB_SCAN, NULL);
	} else {
		lobby_entry got[LOBBY_LIST_MAX];
		int n = lan_browse_poll(got, LOBBY_LIST_MAX, t);
		for (int i = 0; i < n; i++)
			lobby_list_seen(&B.list, &got[i], t);
		lobby_list_expire(&B.list, t, LOBBY_EXPIRE_MS);
		if (B.t % 30 == 0)
			B.scans++;
	}
}

void netb_end(void)
{
	if (!B.on)
		return;
	worker_wait();
	if (!STUB && B.kind == LOBBY_LAN)
		lan_browse_end();
	B.on = false;
}

bool netb_active(void) { return B.on; }
int netb_kind(void) { return B.kind; }
const lobby_list *netb_list(void) { return &B.list; }
const char *netb_error(void) { return B.err; }
int netb_scans(void) { return B.scans; }

/* ---- lobby (non-blocking, netplay.h netl_*) ---- */

static struct {
	netl_state st;
	bool host;
	int kind;
	bool quick;        /* client without a target: join the first open lobby */
	lobby_entry target;
	int tries;
	rb_hello me;
	rb_start start;
	bool have_start;
	int delay, stage, bgm;
	int peer;          /* opponent's engine id once known, -1 before */
	int peer_color;
	int t;             /* frames since netl_begin */
	bool mismatch;     /* client: saw a host running another build */
	char err[128];
} L;

static void fail(const char *msg)
{
	snprintf(L.err, sizeof L.err, "%s", msg);
	L.st = NETL_FAILED;
}

bool netl_begin(bool host, int kind, const lobby_entry *target, int id, int color, int scheme, int delay, int stage,
                int bgm)
{
	netl_cancel();
	netb_end();
	memset(&L.me, 0, sizeof L.me);
	memset(&L.start, 0, sizeof L.start);
	L.host = host;
	L.kind = kind;
	L.quick = !host && !target;
	if (target)
		L.target = *target;
	else
		memset(&L.target, 0, sizeof L.target);
	L.tries = 0;
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
	L.start.nonce = (uint32_t)svcGetSystemTick() ^ now_ms();
#ifdef AOS_PROFILE
	stub_load();
	if (STUB) {
		if (S.mode == 2 || (S.mode == 4 && kind == LOBBY_LAN)) {
			fail(kind == LOBBY_UDS ? "Wireless communication is off." : "Not connected to a Wi-Fi network.");
			return false;
		}
		return true;
	}
#endif
	const char *e = transport_use(kind);
	if (e) {
		fail(e);
		return false;
	}
	if (host) {
		lobby_entry info;
		memset(&info, 0, sizeof info);
		info.proto = RB_PROTO_VERSION;
		info.game_version = AOS_GAME_VERSION;
		info.fingerprint = L.me.fingerprint;
		info.session = L.start.nonce;
		info.chara = (uint8_t)id;
		info.color = (uint8_t)color;
		info.stage = (uint8_t)stage;
		snprintf(info.name, sizeof info.name, "%s", my_name());
		bool ok = kind == LOBBY_LAN ? lan_link_host(&info) : uds_link_host(&info);
		if (!ok) {
			char m[128];
			snprintf(m, sizeof m, "Could not create the lobby: %s", LK->error());
			fail(m);
			return false;
		}
	} else if (kind == LOBBY_LAN) {
		if (L.quick) {
			if (!lan_browse_begin()) {
				fail(lan_link_error());
				return false;
			}
		} else if (!lan_link_connect(L.target.host, L.target.port)) {
			fail(lan_link_error());
			return false;
		}
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
	LK->send(ack, k);
	L.peer = st->match.ids[0];
	L.peer_color = st->match.colors[0];
	return true;
}

#ifdef AOS_PROFILE
static netl_state stub_poll(void)
{
	if (L.st == NETL_READY || L.st == NETL_FAILED)
		return L.st;
	if (!L.host && !L.quick && L.t > 60 && L.target.status != LOBBY_OPEN) {
		fail("The lobby is full.");
		return L.st;
	}
	if (!L.host && !L.quick && L.t > 60 && L.target.fingerprint != rb_sim_fingerprint()) {
		fail("The host runs a different build.");
		return L.st;
	}
	if (L.t > 90 && L.st != NETL_CONNECTED)
		L.st = NETL_CONNECTED;
	if (L.t > 150) {
		rb_hello h = L.me;
		h.id = L.quick || L.host ? S.id : L.target.chara;
		h.color = L.quick || L.host ? S.color : L.target.color;
		if (L.host) {
			host_hello(&h);
		} else {
			rb_hello me = L.me;
			rb_match_from_picks(&L.start.match, &h, &me, 180, 2);
			L.start.stage = (uint8_t)(!L.quick && L.target.stage > 1 ? L.target.stage : L.stage > 1 ? L.stage : 15);
			L.start.bgm = (uint8_t)L.bgm;
			L.peer = h.id;
			L.peer_color = L.start.match.colors[0];
		}
		L.st = NETL_READY;
	}
	return L.st;
}
#endif

/* Client still looking for its host (UDS scans on the worker / LAN quick-join announces). */
static void client_search(void)
{
	if (L.kind == LOBBY_LAN) { /* Quick Join: the first open lobby heard */
		lobby_entry got[LOBBY_LIST_MAX];
		int n = lan_browse_poll(got, LOBBY_LIST_MAX, now_ms());
		for (int i = 0; i < n; i++) {
			if (!lobby_compatible(&got[i], RB_PROTO_VERSION, AOS_GAME_VERSION, rb_sim_fingerprint())) {
				L.mismatch = true;
				continue;
			}
			if (!lobby_joinable(&got[i], RB_PROTO_VERSION, AOS_GAME_VERSION, rb_sim_fingerprint()))
				continue;
			lan_browse_end();
			L.target = got[i];
			if (!lan_link_connect(got[i].host, got[i].port))
				fail(lan_link_error());
			else
				L.st = NETL_CONNECTED;
			return;
		}
		return;
	}
	if (W.busy)
		return;
	if (W.th) { /* a scan (+ connect) finished */
		worker_wait();
		if (W.mm)
			L.mismatch = true;
		if (W.ok) {
			L.target = W.joined;
			L.st = NETL_CONNECTED;
			return;
		}
		if (!L.quick) { /* a chosen lobby: give it a few scans, then say why */
			if (W.full) {
				fail("The lobby is full.");
				return;
			}
			if (W.mm) {
				fail("The host runs a different build.");
				return;
			}
			if (++L.tries >= 4) {
				fail(W.gone ? "The lobby is gone (no beacon from the host)." : uds_link_error());
				return;
			}
		}
	}
	if (L.t >= 2) /* the first frames draw "searching" before the first scan */
		worker_start(L.quick ? JOB_QUICK : JOB_CONNECT, &L.target);
}

netl_state netl_poll(void)
{
	L.t++;
#ifdef AOS_PROFILE
	if (STUB)
		return stub_poll();
#endif
	if (L.st == NETL_OFF || L.st == NETL_FAILED || L.st == NETL_READY)
		return L.st;
	if (!L.host && L.st == NETL_SEARCHING) {
		if (L.kind == LOBBY_LAN && !L.quick)
			L.st = NETL_CONNECTED; /* targeted LAN join: the socket is aimed at the host already */
		else
			client_search();
		if (L.st != NETL_CONNECTED)
			return L.st;
	}
	link_state ls = LK->poll();
	if (ls == LINK_LOST) {
		const char *e = LK->error();
		fail(e && *e ? e : L.host ? "The other player left." : "The connection to the host was lost.");
		return L.st;
	}
	if (ls == LINK_OFF)
		return L.st;
	/* read while HOSTING too: on LAN the first HELLO is what attaches the peer */
	int n = 0;
	while (L.st != NETL_FAILED && L.st != NETL_READY && (n = LK->recv(pkt, sizeof pkt)) > 0) {
		if (pkt[0] == RB_PKT_QUIT) {
			fail(L.host ? "The other player left." : "The lobby is full, or the host refused or left.");
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
		snprintf(e, sizeof e, "Connection error: %s", LK->error());
		fail(e);
	}
	if (L.st == NETL_FAILED)
		return L.st;
	if (L.host && L.st == NETL_HOSTING && (LK->poll() == LINK_CONNECTED || L.have_start))
		L.st = NETL_CONNECTED;
	if (L.host)
		LK->set_status(L.st == NETL_HOSTING ? LOBBY_OPEN : LOBBY_FULL, L.st == NETL_HOSTING ? 1 : 2);
	/* resend our side of the handshake */
	if (L.st == NETL_CONNECTED && L.t % 4 == 0) {
		size_t k = L.host ? (L.have_start ? rb_encode_start(&L.start, pkt, sizeof pkt) : 0)
		                  : rb_encode_hello(&L.me, pkt, sizeof pkt);
		if (k)
			LK->send(pkt, k);
	}
	return L.st;
}

void netl_hold(void)
{
	if (STUB)
		return;
	if (L.st != NETL_READY)
		return;
	if (LK->poll() == LINK_LOST) {
		fail("The connection was lost.");
		return;
	}
	int n;
	while ((n = LK->recv(pkt, sizeof pkt)) > 0) {
		rb_start again;
		uint8_t ack[8];
		size_t k;
		if (pkt[0] == RB_PKT_QUIT) {
			fail("The other player left.");
			return;
		}
		if (!L.host && rb_decode_start(pkt, (size_t)n, &again) &&
		    (k = rb_encode_start_ack(again.nonce, ack, sizeof ack)))
			LK->send(ack, k); /* our ACK was lost: the host repeats START */
	}
}

void netl_cancel(void)
{
	worker_wait();
	if (STUB) {
		L.st = NETL_OFF;
		return;
	}
	if (L.st == NETL_CONNECTED || L.st == NETL_READY)
		send_quit();
	if (L.st != NETL_OFF) {
		if (transport == LOBBY_LAN && !B.on)
			lan_browse_end(); /* LAN quick join listened for announces */
		LK->close();
	}
	L.st = NETL_OFF;
}

const char *net_transport_check(int kind)
{
#ifdef AOS_PROFILE
	stub_load();
	if (STUB)
		return S.mode == 2 ? "Wireless communication is off (or not available)."
		       : S.mode == 4 && kind == LOBBY_LAN
		           ? "Not connected to a Wi-Fi network. Both consoles must be on the same router."
		           : NULL;
#endif
	return transport_use(kind);
}

void net_shutdown(void)
{
	netb_end();
	netl_cancel();
	if (transport >= 0)
		transport_down(transport);
	transport = -1;
}

netl_state netl_get(void) { return L.st; }
int netl_kind(void) { return L.kind; }
bool netl_mismatch(void) { return L.mismatch; }
const char *netl_error(void) { return L.err; }
int netl_peer(void) { return L.peer; }
int netl_peer_color(void) { return L.peer_color; }
const char *netl_peer_name(void) { return L.host ? "" : L.target.name; }
const rb_start *netl_start(void) { return &L.start; }

const char *netplay_match(C3D_RenderTarget *top, PrintConsole *con)
{
	static char why[128];
	const char *r;
#ifdef AOS_PROFILE
	if (STUB) {
		L.st = NETL_OFF;
		if (S.mode == 3)
			return "The connection was lost. (test stub)";
		return NULL;
	}
#endif
	r = L.st == NETL_READY ? net_match(top, con, L.host, &L.start) : "The connection was lost.";
	if (r) {
		snprintf(why, sizeof why, "%s", r); /* the link's error text is cleared by the close */
		r = why;
	}
	LK->close();
	L.st = NETL_OFF;
	return r;
}
