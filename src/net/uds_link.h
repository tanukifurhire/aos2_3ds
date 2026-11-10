/*
 * Local-wireless (UDS / "download play"-style ad-hoc) datagram link between
 * two 3DS consoles, used as the transport for the rollback session.
 *
 * UNTESTED ON HARDWARE: this file is written against libctru's uds.h and the
 * libctru UDS example, but has not been run between two consoles (or in an
 * emulator with UDS support). Everything above it (rollback.c and its wire
 * format) is host-tested; see tests/test_rollback.c.
 *
 * Model: the host creates a 2-node network whose beacon carries a small
 * appdata tag ("AOS2NET", protocol, sim fingerprint); the client scans for
 * that tag and joins. After that both sides just exchange unreliable
 * datagrams on one data channel (broadcast to the other node). Reliability is
 * the caller's job: the rollback packets are redundant (every packet repeats
 * all unacknowledged inputs) and the handshake messages are resent each frame
 * until answered.
 */
#ifndef AOS_NET_UDS_LINK_H
#define AOS_NET_UDS_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	UDS_LINK_OFF = 0,
	UDS_LINK_HOSTING,   /* network created, no client yet */
	UDS_LINK_CONNECTED, /* two nodes on the network */
	UDS_LINK_LOST,      /* peer left / network gone */
} uds_link_state;

/* udsInit; false (with a message in uds_link_error()) e.g. when wireless is off. */
bool uds_link_init(void);
void uds_link_exit(void);

/* Host: create the network. Non-blocking afterwards; poll uds_link_poll(). */
bool uds_link_host(uint32_t fingerprint);
/* Client: one beacon scan (blocks ~0.5-1 s) and, if a matching host is found,
 * connect. Returns true when connected; false = none found yet / error. */
bool uds_link_join(uint32_t fingerprint, bool *fingerprint_mismatch);

/* Update and return the connection state (call once per frame). */
uds_link_state uds_link_poll(void);

/* Send one datagram (<= 1478 bytes) to the other node. */
bool uds_link_send(const void *buf, size_t len);
/* Receive one datagram: length, 0 = nothing pending, -1 = error. */
int uds_link_recv(void *buf, size_t cap);

/* Leave / destroy the network (keeps UDS initialised). */
void uds_link_close(void);

const char *uds_link_error(void);
uint32_t uds_link_send_errors(void);

#endif
