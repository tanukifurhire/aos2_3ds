/* UDS transport — UNTESTED ON HARDWARE (see uds_link.h). */
#include "uds_link.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WLAN_COMM_ID 0x41325333u /* "A2S3": arbitrary, identifies this application */
#define NET_ID8 1               /* network kind: versus */
#define DATA_CHANNEL 1          /* must be non-zero */
#define SCAN_BUF_SIZE 0x4000

static const char passphrase[] = "aos2-3ds-versus";

static bool inited, on_network, hosting;
static udsBindContext bindctx;
static link_state state = LINK_OFF;
static char err[96];
static uint32_t send_errors;
static lobby_entry my_info;

/* networks of the last scan, for uds_link_connect */
static struct {
	udsNetworkStruct net;
	uint32_t host, session;
} seen_nets[LOBBY_LIST_MAX];
static int n_seen;

static void set_err(const char *what, Result r)
{
	snprintf(err, sizeof err, "%s failed (0x%08lx)", what, (unsigned long)r);
}

const char *uds_link_error(void) { return err; }
static uint32_t uds_send_errors(void) { return send_errors; }
bool uds_link_ready(void) { return inited; }

bool uds_link_init(void)
{
	if (inited)
		return true;
	Result r = udsInit(0x3000, NULL); /* shared memory: 0x1000-aligned, >= recv buffer */
	if (R_FAILED(r)) {
		set_err("udsInit (is wireless enabled?)", r);
		return false;
	}
	inited = true;
	return true;
}

void uds_link_exit(void)
{
	uds_link_close();
	if (inited)
		udsExit();
	inited = false;
	n_seen = 0;
}

static bool push_appdata(void)
{
	uint8_t a[LOBBY_WIRE_SIZE];
	size_t k = lobby_encode(&my_info, a, sizeof a);
	Result r = udsSetApplicationData(a, k);
	if (R_FAILED(r)) {
		set_err("udsSetApplicationData", r);
		return false;
	}
	return true;
}

bool uds_link_host(const lobby_entry *info)
{
	if (!inited && !uds_link_init())
		return false;
	uds_link_close();
	udsNetworkStruct net;
	udsGenerateDefaultNetworkStruct(&net, WLAN_COMM_ID, NET_ID8, 2);
	Result r = udsCreateNetwork(&net, passphrase, sizeof passphrase, &bindctx, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
	if (R_FAILED(r)) {
		set_err("udsCreateNetwork", r);
		return false;
	}
	on_network = hosting = true;
	my_info = *info;
	my_info.players = 1;
	my_info.max_players = 2;
	my_info.status = LOBBY_OPEN;
	if (!push_appdata()) {
		uds_link_close();
		return false;
	}
	state = LINK_HOSTING;
	return true;
}

static void uds_set_status(uint8_t status, uint8_t players)
{
	if (!hosting || (my_info.status == status && my_info.players == players))
		return;
	my_info.status = status;
	my_info.players = players;
	push_appdata();
}

static uint32_t mac_key(const u8 *mac)
{
	return (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5];
}

int uds_link_scan(lobby_entry *out, int cap, uint32_t now_ms)
{
	if (!inited && !uds_link_init())
		return -1;
	void *scanbuf = malloc(SCAN_BUF_SIZE);
	if (!scanbuf) {
		snprintf(err, sizeof err, "out of memory (scan buffer)");
		return -1;
	}
	udsNetworkScanInfo *nets = NULL;
	size_t total = 0;
	Result r = udsScanBeacons(scanbuf, SCAN_BUF_SIZE, &nets, &total, WLAN_COMM_ID, NET_ID8, NULL, false);
	free(scanbuf);
	if (R_FAILED(r)) {
		set_err("udsScanBeacons", r);
		free(nets);
		return -1;
	}
	int n = 0;
	n_seen = 0;
	for (size_t i = 0; i < total && n < cap; i++) {
		uint8_t a[0xc8];
		size_t got = 0;
		lobby_entry e;
		if (R_FAILED(udsGetNetworkStructApplicationData(&nets[i].network, a, sizeof a, &got)) ||
		    !lobby_decode(a, got, &e))
			continue;
		e.kind = LOBBY_UDS;
		e.host = mac_key(nets[i].network.host_macaddress);
		e.last_ms = now_ms;
		/* the beacon's own node count is authoritative for "full" */
		e.players = nets[i].network.total_nodes;
		if (nets[i].network.total_nodes >= nets[i].network.max_nodes && e.status == LOBBY_OPEN)
			e.status = LOBBY_FULL;
		out[n++] = e;
		if (n_seen < LOBBY_LIST_MAX) {
			seen_nets[n_seen].net = nets[i].network;
			seen_nets[n_seen].host = e.host;
			seen_nets[n_seen].session = e.session;
			n_seen++;
		}
	}
	free(nets);
	return n;
}

bool uds_link_connect(uint32_t host, uint32_t session)
{
	if (!inited && !uds_link_init())
		return false;
	uds_link_close();
	for (int i = 0; i < n_seen; i++) {
		if (seen_nets[i].host != host || (session && seen_nets[i].session != session))
			continue;
		Result r = udsConnectNetwork(&seen_nets[i].net, passphrase, sizeof passphrase, &bindctx,
		                             UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
		if (R_FAILED(r)) {
			set_err("udsConnectNetwork", r);
			return false;
		}
		on_network = true;
		hosting = false;
		state = LINK_CONNECTED;
		return true;
	}
	snprintf(err, sizeof err, "The lobby is gone.");
	return false;
}

static link_state uds_poll(void)
{
	if (!on_network)
		return state;
	udsConnectionStatus cs;
	if (R_FAILED(udsGetConnectionStatus(&cs)))
		return state;
	/* status 6 = host, 9 = client on 3dbrew; rely on node count instead */
	if (cs.total_nodes >= 2)
		state = LINK_CONNECTED;
	else if (state == LINK_CONNECTED)
		state = LINK_LOST;
	return state;
}

static bool uds_send(const void *buf, size_t len)
{
	if (!on_network || len == 0 || len > UDS_DATAFRAME_MAXSIZE)
		return false;
	Result r = udsSendTo(UDS_BROADCAST_NETWORKNODEID, DATA_CHANNEL, UDS_SENDFLAG_Default, buf, len);
	if (R_FAILED(r) && (u32)r != 0xC86113F0u) { /* UDS_CHECK_SENDTO_FATALERROR without the sign-compare warning */
		send_errors++;
		set_err("udsSendTo", r);
		return false;
	}
	return true;
}

static int uds_recv(void *buf, size_t cap)
{
	if (!on_network)
		return -1;
	size_t got = 0;
	u16 src = 0;
	Result r = udsPullPacket(&bindctx, buf, cap, &got, &src);
	if (R_FAILED(r)) {
		set_err("udsPullPacket", r);
		return -1;
	}
	return (int)got;
}

void uds_link_close(void)
{
	if (on_network) {
		if (hosting)
			udsDestroyNetwork();
		else
			udsDisconnectNetwork();
		udsUnbind(&bindctx);
	}
	on_network = hosting = false;
	state = LINK_OFF;
	err[0] = 0;
}

const net_link uds_link_ops = {
	.name = "LOCAL WIRELESS",
	.poll = uds_poll,
	.send = uds_send,
	.recv = uds_recv,
	.close = uds_link_close,
	.error = uds_link_error,
	.send_errors = uds_send_errors,
	.set_status = uds_set_status,
};
