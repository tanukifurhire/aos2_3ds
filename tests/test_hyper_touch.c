/* Touch hyper buttons (src/input/hyper_touch.c): every slot of every character, both control schemes, fires
 * exactly the hyper the touch stands for through the real input dispatch (aos_tick), and the synthesised
 * masks never fake a button press. Build: see tests/run_tests.sh. */
#include "../src/input/hyper_touch.h"
#include "../src/sim/sim.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static aos_world W;
static const char *slot_name[HT_NSLOT] = {"UP S+H", "LEFT H", "RIGHT B+H", "DOWN D+H", "CENTER S+B+H"};

static void setup(int chara, int scheme)
{
	int32_t ids[2] = {chara, AOS_CH_SUGURI}, sch[2] = {scheme, 0}, col[2] = {0, 1};
	aos_match_init(&W, ids, sch, col, 999, 2);
	uint16_t m[2] = {0, 0};
	for (int i = 0; i < 210; i++) /* inputs are locked for frame < 200 */
		aos_tick(&W, m);
	W.p[0].x = -300;
	W.p[0].y = 0;
	W.p[1].x = 300;
	W.p[1].y = 0;
}

/* Hold `slot` from a neutral start with a full gauge; returns the state after the first frame that spent
 * gauge (or -1). *spent = levels spent, *frame = that frame. `phys` = physical mask held throughout. */
static int fire(int chara, int scheme, int slot, uint16_t phys, float *spent, int *frame, int *barrier)
{
	setup(chara, scheme);
	W.p[0].hyper = 3.0f;
	ht_state s;
	ht_init(&s);
	for (int f = 0; f < 4; f++) {
		float before = W.p[0].hyper;
		uint16_t m[2] = {ht_apply(&s, chara, scheme, slot, phys), 0};
		aos_tick(&W, m);
		if (W.p[0].hyper < before - 0.5f) {
			*spent = before - W.p[0].hyper;
			*frame = f;
			*barrier = W.p[0].barrierType;
			return W.p[0].state;
		}
	}
	return -1;
}

static void test_all_slots(void)
{
	for (int c = AOS_CH_SORA; c < AOS_NUM_CHARS; c++) {
		for (int sch = 0; sch < 2; sch++) {
			int st[HT_NSLOT];
			for (int k = 0; k < HT_NSLOT; k++) {
				const ht_move *mv = ht_slot(c, k);
				st[k] = -2;
				if (!mv->key)
					continue;
				float spent = 0;
				int frame = -1, barrier = 0;
				st[k] = fire(c, sch, k, 0, &spent, &frame, &barrier);
				CHECK(st[k] > 6, "char %d scheme %d %s: no hyper started (state %d)", c, sch, slot_name[k], st[k]);
				CHECK(fabsf(spent - mv->cost) < 1e-4f, "char %d scheme %d %s: spent %.2f, cost %.2f", c, sch, slot_name[k],
				      spent, mv->cost);
				CHECK(frame == 0, "char %d scheme %d %s: fired on frame %d, want the touch frame", c, sch, slot_name[k],
				      frame);
				if (k == HT_CENTER)
					CHECK(barrier == 4, "char %d scheme %d: Accel barrier kind 4 (FUN_006b1b40 0x14), got %d", c, sch,
					      barrier);
			}
			for (int a = 0; a < HT_NSLOT; a++)
				for (int b = a + 1; b < HT_NSLOT; b++)
					if (st[a] > 6 && st[b] > 6)
						CHECK(st[a] != st[b], "char %d scheme %d: %s and %s both give state %#x", c, sch, slot_name[a],
						      slot_name[b], st[a]);
		}
	}
}

/* Physical buttons held while touching never change which hyper comes out. */
static void test_physical_held(void)
{
	static const uint16_t helds[] = {AOS_IN_A, AOS_IN_B, AOS_IN_S | AOS_IN_A, AOS_IN_S | AOS_IN_B, AOS_IN_D,
	                                 AOS_IN_D | AOS_IN_B};
	for (int c = AOS_CH_SORA; c < AOS_NUM_CHARS; c++)
		for (int sch = 0; sch < 2; sch++)
			for (int k = 0; k < HT_NSLOT; k++) {
				if (!ht_slot(c, k)->key)
					continue;
				float sp;
				int fr, ba;
				int clean = fire(c, sch, k, 0, &sp, &fr, &ba);
				for (unsigned h = 0; h < sizeof helds / sizeof helds[0]; h++) {
					uint16_t ph = helds[h];
					/* Iru's D+S+H is the dash version of S+H (iru.md §2): a held L is meant to pick it */
					if (c == AOS_CH_IRU && k == HT_UP && (ph & AOS_IN_D) && sch == 0)
						continue;
					int st = fire(c, sch, k, ph, &sp, &fr, &ba);
					CHECK(st == clean, "char %d scheme %d %s with %#x held: state %#x, without %#x", c, sch, slot_name[k],
					      ph, st, clean);
					CHECK(fabsf(sp - ht_slot(c, k)->cost) < 1e-4f, "char %d scheme %d %s with %#x held: spent %.2f", c,
					      sch, slot_name[k], ph, sp);
				}
			}
	/* Iru: L held + S+H button = Wide-Area Electric Field, dash version 0x17f */
	float sp;
	int fr, ba;
	CHECK(fire(AOS_CH_IRU, 0, HT_UP, AOS_IN_D, &sp, &fr, &ba) == 0x17f, "Iru D+S+H -> 0x17f");
	CHECK(fire(AOS_CH_IRU, 0, HT_UP, 0, &sp, &fr, &ba) == 0x17e, "Iru S+H -> 0x17e");
}

/* Mask level: suppressed buttons stay off until physically released (no fake press edge). */
static void test_mask_edges(void)
{
	ht_state s;
	ht_init(&s);
	/* B held physically, S+H touched (B cleared), touch released while B still held: B stays off */
	uint16_t m = ht_apply(&s, AOS_CH_SUGURI, 0, HT_UP, AOS_IN_B);
	CHECK(m == (AOS_IN_S | AOS_IN_H), "S+H with B held: %#x", m);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, -1, AOS_IN_B);
	CHECK(m == 0, "B held back after the touch: %#x", m);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, -1, AOS_IN_B | AOS_IN_A);
	CHECK(m == AOS_IN_A, "B still held back, A passes: %#x", m);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, -1, 0);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, -1, AOS_IN_B);
	CHECK(m == AOS_IN_B, "B back after a release: %#x", m);
	/* Suguri has no D+H: a held L survives the H button (dash keeps going) */
	ht_init(&s);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, HT_LEFT, AOS_IN_D);
	CHECK(m == (AOS_IN_D | AOS_IN_H), "Suguri H with L held: %#x", m);
	ht_init(&s);
	m = ht_apply(&s, AOS_CH_TSIH, 0, HT_LEFT, AOS_IN_D);
	CHECK(m == AOS_IN_H, "Tsih H with L held: L cleared (else D+H): %#x", m);
	/* advanced scheme: an already held A is let up for one frame, then pressed with H held */
	ht_init(&s);
	ht_apply(&s, AOS_CH_SUGURI, 1, -1, AOS_IN_A);
	m = ht_apply(&s, AOS_CH_SUGURI, 1, HT_LEFT, AOS_IN_A);
	CHECK(m == AOS_IN_H, "advanced H with A held, frame 0: %#x", m);
	m = ht_apply(&s, AOS_CH_SUGURI, 1, HT_LEFT, AOS_IN_A);
	CHECK(m == (AOS_IN_H | AOS_IN_A), "advanced frame 1: %#x", m);
	m = ht_apply(&s, AOS_CH_SUGURI, 1, -1, AOS_IN_A);
	CHECK(m == AOS_IN_A, "advanced release: A as held: %#x", m);
	/* Iru, advanced: a held X (S) is kept off for the H button (row 3 needs no edge) */
	ht_init(&s);
	ht_apply(&s, AOS_CH_IRU, 1, -1, AOS_IN_S | AOS_IN_A);
	m = ht_apply(&s, AOS_CH_IRU, 1, HT_RIGHT, AOS_IN_S | AOS_IN_A);
	CHECK(m == (AOS_IN_H | AOS_IN_B | AOS_IN_A), "Iru advanced B+H with X held: %#x", m);
	/* empty slots do nothing */
	ht_init(&s);
	m = ht_apply(&s, AOS_CH_SUGURI, 0, HT_DOWN, AOS_IN_A);
	CHECK(m == AOS_IN_A, "Suguri has no D+H slot: %#x", m);
	m = ht_apply(&s, AOS_CH_SUMIKA2, 0, HT_CENTER, 0);
	CHECK(m == 0, "Sumika 2nd has no Accel slot: %#x", m);
}

/* Gauge / permission gate (ht_ready) */
static void test_ready(void)
{
	setup(AOS_CH_SUGURI, 0);
	W.p[0].hyper = 2.5f;
	CHECK(ht_ready(&W, 0, HT_UP) && !ht_ready(&W, 0, HT_CENTER) && !ht_ready(&W, 0, HT_DOWN), "2.5 levels");
	W.p[0].hyper = 3.0f;
	CHECK(ht_ready(&W, 0, HT_CENTER), "3 levels: Accel");
	W.p[1].barrierType = 4;
	W.p[1].barrierTime = 10;
	CHECK(!ht_ready(&W, 0, HT_LEFT), "opponent in Accel: no hyper (FUN_00780950)");
}

static void test_layout(void)
{
	/* left/right (thumb) quadrants filled first: Suguri/Sora keep the gap at the bottom; the map is its own inverse */
	for (int k = 0; k < HT_NSLOT; k++)
		CHECK(ht_pos_slot(AOS_CH_SUGURI, k) == k && ht_pos_slot(AOS_CH_SORA, k) == k, "gap stays at the bottom");
	CHECK(!ht_slot(AOS_CH_SUGURI, HT_DOWN)->key && !ht_slot(AOS_CH_SORA, HT_DOWN)->key, "bottom empty");
	for (int c = AOS_CH_SORA; c < AOS_NUM_CHARS; c++)
		CHECK(ht_slot(c, ht_pos_slot(c, HT_LEFT))->key && ht_slot(c, ht_pos_slot(c, HT_RIGHT))->key, "thumbs filled");
	CHECK(ht_pos_slot(AOS_CH_NATH, HT_DOWN) == HT_DOWN && ht_pos_slot(AOS_CH_NATH, HT_UP) == HT_UP, "nath as is");
	for (int c = AOS_CH_SORA; c < AOS_NUM_CHARS; c++)
		for (int k = 0; k < HT_NSLOT; k++)
			CHECK(ht_pos_slot(c, ht_pos_slot(c, k)) == k, "involution");
}

int main(void)
{
	aos_register_all();
	test_layout();
	test_all_slots();
	test_physical_held();
	test_mask_edges();
	test_ready();
	printf("hyper touch: %d checks, %d failures\n", checks, fails);
	return fails != 0;
}
