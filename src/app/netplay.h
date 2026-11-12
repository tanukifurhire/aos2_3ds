/* Two-console versus: Local Wireless (UDS) or LAN (soc/UDP) transport + rollback session. */
#ifndef AOS_APP_NETPLAY_H
#define AOS_APP_NETPLAY_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#include "../input/aos_input.h"
#include "../net/lobby.h"
#include "../net/rollback.h"

/* User button layout -> original input mask (main.c; shared so both modes use one mapping). */
uint16_t to_mask(const aos_input_out *o, u32 kh, const circlePosition *cp);

/* ---- transports (lobby.h lobby_kind): LOBBY_UDS = Local Wireless, LOBBY_LAN = same router ----
 * Only one can be up at a time (UDS takes the wireless module out of infrastructure mode). */
const char *net_transport_check(int kind); /* bring `kind` up now: NULL ok, else why not (no Wi-Fi, ...) */
void net_shutdown(void);     /* stop browsing / leave any lobby, shut both transports down */

/* ---- lobby browser: non-blocking, polled once per frame (netsel.c "View Lobbies") ---- */
bool netb_begin(int kind);   /* false: transport unavailable, netb_error() says why (styled error) */
void netb_poll(void);
void netb_end(void);         /* stop scanning (the transport stays up for a join) */
bool netb_active(void);
int netb_kind(void);
const lobby_list *netb_list(void);
const char *netb_error(void); /* "" while fine; a scan error while browsing */
int netb_scans(void);         /* completed scans (UDS) / announce polls (LAN) since begin */

/* ---- lobby: non-blocking, polled once per frame by the select scene (netsel.c) ---- */
typedef enum {
	NETL_OFF = 0,
	NETL_SEARCHING, /* client: scanning for / contacting a host */
	NETL_HOSTING,   /* host: lobby open, nobody joined yet */
	NETL_CONNECTED, /* both on the network, handshake running (HELLO / START) */
	NETL_READY,     /* handshake done: netl_start() holds the match; netl_peer() is the opponent */
	NETL_FAILED,    /* netl_error() says why */
} netl_state;

/* Host (P1) or join (P2) on transport `kind` with the local pick (engine id, palette 0..2, hyper
 * control). Client: target = lobby from the browser, NULL = Quick Join (first open lobby).
 * Host only: delay = input delay (used by both), stage = resolved stage id, bgm = resolved
 * BGM id, both sent in START. False = failed at once (NETL_FAILED, e.g. wireless off). */
bool netl_begin(bool host, int kind, const lobby_entry *target, int id, int color, int scheme, int delay, int stage,
                int bgm);
netl_state netl_poll(void);  /* once per frame until READY / FAILED */
void netl_hold(void);        /* once per frame while READY (VS screen): answers repeated STARTs, sees QUIT / loss */
void netl_cancel(void);      /* B: tell the other console (QUIT) and leave the network */
netl_state netl_get(void);
int netl_kind(void);         /* transport of the current / last lobby */
bool netl_mismatch(void);    /* client: found a host with a different build */
const char *netl_error(void);
int netl_peer(void);         /* opponent's character (engine id), -1 while unknown */
int netl_peer_color(void);   /* opponent's palette once known */
const char *netl_peer_name(void); /* client: the host's nickname ("" unknown) */
const char *netl_my_name(void);   /* this console's nickname (3DS profile, else "3DS-XXXX") */
const rb_start *netl_start(void); /* final match settings (valid in READY) */

/* Runs the rollback match of a READY lobby (owns the loop until it ends) and leaves the
 * network. Returns NULL when the local player quit (START), else why it ended. */
const char *netplay_match(C3D_RenderTarget *top, PrintConsole *con);

/* Multiplayer select / lobby browser scenes (netsel.c): SCENE_NET_SELECT + the match runner. */
void netsel_register(void);

#endif
