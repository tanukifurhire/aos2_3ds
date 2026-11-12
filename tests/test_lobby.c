/* Lobby discovery data (src/net/lobby.c): LAN announce / UDS appdata encode-decode, nickname
 * conversion, browser list merge / signal / expiry. */
#include <stdio.h>
#include <string.h>

#include "../src/net/lobby.h"

static int fails;
#define CHECK(c)                                                                 \
	do {                                                                         \
		if (!(c)) {                                                              \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
			fails++;                                                             \
		}                                                                        \
	} while (0)

static lobby_entry mk(uint32_t host, uint32_t session, int kind)
{
	lobby_entry e;
	memset(&e, 0, sizeof e);
	e.proto = 4;
	e.game_version = 0x5a2;
	e.fingerprint = 0xdeadbeef;
	e.session = session;
	e.chara = 3;
	e.color = 1;
	e.stage = 15;
	e.status = LOBBY_OPEN;
	e.players = 1;
	e.max_players = 2;
	e.port = LOBBY_LAN_GAME_PORT;
	snprintf(e.name, sizeof e.name, "Host%u", (unsigned)host);
	e.kind = (uint8_t)kind;
	e.host = host;
	return e;
}

static void test_codec(void)
{
	lobby_entry e = mk(7, 0x12345678, LOBBY_LAN), d;
	e.chara = 17;
	e.color = 2;
	e.stage = 19;
	e.status = LOBBY_INMATCH;
	e.players = 2;
	e.port = 39015;
	snprintf(e.name, sizeof e.name, "ABCDEFGHIJKLMNO"); /* 15 chars = the maximum */
	uint8_t b[LOBBY_WIRE_SIZE + 8];
	CHECK(lobby_encode(&e, b, LOBBY_WIRE_SIZE - 1) == 0);
	size_t n = lobby_encode(&e, b, sizeof b);
	CHECK(n == LOBBY_WIRE_SIZE);
	CHECK(n <= 0xc8); /* fits the UDS beacon appdata */
	CHECK(memcmp(b, "AOS2LB", 6) == 0);
	CHECK(b[10] == 0xef && b[13] == 0xde); /* little endian on the wire */
	CHECK(lobby_decode(b, n, &d));
	CHECK(d.proto == 4 && d.game_version == 0x5a2 && d.fingerprint == 0xdeadbeef && d.session == 0x12345678);
	CHECK(d.chara == 17 && d.color == 2 && d.stage == 19 && d.status == LOBBY_INMATCH);
	CHECK(d.players == 2 && d.max_players == 2 && d.port == 39015);
	CHECK(strcmp(d.name, "ABCDEFGHIJKLMNO") == 0);
	CHECK(d.kind == 0 && d.host == 0 && d.signal == -1); /* receiver fields are not on the wire */
	CHECK(lobby_compatible(&d, 4, 0x5a2, 0xdeadbeef));
	CHECK(!lobby_compatible(&d, 5, 0x5a2, 0xdeadbeef));
	CHECK(!lobby_compatible(&d, 4, 0x5a2, 0xdeadbeee));
	CHECK(!lobby_joinable(&d, 4, 0x5a2, 0xdeadbeef)); /* in match */

	/* rejects: short, bad magic, other format, inconsistent counts / status */
	CHECK(!lobby_decode(b, n - 1, &d));
	uint8_t c[LOBBY_WIRE_SIZE];
	memcpy(c, b, n);
	c[0] = 'X';
	CHECK(!lobby_decode(c, n, &d));
	memcpy(c, b, n);
	c[6] = LOBBY_FORMAT + 1;
	CHECK(!lobby_decode(c, n, &d));
	memcpy(c, b, n);
	c[22] = 3; /* players > max */
	CHECK(!lobby_decode(c, n, &d));
	memcpy(c, b, n);
	c[21] = 7; /* unknown status */
	CHECK(!lobby_decode(c, n, &d));
	memcpy(c, b, n);
	c[23] = 0;
	c[22] = 0;
	CHECK(!lobby_decode(c, n, &d));
	/* the old "AOS2NET" UDS appdata of previous builds is not a lobby */
	uint8_t old[16] = {'A', 'O', 'S', '2', 'N', 'E', 'T', 0, 1};
	CHECK(!lobby_decode(old, sizeof old, &d));
	/* name bytes are sanitised; no terminator needed on the wire */
	memcpy(c, b, n);
	c[32] = 0x01;
	c[33] = 0xc3;
	CHECK(lobby_decode(c, n, &d));
	CHECK(d.name[0] == '?' && d.name[1] == '?' && strlen(d.name) == 15);
	/* longer datagram (future fields) still decodes */
	uint8_t big[64];
	memset(big, 0x55, sizeof big);
	memcpy(big, b, n);
	CHECK(lobby_decode(big, sizeof big, &d));
}

static void test_names(void)
{
	char out[LOBBY_NAME_MAX];
	const uint16_t a[] = {'S', 'u', 'g', 'u', 'r', 'i', ' ', ' ', 0};
	CHECK(lobby_name_from_utf16(out, a, 10) && strcmp(out, "Suguri") == 0);
	const uint16_t jp[] = {0x30b9, 0x30b0, 0x30ea, 0}; /* katakana only: no usable letters */
	CHECK(!lobby_name_from_utf16(out, jp, 10));
	const uint16_t mix[] = {'K', 'a', 0x00e9, 0xd83d, 0xde00, '2', 0};
	CHECK(lobby_name_from_utf16(out, mix, 10) && strcmp(out, "Ka??2") == 0);
	const uint16_t full[10] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'}; /* no terminator */
	CHECK(lobby_name_from_utf16(out, full, 10) && strcmp(out, "ABCDEFGHIJ") == 0);
	lobby_name_from_id(out, 0x1234abcd);
	CHECK(strcmp(out, "3DS-ABCD") == 0);
}

static void test_list_lan(void)
{
	lobby_list l;
	lobby_list_clear(&l);
	lobby_entry a = mk(0x0a00000a, 100, LOBBY_LAN), b = mk(0x0a00000b, 200, LOBBY_LAN);
	CHECK(lobby_list_seen(&l, &a, 1000) == 0);
	CHECK(lobby_list_seen(&l, &b, 1100) == 1);
	CHECK(l.n == 2 && l.e[0].signal == -1);
	uint32_t v = l.version;
	/* re-announce: same slot, fields updated, no version bump */
	a.status = LOBBY_FULL;
	a.players = 2;
	CHECK(lobby_list_seen(&l, &a, 1500) == 0);
	CHECK(l.n == 2 && l.e[0].status == LOBBY_FULL && l.e[0].last_ms == 1500 && l.version == v);
	/* same lobby heard over another path (loopback / second interface): merged by session, keeps first host */
	lobby_entry a2 = a;
	a2.host = 0x7f000001;
	CHECK(lobby_list_seen(&l, &a2, 1600) == 0 && l.n == 2 && l.e[0].host == 0x0a00000a);
	/* host makes a new lobby: same slot, new session */
	a.session = 101;
	CHECK(lobby_list_seen(&l, &a, 1700) == 0 && l.n == 2 && l.e[0].session == 101 && l.version != v);
	CHECK(lobby_list_find(&l, LOBBY_LAN, 0x0a00000a, 101) == 0);
	CHECK(lobby_list_find(&l, LOBBY_LAN, 0x0a00000a, 100) == -1);
	CHECK(lobby_list_find(&l, LOBBY_UDS, 0x0a00000a, 101) == -1);
	/* expiry: b last seen at 1100 */
	lobby_list_expire(&l, 1100 + LOBBY_EXPIRE_MS, LOBBY_EXPIRE_MS);
	CHECK(l.n == 2);
	lobby_list_expire(&l, 1101 + LOBBY_EXPIRE_MS, LOBBY_EXPIRE_MS);
	CHECK(l.n == 1 && l.e[0].host == 0x0a00000a);
	/* clock wrap-around is fine (unsigned age) */
	lobby_list_clear(&l);
	lobby_list_seen(&l, &b, 0xfffffff0u);
	lobby_list_expire(&l, 0x10, LOBBY_EXPIRE_MS);
	CHECK(l.n == 1);
	/* capacity */
	lobby_list_clear(&l);
	for (int i = 0; i < LOBBY_LIST_MAX; i++) {
		lobby_entry e = mk((uint32_t)i + 1, (uint32_t)i + 1000, LOBBY_LAN);
		CHECK(lobby_list_seen(&l, &e, 0) == i);
	}
	lobby_entry x = mk(999, 999, LOBBY_LAN);
	CHECK(lobby_list_seen(&l, &x, 0) == -1 && l.n == LOBBY_LIST_MAX);
	/* removal keeps the order of the rest */
	l.e[3].last_ms = 0;
	for (int i = 0; i < l.n; i++)
		if (i != 3)
			l.e[i].last_ms = 5000;
	lobby_list_expire(&l, 5000, LOBBY_EXPIRE_MS);
	CHECK(l.n == LOBBY_LIST_MAX - 1 && l.e[3].host == 5 && l.e[2].host == 3);
	lobby_list_drop_kind(&l, LOBBY_UDS);
	CHECK(l.n == LOBBY_LIST_MAX - 1);
	lobby_list_drop_kind(&l, LOBBY_LAN);
	CHECK(l.n == 0);
}

static void test_list_uds(void)
{
	lobby_list l;
	lobby_list_clear(&l);
	lobby_entry a = mk(0xa1, 1, LOBBY_UDS), b = mk(0xb2, 2, LOBBY_UDS), lan = mk(0xc3, 3, LOBBY_LAN);
	lobby_list_seen(&l, &lan, 0);
	/* scan 1: both */
	lobby_list_seen(&l, &a, 0);
	lobby_list_seen(&l, &b, 0);
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	CHECK(l.n == 3 && l.e[1].signal == 1 && l.e[2].signal == 1);
	/* scans 2-4: a every time, b in scan 3 only */
	for (int s = 2; s <= 4; s++) {
		lobby_list_seen(&l, &a, 0);
		if (s == 3)
			lobby_list_seen(&l, &b, 0);
		lobby_list_scan_done(&l, LOBBY_UDS, 0);
	}
	CHECK(l.e[1].host == 0xa1 && l.e[1].signal == 3); /* seen 4/4 */
	CHECK(l.e[2].host == 0xb2 && l.e[2].signal == 1 && l.e[2].missed == 1);
	/* b missing twice more -> 3 consecutive misses -> gone; the LAN entry is untouched */
	for (int s = 0; s < 2; s++) {
		lobby_list_seen(&l, &a, 0);
		lobby_list_scan_done(&l, LOBBY_UDS, 0);
	}
	CHECK(l.n == 2 && l.e[0].kind == LOBBY_LAN && l.e[1].host == 0xa1);
	/* a fades: seen, missed, missed -> 1 bar, then gone */
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	CHECK(l.n == 2 && l.e[1].signal == 2);
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	CHECK(l.n == 2 && l.e[1].signal == 1);
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	CHECK(l.n == 1);
	/* a reappearing lobby with a new session from the same console: fresh history */
	lobby_list_seen(&l, &b, 0);
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	lobby_list_seen(&l, &b, 0);
	lobby_list_scan_done(&l, LOBBY_UDS, 0);
	b.session = 77;
	int i = lobby_list_seen(&l, &b, 0);
	CHECK(i == 1 && l.e[i].session == 77 && l.e[i].signal == 1);
	CHECK(lobby_signal_from_history(0) == 0 && lobby_signal_from_history(0x0f) == 3 &&
	      lobby_signal_from_history(0xf0) == 0 && lobby_signal_from_history(0x07) == 2);
}

int main(void)
{
	test_codec();
	test_names();
	test_list_lan();
	test_list_uds();
	if (fails) {
		printf("test_lobby: %d FAILED\n", fails);
		return 1;
	}
	printf("test_lobby: ok\n");
	return 0;
}
