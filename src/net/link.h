/*
 * Datagram link carrying the handshake + rollback packets between two consoles.
 * rollback.c is transport-agnostic (it only builds / consumes byte packets); the app's lobby and
 * match loop (src/app/netplay.c) talk to whichever link the lobby was made on through this table:
 *   uds_link_ops  Local Wireless (ad-hoc UDS, uds_link.c)
 *   lan_link_ops  LAN (infrastructure Wi-Fi, UDP over soc, lan_link.c)
 * Both deliver unreliable, unordered datagrams to the one peer; reliability is the caller's
 * (redundant input packets, handshake messages resent until answered).
 */
#ifndef AOS_NET_LINK_H
#define AOS_NET_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	LINK_OFF = 0,
	LINK_HOSTING,   /* lobby open, no peer yet */
	LINK_CONNECTED, /* a peer is attached */
	LINK_LOST,      /* peer left / network gone / silent too long */
} link_state;

typedef struct {
	const char *name;                         /* "LOCAL WIRELESS" / "LAN" */
	link_state (*poll)(void);                 /* once per frame (also sends LAN announces / keepalives) */
	bool (*send)(const void *buf, size_t len);
	int (*recv)(void *buf, size_t cap);       /* length, 0 = nothing pending, -1 = error */
	void (*close)(void);                      /* leave / destroy the lobby (transport stays initialised) */
	const char *(*error)(void);
	uint32_t (*send_errors)(void);
	void (*set_status)(uint8_t status, uint8_t players); /* host: lobby_status shown to browsers */
} net_link;

/* Link-level keepalive (LAN; ignored by everything above the link). */
#define LINK_PKT_PING 'P'

#endif
