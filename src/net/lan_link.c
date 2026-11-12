/* LAN transport over soc (UDP broadcast discovery + UDP session), see lan_link.h. */
#include "lan_link.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "rollback.h"

#define SOC_ALIGN 0x1000
#define SOC_BUFSIZE 0x40000 /* 256 KB handed to the soc service */
#define ANNOUNCE_MS 500
#define PING_MS 250
#define TIMEOUT_MS 5000

static u32 *soc_buf;
static bool soc_up;
static int game_fd = -1, disc_fd = -1;
static bool hosting, have_peer, peer_heard;
static struct sockaddr_in peer;
static link_state state = LINK_OFF;
static lobby_entry my_info;
static uint32_t last_announce, last_ping, last_recv;
static char err[96];
static uint32_t send_errors;

static uint32_t now(void) { return (uint32_t)osGetTime(); }
const char *lan_link_error(void) { return err; }
static uint32_t lan_send_errors(void) { return send_errors; }
bool lan_link_ready(void) { return soc_up; }

uint32_t lan_link_my_ip(void)
{
	if (!soc_up)
		return 0;
	long id = gethostid();
	return (uint32_t)id;
}

static bool ip_valid(uint32_t ip_net)
{
	uint32_t h = ntohl(ip_net);
	return h != 0 && h != 0xffffffffu && (h >> 24) != 127;
}

lan_init_result lan_link_init(void)
{
	if (!soc_up) {
		if (!soc_buf)
			soc_buf = (u32 *)memalign(SOC_ALIGN, SOC_BUFSIZE);
		if (!soc_buf) {
			snprintf(err, sizeof err, "Out of memory for the network buffer.");
			return LAN_ERR_SOC;
		}
		Result r = socInit(soc_buf, SOC_BUFSIZE);
		if (R_FAILED(r)) {
			snprintf(err, sizeof err, "socInit failed (0x%08lx).", (unsigned long)r);
			free(soc_buf);
			soc_buf = NULL;
			return LAN_ERR_SOC;
		}
		soc_up = true;
	}
	/* An IP address = associated with an access point (ac's status is also asked: on hardware
	 * gethostid can keep an old address briefly after the connection drops). */
	bool ac_ok = false, connected_ac = false;
	if (R_SUCCEEDED(acInit())) {
		u32 ws = 0;
		ac_ok = R_SUCCEEDED(ACU_GetWifiStatus(&ws));
		connected_ac = ws != 0;
		acExit();
	}
	if (!ip_valid(lan_link_my_ip()) || (ac_ok && !connected_ac)) {
		snprintf(err, sizeof err, "Not connected to a Wi-Fi network.");
		return LAN_ERR_NOWIFI;
	}
	err[0] = 0;
	return LAN_OK;
}

static void close_fd(int *fd)
{
	if (*fd >= 0)
		close(*fd);
	*fd = -1;
}

void lan_link_close(void)
{
	close_fd(&game_fd);
	hosting = have_peer = peer_heard = false;
	state = LINK_OFF;
	err[0] = 0;
}

void lan_browse_end(void) { close_fd(&disc_fd); }

void lan_link_exit(void)
{
	lan_link_close();
	lan_browse_end();
	if (soc_up)
		socExit();
	soc_up = false;
	free(soc_buf);
	soc_buf = NULL;
}

static int udp_socket(uint16_t port, bool broadcast)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	if (broadcast && SO_BROADCAST != 0) /* libctru: 0 = not required */
		setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
	struct sockaddr_in a;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	return fd;
}

static void announce(void)
{
	uint8_t b[LOBBY_WIRE_SIZE];
	size_t k = lobby_encode(&my_info, b, sizeof b);
	struct sockaddr_in to;
	memset(&to, 0, sizeof to);
	to.sin_family = AF_INET;
	to.sin_port = htons(LOBBY_LAN_DISC_PORT);
	to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	sendto(game_fd, b, k, 0, (struct sockaddr *)&to, sizeof to);
	/* also the subnet broadcast (some routers / stacks drop 255.255.255.255) */
	uint32_t ip = ntohl(lan_link_my_ip());
	if (ip) {
		to.sin_addr.s_addr = htonl(ip | 0xff);
		sendto(game_fd, b, k, 0, (struct sockaddr *)&to, sizeof to);
	}
#ifdef AOS_PROFILE
	/* emulator tests: two instances on one PC also hear each other over loopback */
	to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sendto(game_fd, b, k, 0, (struct sockaddr *)&to, sizeof to);
#endif
	last_announce = now();
}

bool lan_link_host(const lobby_entry *info)
{
	if (!soc_up && lan_link_init() == LAN_ERR_SOC)
		return false;
	lan_link_close();
	for (int i = 0; i < 4 && game_fd < 0; i++) {
		game_fd = udp_socket((uint16_t)(LOBBY_LAN_GAME_PORT + i), true);
		my_info = *info;
		my_info.port = (uint16_t)(LOBBY_LAN_GAME_PORT + i);
	}
	if (game_fd < 0) {
		snprintf(err, sizeof err, "Could not open a UDP port (errno %d).", errno);
		return false;
	}
	my_info.players = 1;
	my_info.max_players = 2;
	my_info.status = LOBBY_OPEN;
	hosting = true;
	state = LINK_HOSTING;
	announce();
	return true;
}

bool lan_link_connect(uint32_t ip, uint16_t port)
{
	if (!soc_up && lan_link_init() == LAN_ERR_SOC)
		return false;
	lan_link_close();
	game_fd = udp_socket(0, false);
	if (game_fd < 0) {
		snprintf(err, sizeof err, "Could not open a UDP socket (errno %d).", errno);
		return false;
	}
	memset(&peer, 0, sizeof peer);
	peer.sin_family = AF_INET;
	peer.sin_port = htons(port);
	peer.sin_addr.s_addr = ip;
	have_peer = true;
	peer_heard = false;
	last_recv = last_ping = now();
	state = LINK_CONNECTED;
	return true;
}

static void lan_set_status(uint8_t status, uint8_t players)
{
	if (!hosting || (my_info.status == status && my_info.players == players))
		return;
	my_info.status = status;
	my_info.players = players;
	announce();
}

static bool raw_send(const void *buf, size_t len)
{
	if (game_fd < 0 || !have_peer)
		return false;
	if (sendto(game_fd, buf, len, 0, (struct sockaddr *)&peer, sizeof peer) < 0) {
		send_errors++;
		return false;
	}
	return true;
}

static link_state lan_poll(void)
{
	if (game_fd < 0)
		return state;
	uint32_t t = now();
	static uint32_t last_poll;
	if (t - last_poll > 1000) /* not polled for a while (match loading): don't count that as silence */
		last_recv = t;
	last_poll = t;
	if (hosting && t - last_announce >= ANNOUNCE_MS)
		announce();
	if (have_peer && t - last_ping >= PING_MS) {
		uint8_t p = LINK_PKT_PING;
		raw_send(&p, 1);
		last_ping = t;
	}
	if (state == LINK_CONNECTED && t - last_recv > TIMEOUT_MS) {
		snprintf(err, sizeof err, peer_heard ? "No data from the other console." : "No response received.");
		state = LINK_LOST;
	}
	return state;
}

static bool lan_send(const void *buf, size_t len)
{
	if (len == 0 || len > 1400)
		return false;
	return raw_send(buf, len);
}

static bool same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
	return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int lan_recv(void *buf, size_t cap)
{
	if (game_fd < 0)
		return -1;
	for (;;) {
		struct sockaddr_in from;
		socklen_t fl = sizeof from;
		int n = (int)recvfrom(game_fd, buf, cap, 0, (struct sockaddr *)&from, &fl);
		if (n <= 0)
			return 0; /* EAGAIN, or an ICMP "port unreachable" bounced back: nothing to read */
		const uint8_t *p = buf;
		if (!have_peer) {
			if (!hosting || p[0] != RB_PKT_HELLO)
				continue;
			peer = from; /* first HELLO: this is our opponent */
			have_peer = true;
			state = LINK_CONNECTED;
		} else if (!same_addr(&from, &peer)) {
			if (hosting && p[0] == RB_PKT_HELLO) { /* someone else: the lobby is full */
				uint8_t q[2] = {RB_PKT_QUIT, RB_PROTO_VERSION};
				sendto(game_fd, q, sizeof q, 0, (struct sockaddr *)&from, sizeof from);
			}
			continue;
		}
		last_recv = now();
		peer_heard = true;
		if (n == 1 && p[0] == LINK_PKT_PING)
			continue;
		return n;
	}
}

bool lan_browse_begin(void)
{
	if (disc_fd >= 0)
		return true;
	if (!soc_up && lan_link_init() == LAN_ERR_SOC)
		return false;
	disc_fd = udp_socket(LOBBY_LAN_DISC_PORT, true);
	if (disc_fd < 0) {
		snprintf(err, sizeof err, "Could not listen on UDP port %d (errno %d).", LOBBY_LAN_DISC_PORT, errno);
		return false;
	}
	return true;
}

int lan_browse_poll(lobby_entry *out, int cap, uint32_t now_ms)
{
	int k = 0;
	if (disc_fd < 0)
		return 0;
	uint8_t b[128];
	while (k < cap) {
		struct sockaddr_in from;
		socklen_t fl = sizeof from;
		int n = (int)recvfrom(disc_fd, b, sizeof b, 0, (struct sockaddr *)&from, &fl);
		if (n <= 0)
			break;
		lobby_entry e;
		if (!lobby_decode(b, (size_t)n, &e))
			continue;
		e.kind = LOBBY_LAN;
		e.host = from.sin_addr.s_addr;
		e.last_ms = now_ms;
		/* one host heard over several paths (broadcast + subnet): lobby_list merges by host */
		out[k++] = e;
	}
	return k;
}

const net_link lan_link_ops = {
	.name = "LAN",
	.poll = lan_poll,
	.send = lan_send,
	.recv = lan_recv,
	.close = lan_link_close,
	.error = lan_link_error,
	.send_errors = lan_send_errors,
	.set_status = lan_set_status,
};
