/*
 * Lobby discovery data (platform independent, host-tested by tests/test_lobby.c).
 *
 * One lobby = one hosting console. Its description travels
 *   - Local Wireless (UDS): in the beacon's application data (udsSetApplicationData), so a
 *     scan lists every lobby without connecting;
 *   - LAN (infrastructure Wi-Fi, soc): in a UDP broadcast "announce" datagram the host sends
 *     every ~0.5 s to LOBBY_LAN_DISC_PORT; the source address of the datagram is the host.
 * Both use the same fixed little-endian encoding (lobby_encode / lobby_decode).
 *
 * The browser keeps a lobby_list: entries are merged by (kind, host, session), keep their
 * position (the cursor doesn't jump while the list refreshes), get a signal estimate from how
 * many recent scans saw them (UDS: no RSSI is exposed by libctru's beacon scan), and expire
 * when not seen for a while.
 */
#ifndef AOS_NET_LOBBY_H
#define AOS_NET_LOBBY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOBBY_MAGIC "AOS2LB"     /* 6 bytes */
#define LOBBY_FORMAT 1           /* encoding version of the announce / appdata */
#define LOBBY_WIRE_SIZE 48       /* lobby_encode output size (fits the 0xC8-byte beacon appdata) */
#define LOBBY_NAME_MAX 16        /* nickname bytes incl. NUL (3DS nicknames are <= 10 UTF-16 chars) */
#define LOBBY_LIST_MAX 16
#define LOBBY_LAN_DISC_PORT 39012 /* browsers listen here for announces */
#define LOBBY_LAN_GAME_PORT 39013 /* host's game socket (announce carries the port actually bound) */
#define LOBBY_EXPIRE_MS 4000     /* LAN: not announced for this long = gone */
#define LOBBY_EXPIRE_SCANS 3     /* UDS: missing from this many scans in a row = gone */

typedef enum { LOBBY_UDS = 0, LOBBY_LAN = 1, LOBBY_KINDS } lobby_kind;
typedef enum { LOBBY_OPEN = 0, LOBBY_FULL = 1, LOBBY_INMATCH = 2 } lobby_status;

typedef struct {
	/* on the wire */
	uint8_t proto;           /* RB_PROTO_VERSION of the host */
	uint16_t game_version;   /* AOS_GAME_VERSION */
	uint32_t fingerprint;    /* rb_sim_fingerprint() */
	uint32_t session;        /* random per hosted lobby */
	uint8_t chara, color;    /* host's character (engine id) + palette */
	uint8_t stage;           /* host's stage id (random already resolved), 0 none */
	uint8_t status;          /* lobby_status */
	uint8_t players, max_players;
	uint16_t port;           /* LAN: host's game port; UDS: 0 */
	char name[LOBBY_NAME_MAX]; /* host nickname, printable ASCII, NUL terminated */
	/* filled by the receiver */
	uint8_t kind;            /* lobby_kind */
	uint32_t host;           /* LAN: IPv4 (network byte order as read by soc); UDS: low 4 bytes of the MAC */
	uint32_t last_ms;        /* last time seen */
	uint8_t seen;            /* bit history of the last 8 scans (bit 0 = latest); signal estimate */
	uint8_t missed;          /* UDS: consecutive scans without it */
	int8_t signal;           /* 0..3 bars (lobby_list derives it), -1 = n/a */
} lobby_entry;

size_t lobby_encode(const lobby_entry *e, uint8_t *buf, size_t cap); /* LOBBY_WIRE_SIZE or 0 */
bool lobby_decode(const uint8_t *buf, size_t len, lobby_entry *e);   /* wire fields only; rest zeroed */

/* Same build (protocol, game version, simulation fingerprint)? */
static inline bool lobby_compatible(const lobby_entry *e, uint8_t proto, uint16_t gamever, uint32_t fp)
{
	return e->proto == proto && e->game_version == gamever && e->fingerprint == fp;
}
static inline bool lobby_joinable(const lobby_entry *e, uint8_t proto, uint16_t gamever, uint32_t fp)
{
	return e->status == LOBBY_OPEN && e->players < e->max_players && lobby_compatible(e, proto, gamever, fp);
}

/* Copy a nickname into a LOBBY_NAME_MAX buffer: UTF-16 (3DS profile) -> printable ASCII
 * (other characters -> '?'; the menu fonts only carry Latin glyphs). Returns false if the
 * result is empty / has no letter or digit (caller then uses a console id). */
bool lobby_name_from_utf16(char *out, const uint16_t *in, size_t n_units);
/* Fallback name from a console id: "3DS-XXXX" (hex of the low 16 bits). */
void lobby_name_from_id(char *out, uint32_t id);

typedef struct {
	lobby_entry e[LOBBY_LIST_MAX];
	int n;
	uint32_t version;  /* bumped whenever an entry is added / removed */
} lobby_list;

void lobby_list_clear(lobby_list *l);
/* Merge one sighting (kind, host and the wire fields set; now = time). Returns the entry's
 * index, -1 if the list is full. Entries match by kind + (host or session) and keep their slot; a
 * new session from the same host replaces the old one in place (fresh signal history). */
int lobby_list_seen(lobby_list *l, const lobby_entry *e, uint32_t now_ms);
/* UDS: a scan of `kind` finished: entries not seen in it shift a 0 into their history and
 * count a miss; LOBBY_EXPIRE_SCANS misses drop them. Updates the signal bars. */
void lobby_list_scan_done(lobby_list *l, int kind, uint32_t now_ms);
/* Drop LAN entries older than expire_ms (timer based; announces are periodic). Also drops
 * entries of any kind older than 4 * expire_ms (a stalled scanner). */
void lobby_list_expire(lobby_list *l, uint32_t now_ms, uint32_t expire_ms);
/* Remove every entry of one kind (transport switched off). */
void lobby_list_drop_kind(lobby_list *l, int kind);
int lobby_list_find(const lobby_list *l, int kind, uint32_t host, uint32_t session);
/* Signal bars 0..3 from a sighting history (popcount of the last 4 scans, at least 1 if seen). */
int lobby_signal_from_history(uint8_t seen);

#endif
