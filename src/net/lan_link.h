/*
 * LAN link: both consoles on the same router (infrastructure Wi-Fi), UDP over the soc service.
 *
 * Discovery: the host sends its lobby description (lobby.h) as a UDP broadcast to
 * LOBBY_LAN_DISC_PORT every ~0.5 s from its game socket (bound to LOBBY_LAN_GAME_PORT, or the
 * next free one; the announce carries the port). Browsers bind LOBBY_LAN_DISC_PORT and list
 * what they hear; the datagram's source address is the host.
 * Session: the client sends its handshake (HELLO) straight to the host's game port; the host
 * locks onto the first address that sends a HELLO (later HELLOs from others get a QUIT = "lobby
 * full"). Both sides send a 1-byte keepalive (LINK_PKT_PING) every 250 ms; 5 s of silence =
 * LINK_LOST. Everything else is plain unreliable datagrams like the UDS link.
 *
 * soc needs a page-aligned buffer handed to the service (memalign + socInit); lan_link_init
 * allocates it and checks that the console has an IP on a Wi-Fi network.
 */
#ifndef AOS_NET_LAN_LINK_H
#define AOS_NET_LAN_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "link.h"
#include "lobby.h"

extern const net_link lan_link_ops;

typedef enum { LAN_OK = 0, LAN_ERR_SOC, LAN_ERR_NOWIFI } lan_init_result;

/* socInit (once) + Wi-Fi check. LAN_ERR_NOWIFI = no access point / no IP. */
lan_init_result lan_link_init(void);
void lan_link_exit(void);          /* closes everything, socExit, frees the soc buffer */
bool lan_link_ready(void);
uint32_t lan_link_my_ip(void);     /* gethostid() (network order), 0 unknown */

bool lan_link_host(const lobby_entry *info);              /* open the lobby, start announcing */
bool lan_link_connect(uint32_t ip, uint16_t port);       /* client: target a host (no packet sent yet) */

bool lan_browse_begin(void);       /* bind the discovery port */
/* Drain announces heard since the last call into out (kind LAN, host = sender IP, last_ms = now). */
int lan_browse_poll(lobby_entry *out, int cap, uint32_t now_ms);
void lan_browse_end(void);

void lan_link_close(void);
const char *lan_link_error(void);

#endif
