/* Local-wireless versus (UDS transport + rollback session). */
#ifndef AOS_APP_NETPLAY_H
#define AOS_APP_NETPLAY_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#include "../input/aos_input.h"
#include "../net/rollback.h"

/* User button layout -> original input mask (main.c; shared so both modes use one mapping). */
uint16_t to_mask(const aos_input_out *o, u32 kh, const circlePosition *cp);

/* ---- lobby: non-blocking, polled once per frame by the select scene (netsel.c) ---- */
typedef enum {
	NETL_OFF = 0,
	NETL_SEARCHING, /* client: scanning for a host's beacon */
	NETL_HOSTING,   /* host: network up, nobody joined yet */
	NETL_CONNECTED, /* both on the network, handshake running (HELLO / START) */
	NETL_READY,     /* handshake done: netl_start() holds the match; netl_peer() is the opponent */
	NETL_FAILED,    /* netl_error() says why */
} netl_state;

/* Host (P1) or join (P2) with the local pick (engine id, palette 0..2, hyper control).
 * Host only: delay = input delay (used by both), stage = resolved stage id, bgm = resolved
 * BGM id, both sent in START. False = failed at once (NETL_FAILED, e.g. wireless off). */
bool netl_begin(bool host, int id, int color, int scheme, int delay, int stage, int bgm);
netl_state netl_poll(void);  /* once per frame until READY / FAILED */
void netl_hold(void);        /* once per frame while READY (VS screen): answers repeated STARTs, sees QUIT / loss */
void netl_cancel(void);      /* B: tell the other console (QUIT) and leave the network */
netl_state netl_get(void);
bool netl_mismatch(void);    /* client: found a host with a different build */
const char *netl_error(void);
int netl_peer(void);         /* opponent's character (engine id), -1 while unknown */
int netl_peer_color(void);   /* opponent's palette once known */
const rb_start *netl_start(void); /* final match settings (valid in READY) */

/* Runs the rollback match of a READY lobby (owns the loop until it ends) and leaves the
 * network. Returns NULL when the local player quit (START), else why it ended. */
const char *netplay_match(C3D_RenderTarget *top, PrintConsole *con);

/* Local-wireless select / lobby scenes (netsel.c): SCENE_NET_SELECT + the match runner. */
void netsel_register(void);

#endif
