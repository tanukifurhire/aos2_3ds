/* Round/match flow timing (src/sim/flow.c, docs/re/hud.md §5): round-start
 * banner length and timer pause, KO -> next round after the FUN_00608a00
 * schedule, FINAL ROUND selection, time-over, match end after the 0x12 wipe. */
#include "../src/sim/sim.h"

#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...)                                                    \
	do {                                                                 \
		checks++;                                                        \
		if (!(c)) {                                                      \
			fails++;                                                     \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                  \
			printf(__VA_ARGS__);                                         \
			printf("\n");                                                \
		}                                                                \
	} while (0)

static aos_world W;
static const uint16_t none[2] = {0, 0};

/* Runs ticks until cond holds; returns the number of ticks (cap = fail). */
#define RUN_UNTIL(cond, cap)                     \
	({                                           \
		int n_ = 0;                              \
		while (!(cond) && n_ < (cap)) {          \
			aos_tick(&W, none);                  \
			n_++;                                \
		}                                        \
		n_;                                      \
	})

static int ko(int victim)
{
	/* KO lands between ticks (as from a hit in the previous tick's collisions);
	 * the KO state's t==0 runs in the next tick's player update. */
	aos_damage(&W, &W.p[victim], 100000, 0, 0);
	int idx = W.flow.roundIndex;
	return RUN_UNTIL(W.flow.roundIndex != idx || W.flow.matchEnd, 2000);
}

int main(void)
{
	aos_register_all();
	int32_t ids[2] = {10, 14}, sch[2] = {0, 0}, col[2] = {0, 0};
	aos_match_init(&W, ids, sch, col, 180, 2);

	/* round 1 start banner */
	CHECK(W.roundStartBanner == 1 && W.flow.stRound == 1, "banner %d round %d", W.roundStartBanner, W.flow.stRound);
	CHECK(W.flow.fadeOn && W.flow.fadeType == 0x13, "first round fades in with tiles (0x13), got %d", W.flow.fadeType);
	CHECK(W.roundTimer == 180, "timer %d", W.roundTimer);
	int n = RUN_UNTIL(!W.roundStartBanner, 1000);
	/* alpha +0.02 to 1 (frames 0..199), then -0.1 per frame from stT 200 */
	CHECK(n >= 209 && n <= 211, "start banner lasted %d ticks", n);
	CHECK(W.roundTimer == 180, "timer ran during the start banner (%d)", W.roundTimer);
	CHECK(aos_hud_visible(&W), "HUD not visible after the banner");
	int t0 = W.frame;
	RUN_UNTIL(W.roundTimer == 179, 200);
	CHECK(W.frame == ((t0 + 59) / 60) * 60, "first timer tick at frame %d (banner ended at %d)", W.frame, t0);

	/* KO: P2 down. Tick 1 runs the KO state (t==0: FUN_0060b310) and banner
	 * frame 0; tick 351 processes fb5c 350 (> 349) and sets DAT_00885878; Game
	 * Init runs at the start of tick 352. */
	n = ko(1);
	CHECK(n == 352, "KO -> next round took %d ticks (want 352)", n);
	CHECK(W.p[0].roundsWon == 1 && W.flow.stRound == 2, "won %d, banner round %d", W.p[0].roundsWon, W.flow.stRound);
	CHECK(W.flow.fadeType == 6 && W.roundTimer == 180 && !W.roundEndBanner, "fade %d timer %d", W.flow.fadeType,
	      W.roundTimer);

	/* 1-1 -> FINAL ROUND */
	RUN_UNTIL(!W.roundStartBanner, 1000);
	n = ko(0);
	CHECK(n == 352, "second KO -> next round took %d ticks", n);
	CHECK(W.flow.stRound == 99, "final round banner (99), got %d", W.flow.stRound);

	/* time over decides the match: P1 has more HP */
	RUN_UNTIL(!W.roundStartBanner, 1000);
	W.p[1].hp = 100;
	W.roundTimer = 1;
	n = RUN_UNTIL(W.roundOver, 200);
	CHECK(W.p[0].roundsWon == 2 && W.matchFinished == 1 && W.roundEndBanner && W.flow.enType == 1,
	      "time over: won %d finished %d banner %d type %d", W.p[0].roundsWon, W.matchFinished, W.roundEndBanner,
	      W.flow.enType);
	/* banner timer starts on the next tick; at enT 301 the 0x12 wipe starts, the
	 * banner goes away and the match ends once every tile is covered */
	n = RUN_UNTIL(!W.roundEndBanner, 1000);
	CHECK(n == 302 && W.flow.fadeType == 0x12, "end banner -> wipe after %d ticks (fade %d)", n, W.flow.fadeType);
	n = RUN_UNTIL(W.flow.matchEnd, 1000);
	/* last tile: 34 updates of delay, 101 steps of +0.01f (100 * 0.01f < 1.0f), one more
	 * update to see everything covered; the updates run at the start of each tick */
	CHECK(n == 136, "wipe took %d ticks", n);
	CHECK(aos_match_winner(&W) == 0, "winner %d", aos_match_winner(&W));
	aos_match_restart(&W);
	CHECK(W.p[0].roundsWon == 0 && W.flow.roundIndex == 0 && W.flow.stRound == 1 && W.flow.fadeType == 0x13,
	      "restart: won %d idx %d round %d fade %d", W.p[0].roundsWon, W.flow.roundIndex, W.flow.stRound,
	      W.flow.fadeType);

	/* one-round match shows the round-0 banner ("STAND BY") */
	aos_match_init(&W, ids, sch, col, 90, 1);
	CHECK(W.flow.stRound == 0 && W.roundTimer == 90, "one-round banner %d timer %d", W.flow.stRound, W.roundTimer);

	printf("%d checks, %d failures: %s\n", checks, fails, fails ? "FAIL" : "ok");
	return fails != 0;
}
