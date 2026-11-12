/* Lobby discovery data: wire format + browser list (see lobby.h). Platform independent. */
#include "lobby.h"

#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }

/* layout: 0 magic[6] | 6 format | 7 proto | 8 gamever16 | 10 fingerprint32 | 14 session32 |
 *         18 chara | 19 color | 20 stage | 21 status | 22 players | 23 max | 24 port16 | 26 pad[6] |
 *         32 name[16] | 48 */
size_t lobby_encode(const lobby_entry *e, uint8_t *buf, size_t cap)
{
	if (cap < LOBBY_WIRE_SIZE)
		return 0;
	memset(buf, 0, LOBBY_WIRE_SIZE);
	memcpy(buf, LOBBY_MAGIC, 6);
	buf[6] = LOBBY_FORMAT;
	buf[7] = e->proto;
	put16(buf + 8, e->game_version);
	put32(buf + 10, e->fingerprint);
	put32(buf + 14, e->session);
	buf[18] = e->chara;
	buf[19] = e->color;
	buf[20] = e->stage;
	buf[21] = e->status;
	buf[22] = e->players;
	buf[23] = e->max_players;
	put16(buf + 24, e->port);
	for (int i = 0; i < LOBBY_NAME_MAX - 1 && e->name[i]; i++)
		buf[32 + i] = (uint8_t)e->name[i];
	return LOBBY_WIRE_SIZE;
}

bool lobby_decode(const uint8_t *buf, size_t len, lobby_entry *e)
{
	memset(e, 0, sizeof *e);
	if (len < LOBBY_WIRE_SIZE || memcmp(buf, LOBBY_MAGIC, 6) != 0 || buf[6] != LOBBY_FORMAT)
		return false;
	e->proto = buf[7];
	e->game_version = get16(buf + 8);
	e->fingerprint = get32(buf + 10);
	e->session = get32(buf + 14);
	e->chara = buf[18];
	e->color = buf[19];
	e->stage = buf[20];
	e->status = buf[21];
	e->players = buf[22];
	e->max_players = buf[23];
	e->port = get16(buf + 24);
	if (e->status > LOBBY_INMATCH || e->max_players == 0 || e->players > e->max_players)
		return false;
	for (int i = 0; i < LOBBY_NAME_MAX - 1; i++) {
		uint8_t c = buf[32 + i];
		if (!c)
			break;
		e->name[i] = c >= 0x20 && c < 0x7f ? (char)c : '?';
	}
	e->signal = -1;
	return true;
}

bool lobby_name_from_utf16(char *out, const uint16_t *in, size_t n_units)
{
	size_t k = 0;
	bool alnum = false;
	for (size_t i = 0; i < n_units && in[i] && k < LOBBY_NAME_MAX - 1; i++) {
		uint16_t c = in[i];
		if (c >= 0xdc00 && c < 0xe000) /* low surrogate: the pair already gave one '?' */
			continue;
		char a = c >= 0x20 && c < 0x7f ? (char)c : '?';
		if ((a >= '0' && a <= '9') || (a >= 'A' && a <= 'Z') || (a >= 'a' && a <= 'z'))
			alnum = true;
		out[k++] = a;
	}
	while (k > 0 && out[k - 1] == ' ')
		k--;
	out[k] = 0;
	return alnum;
}

void lobby_name_from_id(char *out, uint32_t id)
{
	snprintf(out, LOBBY_NAME_MAX, "3DS-%04X", (unsigned)(id & 0xffff));
}

void lobby_list_clear(lobby_list *l)
{
	memset(l, 0, sizeof *l);
}

int lobby_signal_from_history(uint8_t seen)
{
	int c = 0;
	for (int i = 0; i < 4; i++)
		c += seen >> i & 1;
	return c >= 4 ? 3 : c >= 3 ? 2 : c >= 1 ? 1 : 0;
}

int lobby_list_find(const lobby_list *l, int kind, uint32_t host, uint32_t session)
{
	for (int i = 0; i < l->n; i++)
		if (l->e[i].kind == kind && l->e[i].host == host && l->e[i].session == session)
			return i;
	return -1;
}

static void remove_at(lobby_list *l, int i)
{
	memmove(&l->e[i], &l->e[i + 1], (size_t)(l->n - i - 1) * sizeof l->e[0]);
	l->n--;
	l->version++;
}

int lobby_list_seen(lobby_list *l, const lobby_entry *e, uint32_t now_ms)
{
	int i = -1;
	for (int k = 0; k < l->n; k++)
		if (l->e[k].kind == e->kind && (l->e[k].host == e->host || l->e[k].session == e->session)) {
			i = k;
			break;
		}
	if (i < 0) {
		if (l->n >= LOBBY_LIST_MAX)
			return -1;
		i = l->n++;
		l->e[i] = *e;
		l->e[i].seen = 0;
		l->version++;
	} else {
		uint8_t seen = l->e[i].seen;
		if (l->e[i].session != e->session) { /* the host made a new lobby: same slot, fresh history */
			seen = 0;
			l->version++;
		}
		uint32_t host = l->e[i].session == e->session ? l->e[i].host : e->host; /* heard over 2 paths: keep the first */
		l->e[i] = *e;
		l->e[i].seen = seen;
		l->e[i].host = host;
	}
	lobby_entry *d = &l->e[i];
	d->last_ms = now_ms;
	d->missed = 0;
	if (d->kind == LOBBY_UDS) {
		d->seen |= 1; /* this scan; scan_done shifts */
		d->signal = (int8_t)lobby_signal_from_history(d->seen);
	} else {
		d->signal = -1;
	}
	return i;
}

void lobby_list_scan_done(lobby_list *l, int kind, uint32_t now_ms)
{
	(void)now_ms;
	for (int i = 0; i < l->n;) {
		lobby_entry *d = &l->e[i];
		if (d->kind != kind) {
			i++;
			continue;
		}
		if (!(d->seen & 1) && ++d->missed >= LOBBY_EXPIRE_SCANS) {
			remove_at(l, i);
			continue;
		}
		d->signal = (int8_t)lobby_signal_from_history(d->seen);
		d->seen = (uint8_t)(d->seen << 1); /* open the next scan's bit */
		i++;
	}
}

void lobby_list_expire(lobby_list *l, uint32_t now_ms, uint32_t expire_ms)
{
	for (int i = 0; i < l->n;) {
		uint32_t age = now_ms - l->e[i].last_ms;
		if ((l->e[i].kind == LOBBY_LAN && age > expire_ms) || age > 4 * expire_ms)
			remove_at(l, i);
		else
			i++;
	}
}

void lobby_list_drop_kind(lobby_list *l, int kind)
{
	for (int i = 0; i < l->n;)
		if (l->e[i].kind == kind)
			remove_at(l, i);
		else
			i++;
}
