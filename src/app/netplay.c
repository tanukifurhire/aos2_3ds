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
 * UNTESTED ON HARDWARE (needs two consoles); the session logic underneath is
 * host-tested by tests/test_rollback.c.
 */
#include "netplay.h"

#include <stdio.h>
#include <string.h>

#include "../net/rollback.h"
#include "../net/uds_link.h"
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

static void blank_top(C3D_RenderTarget *top)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(top, C2D_Color32(8, 10, 30, 255));
	C2D_SceneBegin(top);
	C3D_FrameEnd(0);
	audio_se_frame();
	music_update(); /* lobby/handshake screens keep the select music running */
}

static void wait_key(C3D_RenderTarget *top)
{
	printf("\nPress A to return.\n");
	while (aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & (KEY_A | KEY_B | KEY_START))
			break;
		blank_top(top);
	}
}

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

static void status_line(const rb_session *s, int local, const char *conn)
{
	rb_stats st;
	rb_get_stats(s, &st);
	const rb_game *g = rb_state(s);
	printf("\x1b[6;0H\x1b[0J");
	printf("link: %-10s  you are P%d\n", conn, local + 1);
	printf("ping %3d ms   adv %+5.1f f\n", st.ping_ms, (double)st.advantage);
	printf("frame %ld  confirmed %ld\n", (long)st.frame, (long)st.confirmed);
	printf("rollbacks %lu (last %d, max %d)\n", (unsigned long)st.rollbacks, st.last_depth, st.max_depth);
	printf("resim %lu  waits %lu  skips %lu\n", (unsigned long)st.resim_frames, (unsigned long)st.wait_stalls,
	       (unsigned long)st.sync_skips);
	printf("pkts in %lu out %lu  send err %lu\n", (unsigned long)st.pkts_recv, (unsigned long)st.pkts_sent,
	       (unsigned long)uds_link_send_errors());
	printf("checksums %lu  ", (unsigned long)st.crc_checked);
	if (st.desyncs)
		printf("\x1b[31mDESYNC x%lu @%ld\x1b[0m\n", (unsigned long)st.desyncs, (long)st.desync_frame);
	else
		printf("\x1b[32min sync\x1b[0m\n");
	printf("\nmatches  P1 %ld - %ld P2\n", (long)g->matchWins[0], (long)g->matchWins[1]);
	if (g->lastMatchWinner >= 0)
		printf("last match: P%ld wins\n", (long)g->lastMatchWinner + 1);
}

/* The same stats as status_line, compacted into the bottom-screen panel. */
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

static void net_match(C3D_RenderTarget *top, PrintConsole *con, bool host, const rb_start *st)
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
		printf("\x1b[31mout of memory for rollback snapshots\x1b[0m\n");
		send_quit();
		wait_key(top);
		return;
	}
	const rb_game *g = rb_state(s);
	consoleClear();
	printf("\x1b[36m%s vs %s\x1b[0m  (local wireless)\n", render_char_name(st->match.ids[0]),
	       render_char_name(st->match.ids[1]));
	printf("delay %d f, rollback window %d f, %lu KB\n", cfg.input_delay, cfg.max_rollback,
	       (unsigned long)(rb_memory_bytes(s) / 1024));
	printf("START: leave\n");
	if (!render_load_match(&g->w))
		printf("\x1b[31mtexture load failed\x1b[0m\n");
	/* both consoles play the host's pick (START.bgm), see docs/re/music.md §5 */
	music_battle_start(st->bgm);
	printf("BGM: %.34s\n", music_name(st->bgm));
	/* both consoles draw the host's stage (START.stage, random already resolved) */
	stage3ds_begin(st->stage);
	audio_se_prepare(st->match.ids[0], st->match.ids[1], stage_current()); /* SE residency (audio.h) */
	int32_t match_mark = g->matchEndFrame, stage_frame = g->w.frame;
	panel_begin(con); /* bottom screen: match panel instead of the console */
	panel_line(0, C2D_Color32(110, 220, 255, 255), "%s vs %s (wireless)  delay %d f  START: leave",
	           render_char_name(st->match.ids[0]), render_char_name(st->match.ids[1]), cfg.input_delay);
	panel_status(s, cfg.local_player, "connected");

	aos_input in;
	aos_input_init(&in, NULL);
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
		uint16_t mask = to_mask(&o, kh, &cp);

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
				why = "opponent left the match";
			}
		}
		if (n < 0)
			why = uds_link_error();
		uds_link_state ls = uds_link_poll();
		if (ls == UDS_LINK_LOST)
			why = "connection lost";
		if (why)
			break;

		rb_status r = rb_advance(s, mask, now_ms());
		if (r == RB_DISCONNECTED) {
			why = "no data from opponent (timeout)";
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
	panel_end(con);
	stage3ds_end();
	render_unload_match();
	music_battle_exit();
	if (why) {
		status_line(s, cfg.local_player, "closed");
		printf("\n\x1b[33m%s\x1b[0m\n", why);
		rb_destroy(s);
		wait_key(top);
	} else {
		rb_destroy(s);
	}
}

/* ---- lobby ---- */

void netplay_run(C3D_RenderTarget *top, PrintConsole *con, bool host, int my_id, int delay, int stage)
{
	consoleSelect(con);
	consoleClear();
	printf("\x1b[36mLocal wireless - %s\x1b[0m\n", host ? "host (P1)" : "join (P2)");
	printf("You: %s   B: cancel\n\n", render_char_name(my_id));
	if (!uds_link_init()) {
		printf("\x1b[31m%s\x1b[0m\n", uds_link_error());
		wait_key(top);
		return;
	}
	const uint32_t fp = rb_sim_fingerprint();
	rb_hello me;
	memset(&me, 0, sizeof me);
	me.proto = RB_PROTO_VERSION;
	me.game_version = AOS_GAME_VERSION;
	me.fingerprint = fp;
	me.id = my_id;
	me.color = 0;
	me.scheme = 0;

	rb_start st;
	memset(&st, 0, sizeof st);
	bool have_start = false, started = false;
	const char *fail = NULL;
	if (host) {
		if (!uds_link_host(fp))
			fail = uds_link_error();
		else
			printf("Waiting for a player to join...\n");
		st.nonce = (uint32_t)svcGetSystemTick() ^ now_ms();
	} else {
		printf("Searching for a host...\n");
	}

	int t = 0;
	while (!fail && !started && aptMainLoop()) {
		hidScanInput();
		if (hidKeysDown() & KEY_B) {
			if (have_start || uds_link_poll() == UDS_LINK_CONNECTED)
				send_quit();
			break;
		}
		t++;
		uds_link_state ls = uds_link_poll();
		if (!host && ls != UDS_LINK_CONNECTED && t % 30 == 1) {
			bool mismatch = false;
			if (uds_link_join(fp, &mismatch))
				printf("Connected, handshaking...\n");
			else if (mismatch)
				printf("\x1b[33mFound a host with a different build\x1b[0m\n");
			ls = uds_link_poll();
		}
		if (ls == UDS_LINK_LOST) {
			fail = "connection lost";
			break;
		}

		int n;
		while ((n = uds_link_recv(pkt, sizeof pkt)) > 0) {
			if (host) {
				rb_hello h;
				uint32_t nonce;
				if (rb_decode_hello(pkt, (size_t)n, &h) && !have_start) {
					if (!compatible(h.proto, h.game_version, h.fingerprint)) {
						send_quit();
						fail = "the other console runs a different build";
						break;
					}
					st.proto = RB_PROTO_VERSION;
					st.game_version = AOS_GAME_VERSION;
					st.fingerprint = fp;
					rb_match_from_picks(&st.match, &me, &h, 180, 2);
					st.input_delay = (uint8_t)delay;
					st.max_rollback = RB_MAX_ROLLBACK;
					st.bgm = (uint8_t)music_resolve(); /* host's BGM select, "????" rolled here */
					/* host's stage select; "Random" rolled here like FUN_007aaf90 */
					st.stage = (uint8_t)(stage == STAGE_RANDOM ? stage_random() : stage);
					have_start = true;
					printf("Opponent: %s - starting\n", render_char_name(h.id));
				} else if (have_start && ((rb_decode_start_ack(pkt, (size_t)n, &nonce) && nonce == st.nonce) ||
				                          pkt[0] == RB_PKT_INPUT)) {
					started = true;
				}
			} else {
				if (rb_decode_start(pkt, (size_t)n, &st)) {
					if (!compatible(st.proto, st.game_version, st.fingerprint) || st.input_delay > RB_MAX_DELAY ||
					    st.match.ids[0] < 3 || st.match.ids[0] >= AOS_NUM_CHARS || st.match.ids[1] < 3 ||
					    st.match.ids[1] >= AOS_NUM_CHARS) {
						send_quit();
						fail = "the host runs a different build";
						break;
					}
					uint8_t ack[8];
					size_t k = rb_encode_start_ack(st.nonce, ack, sizeof ack);
					uds_link_send(ack, k);
					have_start = started = true;
				}
			}
			if (pkt[0] == RB_PKT_QUIT) {
				fail = host ? "the player left" : "the host refused or left";
				break;
			}
		}
		if (n < 0 && !fail)
			fail = uds_link_error();
		if (started || fail)
			break;

		/* resend our side of the handshake */
		if (ls == UDS_LINK_CONNECTED && t % 4 == 0) {
			size_t k = host ? (have_start ? rb_encode_start(&st, pkt, sizeof pkt) : 0)
			                : rb_encode_hello(&me, pkt, sizeof pkt);
			if (k)
				uds_link_send(pkt, k);
		}
		blank_top(top);
	}
	if (started)
		net_match(top, con, host, &st);
	else if (fail) {
		printf("\n\x1b[31m%s\x1b[0m\n", fail);
		wait_key(top);
	}
	uds_link_close();
}
