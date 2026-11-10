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
#define APPDATA_MAGIC "AOS2NET"

static const char passphrase[] = "aos2-3ds-versus";

static bool inited, on_network, hosting;
static udsBindContext bindctx;
static uds_link_state state = UDS_LINK_OFF;
static char err[96];
static uint32_t send_errors;

typedef struct {
	char magic[8];
	uint8_t proto;
	uint8_t pad[3];
	uint32_t fingerprint;
} appdata_t;

static void set_err(const char *what, Result r)
{
	snprintf(err, sizeof err, "%s failed (0x%08lx)", what, (unsigned long)r);
}

const char *uds_link_error(void) { return err; }
uint32_t uds_link_send_errors(void) { return send_errors; }

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
}

static void make_appdata(appdata_t *a, uint32_t fingerprint)
{
	memset(a, 0, sizeof *a);
	memcpy(a->magic, APPDATA_MAGIC, sizeof a->magic);
	a->proto = 1;
	a->fingerprint = fingerprint;
}

bool uds_link_host(uint32_t fingerprint)
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
	appdata_t a;
	make_appdata(&a, fingerprint);
	r = udsSetApplicationData(&a, sizeof a);
	if (R_FAILED(r)) {
		set_err("udsSetApplicationData", r);
		uds_link_close();
		return false;
	}
	state = UDS_LINK_HOSTING;
	return true;
}

bool uds_link_join(uint32_t fingerprint, bool *fingerprint_mismatch)
{
	if (fingerprint_mismatch)
		*fingerprint_mismatch = false;
	if (!inited && !uds_link_init())
		return false;
	uds_link_close();
	void *scanbuf = malloc(SCAN_BUF_SIZE);
	if (!scanbuf) {
		snprintf(err, sizeof err, "out of memory (scan buffer)");
		return false;
	}
	udsNetworkScanInfo *nets = NULL;
	size_t total = 0;
	Result r = udsScanBeacons(scanbuf, SCAN_BUF_SIZE, &nets, &total, WLAN_COMM_ID, NET_ID8, NULL, false);
	free(scanbuf);
	if (R_FAILED(r)) {
		set_err("udsScanBeacons", r);
		free(nets);
		return false;
	}
	bool ok = false;
	for (size_t i = 0; i < total && !ok; i++) {
		appdata_t a;
		size_t got = 0;
		memset(&a, 0, sizeof a);
		if (R_FAILED(udsGetNetworkStructApplicationData(&nets[i].network, &a, sizeof a, &got)) ||
		    got != sizeof a || memcmp(a.magic, APPDATA_MAGIC, sizeof a.magic) != 0)
			continue;
		if (a.fingerprint != fingerprint) {
			if (fingerprint_mismatch)
				*fingerprint_mismatch = true;
			continue;
		}
		if (nets[i].network.total_nodes >= nets[i].network.max_nodes)
			continue; /* full */
		r = udsConnectNetwork(&nets[i].network, passphrase, sizeof passphrase, &bindctx, UDS_BROADCAST_NETWORKNODEID,
		                      UDSCONTYPE_Client, DATA_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
		if (R_FAILED(r)) {
			set_err("udsConnectNetwork", r);
			continue;
		}
		ok = true;
	}
	free(nets);
	if (!ok) {
		if (!err[0])
			snprintf(err, sizeof err, "no host found");
		return false;
	}
	err[0] = 0;
	on_network = true;
	hosting = false;
	state = UDS_LINK_CONNECTED;
	return true;
}

uds_link_state uds_link_poll(void)
{
	if (!on_network)
		return state;
	udsConnectionStatus cs;
	if (R_FAILED(udsGetConnectionStatus(&cs)))
		return state;
	/* status 6 = host, 9 = client on 3dbrew; rely on node count instead */
	if (cs.total_nodes >= 2)
		state = UDS_LINK_CONNECTED;
	else if (state == UDS_LINK_CONNECTED)
		state = UDS_LINK_LOST;
	return state;
}

bool uds_link_send(const void *buf, size_t len)
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

int uds_link_recv(void *buf, size_t cap)
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
	state = UDS_LINK_OFF;
	err[0] = 0;
}
