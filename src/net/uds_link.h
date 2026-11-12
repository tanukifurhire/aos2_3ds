/*
 * Local-wireless (UDS / "download play"-style ad-hoc) datagram link between
 * two 3DS consoles, used as the transport for the rollback session.
 *
 * UNTESTED ON HARDWARE: this file is written against libctru's uds.h and the
 * libctru UDS example, but has not been run between two consoles (or in an
 * emulator with UDS support). Everything above it (rollback.c and its wire
 * format) is host-tested; see tests/test_rollback.c.
 *
 * Model: the host creates a 2-node network whose beacon carries the lobby
 * description (lobby.h: magic, protocol, sim fingerprint, host character,
 * stage, nickname, status) as appdata; browsers scan beacons and list them
 * without connecting, then connect to the chosen one. After that both sides
 * just exchange unreliable datagrams on one data channel (broadcast to the
 * other node). Reliability is the caller's job.
 *
 * Note: udsInit puts the wireless module into the "local communications"
 * exclusive state (NDM), which drops an infrastructure Wi-Fi connection — so
 * Local Wireless and LAN (lan_link.h) can't run at the same time.
 */
#ifndef AOS_NET_UDS_LINK_H
#define AOS_NET_UDS_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "link.h"
#include "lobby.h"

extern const net_link uds_link_ops;

/* udsInit; false (with a message in uds_link_error()) e.g. when wireless is off. */
bool uds_link_init(void);
void uds_link_exit(void);
bool uds_link_ready(void);

/* Host: create the network advertising `info` (kind/host ignored). Non-blocking afterwards. */
bool uds_link_host(const lobby_entry *info);
/* One beacon scan (blocks ~0.5-1 s; run it on a worker thread). Fills up to cap lobbies
 * (kind = LOBBY_UDS, host = MAC low bytes, last_ms = now_ms) and remembers their networks for
 * uds_link_connect. Returns the count, -1 on error. */
int uds_link_scan(lobby_entry *out, int cap, uint32_t now_ms);
/* Connect to a lobby from the last scan (session 0 = whatever lobby that console has open).
 * False: gone / error (uds_link_error()). */
bool uds_link_connect(uint32_t host, uint32_t session);

void uds_link_close(void);
const char *uds_link_error(void);

#endif
