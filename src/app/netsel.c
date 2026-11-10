/* Local-wireless versus (2 consoles): connection mode, character select, stage select (host),
 * lobby and VS screens — SCENE_NET_SELECT — plus the blocking match runner. Port-only (the PC
 * online mode uses Steam lobbies); built from the VS Select's pieces (select_draw.c "selx_",
 * drawing from g_vs) so it looks like the offline select:
 *
 *   MODE     Create Lobby (host, P1) / Quick Join (client, P2)          arcade command-mode bands
 *   CHARS    character grid on the bottom screen, wheel + portrait on top (both screens)
 *   OPTIONS  colour / hyper control / accept, standing art on top
 *   STAGE    host only: stage cards + BGM, like VS Select
 *   LINK     hosting / searching / handshake (netplay.h netl_*); own pick shown, the opponent "?"
 *   WHITE/VS both picks + the host's stage: white fade, stage swap, VS jingle (VS Select 300/0x136)
 *   GO       fade out -> SCENE_NETPLAY_RUN (netplay_match), back here afterwards
 *   ERROR    disconnects / failures as a styled message
 * B steps back from every state (and leaves the network). Nothing here touches the simulation:
 * the match settings travel in the unchanged START packet (rollback.h). */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../platform/stage.h"
#include "../sim/rng.h"
#include "audio.h"
#include "menu.h"
#include "modes_flow.h"
#include "music.h"
#include "netplay.h"
#include "scene.h"
#include "select.h"
#include "selgfx.h"
#include "text.h"
#include "ui.h"

#define W 400.0f
#define H 240.0f
#define BW 320.0f
#define WHITE 0xffffffffu
#define GREY 0xffa4a4a4u
#define SE(id) audio_se((id), AUDIO_SE_CD_DEFAULT)

/* Port-internal scene id: the blocking match (netplay_match). */
#define SCENE_NETPLAY_RUN 23

enum { NS_MODE, NS_CHARS, NS_OPTIONS, NS_STAGE, NS_LINK, NS_WHITE, NS_VS, NS_GO, NS_ERROR };
enum { OPT_COLOR, OPT_INPUT, OPT_ACCEPT, OPT_COUNT };

static struct {
	int st, t;
	int mode;          /* MODE cursor: 0 host, 1 join */
	bool host;
	int me, op;        /* g_vs slots: host = 0 (P1), client = 1 */
	int cur;           /* pool cursor of the local pick */
	int color, cmd;    /* colour option 0 Auto/1 P1/2 P2/3 SP, hyper control */
	int row;           /* OPTIONS row */
	int pick;          /* resolved local character (Random rolled) */
	int stage;         /* host: resolved stage; both: START.stage once READY */
	float mode_a, link_a, err_a;
	char err[160];
	int err_back;      /* state B/A returns to from ERROR */
	bool err_conn;     /* the consoles had connected (disconnect) vs. never connected (failure) */
	int leave;
	bool ever;
} N;

static const char *ret_why; /* set by the match runner */
static bool ret_set;

static unsigned a8(float a) { return (unsigned)(int)(255.0f * (a < 0 ? 0 : a > 1 ? 1 : a)) << 24; }
static u32 c2d(unsigned argb) { return ui_c2d(argb); }

static void approach(float *v, bool up, float rate)
{
	*v += up ? rate : -rate;
	if (*v > 1.0f)
		*v = 1.0f;
	if (*v < 0.0f)
		*v = 0.0f;
}

static void to_zero(float *v, float rate)
{
	if (*v > rate)
		*v -= rate;
	else if (*v < -rate)
		*v += rate;
	else
		*v = 0;
}

static void ann(const char *key)
{
	int id = announce_id_of(key);
	if (id >= 0)
		announce_show(id);
}

static int pick_random(const int *list, int n) /* like select.c (FUN_007aaf90): without 1, cosmetic RNG */
{
	int tmp[SEL_POOL_MAX], m = 0;
	for (int i = 0; i < n && m < SEL_POOL_MAX; i++)
		if (list[i] != SEL_RANDOM)
			tmp[m++] = list[i];
	if (!m)
		return 10;
	return tmp[(uint32_t)aos_rand_i(modes_cosmetic(), 0, m * 10) % (uint32_t)m];
}

static const char *char_name(int id)
{
	const char *k = id >= 3 && id <= 18 ? text_id_key(0x1f8 + id - 3) : NULL;
	return k ? tr(k) : "????";
}

static void set_role(bool host)
{
	N.host = host;
	N.me = host ? 0 : 1;
	N.op = 1 - N.me;
}

static void go(int st)
{
	N.st = st;
	N.t = 0;
	N.leave = 0;
}

static void ann_row(void)
{
	static const char *const k[OPT_COUNT] = {"ANNOUNCE_CHARACTERSELECT_COLOR", "ANNOUNCE_CHARACTERSELECT_INPUT",
	                                         "ANNOUNCE_CHARACTERSELECT_ACCEPT"};
	ann(k[N.row]);
}

static void show_error(const char *msg, int back)
{
	snprintf(N.err, sizeof N.err, "%s", msg && *msg ? msg : "The connection was lost.");
	N.err_back = back;
	netl_state ls = netl_get();
	N.err_conn = back == NS_CHARS || ls == NETL_READY || ls == NETL_CONNECTED || N.st >= NS_WHITE;
	netl_cancel();
	g_vs.stage_swapped = false;
	g_vs.ca4 = 0;
	music_force(MUSIC_SELECT, true);
	go(NS_ERROR);
	if (N.err_conn)
		ann("ANNOUNCE_NET_DISCONNECT");
	else
		announce_show_text(N.err);
	SE(3);
}

/* ================================================================ init / exit */

static void netsel_init(void)
{
	vssel_state *v = &g_vs;
	selx_textures();
	const aos_save *sv = modes_save();
	v->n = sel_pool_vs(sv, v->pool);
	v->nstages = sel_stage_list(sv, v->stages);
	bool from_menu = scene_prev() != SCENE_NETPLAY_RUN || !N.ever;
	if (from_menu) {
		int i = sel_index_of(v->pool, v->n, 10), k = sel_index_of(v->stages, v->nstages, 15);
		N.cur = i < 0 ? 0 : i;
		N.color = N.cmd = 0;
		N.mode = 0;
		v->stage_cur = k < 0 ? 0 : k;
		music_sel_set(0); /* "????" */
	}
	N.ever = true;
	if (N.cur >= v->n)
		N.cur = 0;
	if (v->stage_cur >= v->nstages)
		v->stage_cur = 0;
	v->sub = VS_CHARS;
	v->frame = v->t_sub = v->leave = 0;
	v->stage_swapped = false;
	v->ca0 = v->ca4 = v->ca8 = v->d7c = v->d80 = v->cbc = 0;
	for (int p = 0; p < 2; p++) {
		v->cc0[p] = v->cc8[p] = v->cd0[p] = v->cd8[p] = v->ce0[p] = v->d74[p] = 0;
		v->slot[p] = SLOT_CPU;
		v->color[p] = v->command[p] = 0;
		v->handicap[p] = 2;
		v->en_hcp[p] = v->en_col[p] = v->en_cmd[p] = true;
		v->cursor[p] = N.cur;
		v->chara[p] = SEL_RANDOM;
	}
	N.mode_a = N.link_a = N.err_a = 0;
	N.leave = 0;
	N.row = OPT_ACCEPT;
	if (!from_menu) { /* back from a match: same role, pick again (or read why it ended) */
		set_role(N.host);
		if (ret_set && ret_why)
			show_error(ret_why, NS_CHARS);
		else {
			go(NS_CHARS);
			ann("ANNOUNCE_CHARACTERSELECT_CHARACTER");
		}
	} else {
		set_role(N.mode == 0);
		go(NS_MODE);
		ann("ANNOUNCE_NET_JOINSIDE");
	}
	ret_set = false;
	ret_why = NULL;
	scene_fade(5);
	music_force(MUSIC_SELECT, true);
}

static void netsel_exit(void)
{
	if (N.st != NS_GO)
		netl_cancel();
	selx_free();
	announce_hide();
}

/* ================================================================ update */

static void enter_link(void)
{
	vssel_state *v = &g_vs;
	int c = v->pool[N.cur];
	N.pick = c == SEL_RANDOM ? pick_random(v->pool, v->n) : c;
	int stage = 0, bgm = 0;
	if (N.host) {
		int id = v->stages[v->stage_cur];
		stage = id == 1 ? pick_random(v->stages, v->nstages) : id;
		bgm = music_resolve();
	}
	N.stage = stage;
	go(NS_LINK);
	if (!netl_begin(N.host, N.pick, sel_color_resolve(N.pick, N.color), N.cmd, g_config.net_delay, stage, bgm)) {
		show_error(netl_error(), N.host ? NS_STAGE : NS_OPTIONS);
		return;
	}
	ann(N.host ? "ANNOUNCE_NET_ACCEPT" : "ANNOUNCE_NET_CONNECT");
}

static void leave_link(void) /* B in LINK / VS: tell the other console, step back */
{
	netl_cancel();
	g_vs.stage_swapped = false;
	g_vs.ca4 = 0;
	music_force(MUSIC_SELECT, true);
	go(N.host ? NS_STAGE : NS_OPTIONS);
	ann(N.host ? "ANNOUNCE_CHARACTERSELECT_STAGE" : "ANNOUNCE_CHARACTERSELECT_CHARACTER");
	SE(1);
}

static void touch(void)
{
	if (!min_pressed_nr(MI_TOUCH))
		return;
	int tx = g_min.touch_x, ty = g_min.touch_y;
	vssel_state *v = &g_vs;
	if (N.st == NS_MODE) {
		int k = ty >= 26 && ty < 110 ? 0 : ty >= 110 && ty < 194 ? 1 : -1;
		if (k < 0)
			return;
		if (k == N.mode)
			g_min.st[MI_OK] = MIN_PRESSED;
		else {
			N.mode = k;
			SE(2);
		}
	} else if (N.st == NS_CHARS) {
		int col = (tx - 4) / 40, row = (ty - 26) / 40;
		if (tx < 4 || ty < 26 || col > 4 || row > 3)
			return;
		int i = row * 5 + col;
		if (i >= v->n)
			return;
		if (i == N.cur)
			g_min.st[MI_OK] = MIN_PRESSED;
		else {
			v->d74[N.me] = i > N.cur ? 1.0f : -1.0f;
			v->cc0[N.me] = 0;
			N.cur = i;
			SE(2);
		}
	} else if (N.st == NS_OPTIONS) {
		int r = ty >= 96 && ty < 186 ? (ty - 96) / 30 : -1;
		if (r < 0)
			return;
		if (r == N.row && r == OPT_ACCEPT)
			g_min.st[MI_OK] = MIN_PRESSED;
		else if (r == N.row)
			g_min.st[tx < 160 ? MI_LEFT : MI_RIGHT] = MIN_PRESSED;
		else {
			N.row = r;
			SE(2);
			ann_row();
		}
	} else if (N.st == NS_STAGE) {
		if (ty >= 28 && ty < 28 + 5 * 26) {
			int k = (ty - 28) / 26 - 2;
			if (k == 0)
				g_min.st[MI_OK] = MIN_PRESSED;
			else {
				v->stage_cur = ((v->stage_cur + k) % v->nstages + v->nstages) % v->nstages;
				v->d7c = k > 0 ? 1.0f : -1.0f;
				SE(2);
			}
		}
	} else if (N.st == NS_ERROR) {
		g_min.st[MI_OK] = MIN_PRESSED;
	}
}

static void chars_update(void)
{
	vssel_state *v = &g_vs;
	int n = v->n, d = 0;
	/* grid of 5 columns (bottom screen): left/right step, up/down jump a row */
	if (min_pressed(MI_LEFT))
		d = -1;
	if (min_pressed(MI_RIGHT))
		d = 1;
	if (min_pressed(MI_UP))
		d = -5;
	if (min_pressed(MI_DOWN))
		d = 5;
	if (d) {
		int c = N.cur + d;
		if (d == 5 || d == -5) {
			int rows = (n + 4) / 5, col = N.cur % 5, row = (N.cur / 5 + (d > 0 ? 1 : -1) + rows) % rows;
			c = row * 5 + col;
			if (c >= n)
				c = d > 0 ? col : n - 1;
		} else {
			c = (c + n) % n;
		}
		N.cur = c;
		v->d74[N.me] = d > 0 ? 1.0f : -1.0f;
		v->cc0[N.me] = 0;
		SE(2);
	}
	if (min_confirm()) {
		go(NS_OPTIONS);
		N.row = OPT_ACCEPT;
		v->ce0[N.me] = 1.0f;
		SE(0);
		ann("ANNOUNCE_CHARACTERSELECT_ACCEPT");
	} else if (min_cancel()) {
		go(NS_MODE);
		SE(1);
		ann("ANNOUNCE_NET_JOINSIDE");
	}
}

static void options_update(void)
{
	vssel_state *v = &g_vs;
	int c = v->pool[N.cur];
	bool sp = sel_has_sp(c);
	if (!sp && N.color == 3)
		N.color = 0;
	if (min_pressed(MI_UP)) {
		N.row = (N.row + OPT_COUNT - 1) % OPT_COUNT;
		SE(2);
		ann_row();
	}
	if (min_pressed(MI_DOWN)) {
		N.row = (N.row + 1) % OPT_COUNT;
		SE(2);
		ann_row();
	}
	int d = min_pressed(MI_LEFT) ? -1 : min_pressed(MI_RIGHT) ? 1 : 0;
	if (d && N.row == OPT_COLOR) {
		int nc = sp ? 4 : 3;
		N.color = (N.color + d + nc) % nc;
		SE(4);
	} else if (d && N.row == OPT_INPUT) {
		N.cmd ^= 1;
		SE(4);
	}
	if (min_confirm()) {
		if (N.row != OPT_ACCEPT) {
			N.row = OPT_ACCEPT;
			SE(2);
			ann_row();
			return;
		}
		v->ce0[N.me] = 1.0f;
		SE(0);
		if (N.host) {
			go(NS_STAGE);
			ann("ANNOUNCE_CHARACTERSELECT_STAGE");
		} else {
			enter_link();
		}
	} else if (min_cancel()) {
		go(NS_CHARS);
		SE(1);
		ann("ANNOUNCE_CHARACTERSELECT_CHARACTER");
	}
}

static void stage_update_sel(void)
{
	vssel_state *v = &g_vs;
	if (min_pressed(MI_LEFT)) {
		v->stage_cur = (v->stage_cur + v->nstages - 1) % v->nstages;
		v->d7c = -1.0f;
		SE(2);
	}
	if (min_pressed(MI_RIGHT)) {
		v->stage_cur = (v->stage_cur + 1) % v->nstages;
		v->d7c = 1.0f;
		SE(2);
	}
	if (min_pressed(MI_UP))
		music_sel_step(-1);
	if (min_pressed(MI_DOWN))
		music_sel_step(1);
	if (min_confirm()) {
		SE(0);
		enter_link();
	} else if (min_cancel()) {
		go(NS_OPTIONS);
		SE(1);
		ann("ANNOUNCE_CHARACTERSELECT_ACCEPT");
	}
}

static void link_update(void)
{
	netl_state s = netl_poll();
	if (s == NETL_FAILED) {
		show_error(netl_error(), N.host ? NS_STAGE : NS_OPTIONS);
		return;
	}
	if (s == NETL_READY) {
		vssel_state *v = &g_vs;
		const rb_start *st = netl_start();
		v->chara[N.op] = netl_peer();
		v->color[N.op] = netl_peer_color() + 1; /* palette -> option P1 / P2 / SP */
		v->slot[N.op] = SLOT_READY;
		v->ce0[N.op] = 1.0f;
		v->cc8[N.op] = 0;
		/* START may move our palette (same character + colour: P2 takes the other one) */
		N.color = st->match.colors[N.me] + 1;
		N.stage = st->stage;
		go(NS_WHITE);
		v->ca4 = 0;
		music_request(0, true, true);
		SE(0);
		announce_hide();
		return;
	}
	static netl_state last;
	if (s == NETL_CONNECTED && last != NETL_CONNECTED)
		ann("ANNOUNCE_NET_CONNECT");
	last = s;
	if (min_cancel())
		leave_link();
}

static void vs_update_net(void)
{
	vssel_state *v = &g_vs;
	netl_hold();
	if (netl_get() == NETL_FAILED) {
		show_error(netl_error(), NS_CHARS);
		return;
	}
	if (N.st == NS_WHITE) {
		v->ca4 += 0.01f;
		if (v->ca4 >= 1.0f) { /* VS Select 300 -> 0x136 */
			v->ca4 = 1.0f;
			v->stage = N.stage;
			v->stage_swapped = true;
			go(NS_VS);
			music_request(MUSIC_VERSUS, true, false); /* jingle 2 */
		}
	} else if (N.st == NS_VS) {
		v->ca4 -= 0.025f;
		if (v->ca4 < 0)
			v->ca4 = 0;
		if (N.t > 100) {
			go(NS_GO);
			scene_fade(2);
		}
	}
	if (N.st != NS_GO && min_cancel())
		leave_link();
}

static void netsel_update(void)
{
	vssel_state *v = &g_vs;
	if (!v->stage_swapped) {
		stage3ds_menu_tick();
	} else {
		if (stage_current() != v->stage && v->stage > 0) { /* FUN_004bfca0(4) + FUN_004bfc10(stage) */
			stage3ds_end();
			stage3ds_begin(v->stage);
		}
		stage_g.gameMode = 0;
		stage_g.zoom = 1.0f;
		stage_update();
	}
	touch();
	bool fading_out = N.st == NS_MODE && N.leave;
	switch (N.st) {
	case NS_MODE:
		if (fading_out) {
			if (!scene_fade_busy()) {
				menu_resume_next();
				scene_goto(SCENE_MENU);
			}
			break;
		}
		if (min_pressed(MI_UP) || min_pressed(MI_DOWN)) {
			N.mode ^= 1;
			SE(2);
		}
		if (min_confirm()) {
			set_role(N.mode == 0);
			go(NS_CHARS);
			SE(0);
			ann("ANNOUNCE_CHARACTERSELECT_CHARACTER");
		} else if (min_cancel()) {
			N.leave = 1;
			scene_fade(1);
			SE(1);
			announce_hide();
		}
		break;
	case NS_CHARS: chars_update(); break;
	case NS_OPTIONS: options_update(); break;
	case NS_STAGE: stage_update_sel(); break;
	case NS_LINK: link_update(); break;
	case NS_WHITE:
	case NS_VS:
	case NS_GO:
		vs_update_net();
		if (N.st == NS_GO && !scene_fade_busy()) {
			if (N.leave++ == 1) /* one "NOW LOADING" frame first, like VS Select */
				scene_goto(SCENE_NETPLAY_RUN);
		}
		break;
	case NS_ERROR:
		if (min_confirm() || min_cancel()) {
			SE(1);
			go(N.err_back);
			ann(N.err_back == NS_STAGE     ? "ANNOUNCE_CHARACTERSELECT_STAGE"
			    : N.err_back == NS_OPTIONS ? "ANNOUNCE_CHARACTERSELECT_ACCEPT"
			                               : "ANNOUNCE_CHARACTERSELECT_CHARACTER");
		}
		break;
	}
	/* g_vs mirrors this scene for the shared VS pieces */
	int me = N.me, op = N.op;
	v->cursor[me] = N.cur;
	v->chara[me] = N.st >= NS_LINK && N.st != NS_ERROR ? N.pick : v->pool[N.cur];
	v->color[me] = N.color;
	v->command[me] = N.cmd;
	v->slot[me] = N.st == NS_MODE ? SLOT_CPU : N.st == NS_CHARS ? SLOT_CHOOSE : N.st == NS_OPTIONS ? SLOT_OPTIONS : SLOT_READY;
	bool known = N.st == NS_WHITE || N.st == NS_VS || N.st == NS_GO;
	if (!known) {
		v->chara[op] = SEL_RANDOM;
		v->color[op] = 0;
		v->slot[op] = SLOT_CPU;
	}
	v->cursor[op] = sel_index_of(v->pool, v->n, v->chara[op]) < 0 ? 0 : sel_index_of(v->pool, v->n, v->chara[op]);
	bool freeze = N.st == NS_GO;
	for (int p = 0; p < 2; p++) {
		if (!freeze) {
			approach(&v->cd0[p], v->slot[p] == SLOT_CHOOSE, 0.1f);
			bool stand = p == me ? v->slot[p] >= SLOT_OPTIONS : known || N.st == NS_LINK;
			approach(&v->cc8[p], stand, 0.1f);
			approach(&v->cc0[p], p == me ? N.st != NS_MODE : known, 0.1f);
		}
		to_zero(&v->d74[p], 0.1f);
		approach(&v->ce0[p], false, 0.025f);
		v->ring[p] = (v->ring[p] + 2) % 3600;
		selx_std(p, v->chara[p], v->color[p]);
	}
	approach(&v->ca8, N.st != NS_MODE, 0.1f);
	approach(&v->ca0, N.st == NS_STAGE, 0.1f);
	to_zero(&v->d7c, 0.1f);
	if (!freeze)
		approach(&v->d80, N.st == NS_STAGE || N.st >= NS_LINK, 0.1f);
	approach(&N.mode_a, N.st == NS_MODE && !N.leave, 0.05f);
	approach(&N.link_a, N.st == NS_LINK || known, 0.1f);
	approach(&N.err_a, N.st == NS_ERROR, 0.1f);
	v->frame++;
	N.t++;
}

/* ================================================================ drawing */

static const char *dots(void)
{
	static const char *const d[4] = {"", ".", "..", "..."};
	return d[(N.t / 20) % 4];
}

/* status text of the lobby (top band + bottom), "" if none */
static const char *status_title(char *buf, size_t n)
{
	netl_state s = netl_get();
	if (N.st == NS_ERROR)
		return N.err_conn ? "DISCONNECTED" : "CONNECTION FAILED";
	if (N.st >= NS_WHITE)
		return "CHALLENGER!!";
	if (N.st != NS_LINK)
		return "";
	const char *t = N.host ? (s == NETL_CONNECTED ? "CHALLENGER FOUND" : "WAITING FOR A CHALLENGER")
	                       : (s == NETL_CONNECTED ? "CONNECTING" : "SEARCHING FOR A HOST");
	snprintf(buf, n, "%s%s", t, dots());
	return buf;
}

static void band(float y, float hh, float a)
{
	ui_rect(0, y - hh, W, y + hh, (unsigned)(int)(160 * a) << 24);
	ui_hline(0, W, y - hh, a8(a) | 0xffffff);
	ui_hline(0, W, y + hh - 1, a8(a) | 0xffffff);
}

static void draw_top(void)
{
	vssel_state *v = &g_vs;
	if (N.st == NS_GO && N.leave) {
		sel_draw_loading();
		return;
	}
	selx_backdrop();
	if (v->ca0 > 0)
		selx_stage_select();
	if (v->ca4 > 0)
		ui_rect(0, 0, W, H, a8(v->ca4) | 0xffffff);
	for (int p = 0; p < 2; p++)
		selx_portrait(p);
	for (int p = 0; p < 2; p++)
		selx_wheel(p);
	for (int p = 0; p < 2; p++)
		selx_standing(p);
	/* connection mode: the arcade command-mode bands (FUN_0079f410 layout) */
	float ma = N.mode_a;
	if (ma > 0) {
		selx_pill(H / 2 - 48, 0x9cffa5, tr("MENULABEL_MULTIPLAYER_STARTMENU"), ma, W);
		static const char *const k[2] = {"MENU_MULTIPLAYER_STARTMENU_CREATELOBBY", "MENU_MULTIPLAYER_STARTMENU_QUICKMATCH"};
		for (int i = 0; i < 2; i++) {
			float y = H / 2 - 20 + 33 * (float)i;
			bool sel = i == N.mode;
			ui_rect(0, y - 14, W, y + 14, (unsigned)(int)(144 * ma) << 24 | (sel ? 0xffffff : 0));
			ui_hline(0, W, y - 14, a8(ma));
			ui_hline(0, W, y + 13, a8(ma));
			text_draw(FONT_MENUBIG, tr(k[i]), W / 2, y - 1, TEXT_CENTER, c2d(a8(ma) | (sel ? 0xffffff : 0xa4a4a4)), 1.0f, 0);
		}
	}
	/* lobby / VS / error band (lower third, under the standing art's feet) */
	float la = N.link_a > N.err_a ? N.link_a : N.err_a;
	if (la > 0) {
		float y = H / 2 + 84;
		char buf[64];
		if (N.st >= NS_WHITE && N.st <= NS_GO && N.stage > 0) {
			band(y, 13, la);
			selx_stage_name(N.stage, W / 2, y, 2.0f, a8(la) | 0xffffff);
		} else {
			band(y, 12, la);
			text_draw(FONT_MENUBIG, status_title(buf, sizeof buf), W / 2, y - 1, TEXT_CENTER,
			          c2d(a8(la) | (N.st == NS_ERROR ? 0xff8080 : 0xffffff)), 0.85f, W - 150);
		}
	}
	selx_frame();
	if (N.st != NS_MODE) /* PLAYER1 / PLAYER2 in the top letterbox bar, like VS Select */
		for (int p = 0; p < 2; p++)
			text_draw(FONT_SYSTEM, tr_id(0x1ce + p), p ? W - 6 : 6, 10, p ? TEXT_RIGHT : TEXT_LEFT,
			          c2d(a8(v->ca8) | (p ? 0xc02828 : 0x2850c0)), 0.8f, 88);
}

static void header(const char *label, const char *right)
{
	selx_pill(12, 0x97cbff, label, 1, BW);
	if (right && *right)
		text_draw(FONT_SYSTEM, right, BW - 6, 12, TEXT_RIGHT, c2d(0xffd8e8ff), 0.8f, 150);
}

static const char *role_name(void)
{
	return tr(N.host ? "MENU_MULTIPLAYER_STARTMENU_CREATELOBBY" : "MENU_MULTIPLAYER_STARTMENU_QUICKMATCH");
}

static void draw_mode_bottom(void)
{
	header(tr("MENULABEL_MULTIPLAYER_STARTMENU"), "LOCAL WIRELESS");
	static const char *const k[2] = {"MENU_MULTIPLAYER_STARTMENU_CREATELOBBY", "MENU_MULTIPLAYER_STARTMENU_QUICKMATCH"};
	static const char *const sub[2] = {"MENU_MULTIPLAYER_STARTMENU_CREATELOBBY_SUBTITLE",
	                                   "MENU_MULTIPLAYER_STARTMENU_QUICKMATCH_SUBTITLE"};
	for (int i = 0; i < 2; i++) {
		float y = 28 + 84 * (float)i;
		bool sel = i == N.mode;
		if (sel)
			ui_rect(0, y, BW, y + 80, 0x46ffffff);
		text_draw(FONT_MENUBIG, tr(k[i]), 10, y + 14, TEXT_LEFT, c2d(sel ? WHITE : GREY), 1.0f, BW - 20);
		char buf[300];
		snprintf(buf, sizeof buf, "%s %s", tr(sub[i]),
		         i ? "You play as PLAYER2." : "You play as PLAYER1 and choose the stage.");
		static char wr[400];
		text_wrap(FONT_MENUSUBTITLE, buf, 1.0f, BW - 24, wr, sizeof wr);
		text_draw(FONT_MENUSUBTITLE, wr, 14, y + 36, TEXT_LEFT, c2d(sel ? WHITE : 0x99ffffff), 1.0f, 0);
	}
}

static void draw_chars_bottom(void)
{
	vssel_state *v = &g_vs;
	header(tr_id(0x1ce + N.me), role_name());
	for (int i = 0; i < v->n; i++) {
		float x = 4 + 40 * (float)(i % 5) + 20, y = 26 + 40 * (float)(i / 5) + 20;
		bool sel = i == N.cur;
		if (sel)
			ui_rect(x - 20, y - 20, x + 20, y + 20, 0x80ffffff);
		selx_icon(v->pool[i], x, y, 18, sel ? WHITE : 0xffa0a0a0);
	}
	int c = v->pool[N.cur];
	selx_stats(c, 208, 30, 1.0f);
	text_draw(FONT_MENUBIG, char_name(c), 262, 180, TEXT_CENTER, c2d(WHITE), 1.0f, 108);
}

static const char *color_value(int c, int opt)
{
	if (opt == 3)
		return tr_id(sel_sp_text(c));
	return tr_id(0x1df + opt);
}

static void pick_card(int c, float y) /* big icon + name + difficulty (bottom screen) */
{
	selx_icon(c, 40, y, 26, WHITE);
	text_draw(FONT_MENUBIG, char_name(c), 78, y - 10, TEXT_LEFT, c2d(WHITE), 1.0f, BW - 90);
	if (c >= 3) {
		sel_stats st = sel_char_stats(c);
		char b[64];
		snprintf(b, sizeof b, "%s %s", tr_id(0x1ea), tr_id(0x1eb + st.diff));
		text_draw(FONT_SYSTEM, b, 80, y + 12, TEXT_LEFT, c2d(GREY), 0.8f, BW - 90);
	}
}

static void draw_options_bottom(void)
{
	vssel_state *v = &g_vs;
	header(tr_id(0x1ce + N.me), role_name());
	int c = v->pool[N.cur];
	pick_card(c, 56);
	static const int lab[OPT_COUNT] = {0x1de, 0x1f2, 0};
	for (int r = 0; r < OPT_COUNT; r++) {
		float y = 96 + 30 * (float)r;
		bool sel = r == N.row;
		ui_rect(0, y, BW, y + 28, sel ? 0x60ffffff : 0x40000000);
		unsigned tc = sel ? WHITE : 0xffa0a0a0;
		if (r == OPT_ACCEPT) {
			text_draw(FONT_MENUBIG, tr_id(0x1f5), BW / 2, y + 14, TEXT_CENTER, c2d(tc), 1.0f, 0);
			continue;
		}
		text_draw(FONT_SYSTEM, tr_id(lab[r]), 10, y + 14, TEXT_LEFT, c2d(tc), 0.8f, 100);
		const char *val = r == OPT_COLOR ? color_value(c, N.color) : tr_id(0x1f3 + N.cmd);
		text_draw(FONT_MENU, val, 214, y + 14, TEXT_CENTER, c2d(tc), 0.9f, 150);
		if (sel)
			selx_arrows(130, 298, y + 14, WHITE);
	}
}

static void draw_stage_bottom(void)
{
	vssel_state *v = &g_vs;
	header("STAGE", role_name());
	for (int k = -2; k <= 2; k++) {
		int id = v->stages[((v->stage_cur + k) % v->nstages + v->nstages) % v->nstages];
		float y = 28 + 26 * (float)(k + 2) + 13;
		if (!k)
			ui_rect(0, y - 12, BW, y + 12, 0x60ffffff);
		text_draw(FONT_MENU, stage_name(id), BW / 2, y, TEXT_CENTER, c2d(k ? 0xff909090 : WHITE), k ? 0.85f : 1.0f, BW - 40);
	}
	selx_pill(170, 0x9cffa5, "BGM", 1, BW);
	text_draw(FONT_MENU, music_name(music_list_id(music_sel())), BW / 2, 190, TEXT_CENTER, c2d(WHITE), 0.9f, BW - 20);
}

/* lobby / VS: one card per player (P1 left, P2 right) */
static void player_card(int p, bool known)
{
	vssel_state *v = &g_vs;
	float x0 = p ? 166 : 6, x1 = x0 + 148, y0 = 28, y1 = 128;
	ui_rect(x0, y0, x1, y1, p ? 0xb0802020 : 0xb0204080);
	ui_hline(x0, x1, y0, 0x80ffffff);
	ui_hline(x0, x1, y1 - 1, 0x80ffffff);
	text_draw(FONT_SYSTEM, tr_id(0x1ce + p), x0 + 6, y0 + 10, TEXT_LEFT, c2d(0xffd0d0d0), 0.8f, 0);
	float cx = (x0 + x1) / 2;
	if (!known) {
		float pulse = 0.55f + 0.45f * sinf((float)N.t * 0.08f);
		selx_icon(SEL_RANDOM, cx, y0 + 46, 22, a8(pulse) | 0xffffff);
		text_draw(FONT_MENU, "????", cx, y0 + 82, TEXT_CENTER, c2d(a8(pulse) | 0xffffff), 1.0f, 140);
		return;
	}
	int c = v->chara[p];
	selx_icon(c, cx, y0 + 46, 22, WHITE);
	if (p == N.me || N.st >= NS_WHITE)
		selx_ring(p == 1, cx, y0 + 46, 0.6f, v->ring[p]);
	text_draw(FONT_MENU, char_name(c), cx, y0 + 78, TEXT_CENTER, c2d(WHITE), 1.0f, 140);
	text_draw(FONT_SYSTEM, color_value(c, v->color[p]), cx, y0 + 92, TEXT_CENTER, c2d(GREY), 0.8f, 140);
}

static void draw_link_bottom(void)
{
	netl_state s = netl_get();
	bool known = N.st >= NS_WHITE;
	const char *lab = N.host ? tr("MENULABEL_MULTIPLAYER_LOBBY")
	                         : tr(s == NETL_CONNECTED || known ? "MENULABEL_MULTIPLAYER_JOININGLOBBY"
	                                                           : "MENULABEL_MULTIPLAYER_SEARCHINGLOBBY");
	header(lab, "LOCAL WIRELESS");
	player_card(0, N.me == 0 || known);
	player_card(1, N.me == 1 || known);
	text_draw(FONT_MENUBIG, "VS", BW / 2, 78, TEXT_CENTER, c2d(WHITE), 1.0f, 0);
	char buf[64], line[160];
	if (known) {
		const rb_start *st = netl_start();
		selx_stage_name(N.stage, BW / 2, 148, 2.2f, WHITE);
		snprintf(line, sizeof line, "BGM  %s", music_name(st->bgm));
		if (*music_name(st->bgm))
			text_draw(FONT_SYSTEM, line, BW / 2, 168, TEXT_CENTER, c2d(GREY), 0.8f, BW - 20);
		return;
	}
	text_draw(FONT_MENUBIG, status_title(buf, sizeof buf), BW / 2, 146, TEXT_CENTER, c2d(WHITE), 1.0f, BW - 16);
	if (N.host)
		snprintf(line, sizeof line, "%s  /  Input delay %d f", stage_name(N.stage), g_config.net_delay);
	else if (netl_mismatch())
		snprintf(line, sizeof line, "Found a host running a different build.");
	else
		snprintf(line, sizeof line, "The host chooses the stage and the music.");
	text_draw(FONT_SYSTEM, line, BW / 2, 168, TEXT_CENTER, c2d(netl_mismatch() && !N.host ? 0xffffc060 : GREY), 0.8f,
	          BW - 20);
	text_draw(FONT_SYSTEM, "B  Cancel", BW / 2, 184, TEXT_CENTER, c2d(0xff8090a0), 0.8f, 0);
}

static void draw_error_bottom(void)
{
	header(N.err_conn ? "DISCONNECTED" : "JOIN FAILURE", "LOCAL WIRELESS");
	ui_rect(0, 40, BW, 150, 0x60000000);
	ui_hline(0, BW, 40, 0x80ff8080);
	ui_hline(0, BW, 149, 0x80ff8080);
	const char *t = N.err_conn ? announce_by_id(announce_id_of("ANNOUNCE_NET_DISCONNECT"))
	                           : tr("MENU_MULTIPLAYER_LOBBY_MESSAGE_JOIN_CONNECTFAIL");
	text_draw(FONT_MENUBIG, t && *t ? t : "Disconnected.", BW / 2, 62, TEXT_CENTER, c2d(0xffff9090), 1.0f, BW - 20);
	static char wr[320];
	int lines = text_wrap(FONT_MENU, N.err, 0.9f, BW - 30, wr, sizeof wr);
	text_draw(FONT_MENU, wr, BW / 2, 104 - 8 * (float)(lines - 1), TEXT_CENTER, c2d(WHITE), 0.9f, 0);
	text_draw(FONT_SYSTEM, "A / B  Back", BW / 2, 172, TEXT_CENTER, c2d(0xff8090a0), 0.8f, 0);
}

static void draw_bottom(void)
{
	selx_bottom_bg();
	if (N.st == NS_GO && N.leave)
		return;
	switch (N.st) {
	case NS_MODE: draw_mode_bottom(); break;
	case NS_CHARS: draw_chars_bottom(); break;
	case NS_OPTIONS: draw_options_bottom(); break;
	case NS_STAGE: draw_stage_bottom(); break;
	case NS_ERROR: draw_error_bottom(); break;
	default: draw_link_bottom(); break;
	}
}

static const scene_def netsel_scene = {
	.name = "local wireless", .init = netsel_init, .update = netsel_update, .draw_top = draw_top,
	.draw_bottom = draw_bottom, .exit = netsel_exit,
};

/* ================================================================ the match (blocking) */

static void run_match(void)
{
	text_fonts_unload();
	ui_mtex_free_all();
	selx_free();
	ret_why = netplay_match(scene_top(), scene_console());
	ret_set = true;
	scene_goto(SCENE_NET_SELECT);
}

static const scene_def run_scene = {.name = "netplay", .run = run_match};

void netsel_register(void)
{
	scene_register(SCENE_NET_SELECT, &netsel_scene);
	scene_register(SCENE_NETPLAY_RUN, &run_scene);
}
