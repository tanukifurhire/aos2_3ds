/* Host-side tests for aos_input. Build: see tests/run_tests.sh */
#include "../src/input/aos_input.h"
#include "../src/modes/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...)                                              \
	do {                                                              \
		checks++;                                                     \
		if (!(cond)) {                                                \
			failures++;                                               \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
			printf(__VA_ARGS__);                                      \
			printf("\n");                                             \
		}                                                             \
	} while (0)

#define L AOS_RAW_L
#define R AOS_RAW_R

/* Feed a sequence of raw frames, return OR of all events and the per-frame
 * event log in ev[] (may be NULL). */
static uint32_t run(aos_input *s, const uint32_t *frames, int n, uint32_t *ev, uint32_t *held)
{
	uint32_t all = 0;
	for (int i = 0; i < n; i++) {
		aos_input_out o = aos_input_update(s, frames[i]);
		if (ev) ev[i] = o.events;
		if (held) held[i] = o.held;
		all |= o.events;
	}
	return all;
}

static aos_input fresh(int w, int d)
{
	aos_input s;
	aos_input_cfg c = {(uint8_t)w, (uint8_t)d};
	aos_input_init(&s, &c);
	return s;
}

static void test_dash_resolves_after_window(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, L, L, L, L, 0}, ev[6], held[6];
	run(&s, f, 6, ev, held);
	CHECK(ev[0] == 0 && ev[1] == 0 && ev[2] == 0, "dash must wait for chord window");
	CHECK(ev[3] == AOS_EV_DASH, "dash on frame W (got %x)", ev[3]);
	CHECK(ev[4] == 0 && ev[5] == 0, "dash only once");
	CHECK(!(held[2] & AOS_HELD_DASH) && (held[3] & AOS_HELD_DASH) && (held[4] & AOS_HELD_DASH), "dash held after resolve");
	CHECK(!(held[5] & AOS_HELD_DASH), "dash held cleared on release");
}

static void test_quick_tap_resolves_on_release(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, 0, 0, 0}, ev[5];
	run(&s, f, 5, ev, NULL);
	CHECK(ev[0] == 0 && ev[1] == AOS_EV_DASH, "tap resolves on release frame (got %x %x)", ev[0], ev[1]);
	CHECK(ev[2] == 0 && ev[3] == 0, "no duplicate");
}

/* R alone = Shield, on the press frame (hypers are on the touch screen since 2026-11-10). */
static void test_r_shield(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {R, R, R, R, R, 0, R}, ev[7], held[7];
	run(&s, f, 7, ev, held);
	CHECK(ev[0] == AOS_EV_SHIELD && (ev[1] | ev[2] | ev[3] | ev[4] | ev[5]) == 0, "R: shield once, at once (got %x)", ev[0]);
	for (int i = 0; i < 5; i++)
		CHECK(held[i] == AOS_HELD_SHIELD, "R held -> shield held (frame %d got %x)", i, held[i]);
	CHECK(held[5] == 0, "R released");
	CHECK(ev[6] == AOS_EV_SHIELD, "R again -> shield again");
}

static void test_shield_same_frame(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L | R, L | R, L | R, L | R, L | R, 0}, ev[6], held[6];
	uint32_t all = run(&s, f, 6, ev, held);
	CHECK(ev[0] == AOS_EV_SHIELD, "same-frame shield immediate (got %x)", ev[0]);
	CHECK(all == AOS_EV_SHIELD, "no dash/hyper with shield (got %x)", all);
	CHECK(held[4] == AOS_HELD_SHIELD, "shield held only (got %x)", held[4]);
	CHECK(held[5] == 0, "shield released");
}

static void test_shield_staggered_within_window(void)
{
	for (int lead = 0; lead < 2; lead++)
		for (int gap = 0; gap <= 3; gap++) {
			aos_input s = fresh(3, 15);
			uint32_t first = lead ? R : L, f[10], ev[10];
			for (int i = 0; i < 10; i++) f[i] = i < gap ? first : (L | R);
			uint32_t held[10];
			uint32_t all = run(&s, f, 10, ev, held);
			CHECK(all == AOS_EV_SHIELD, "lead=%d gap=%d: expected only shield, got %x", lead, gap, all);
			/* L first: the shield comes with R; R first: at once */
			CHECK(ev[lead ? 0 : gap] == AOS_EV_SHIELD, "lead=%d gap=%d: shield on the R press frame", lead, gap);
			CHECK(held[9] == AOS_HELD_SHIELD, "lead=%d gap=%d: shield held, no dash (got %x)", lead, gap, held[9]);
		}
}

static void test_no_shield_after_window(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, L, L, L, L | R, L | R, L | R, L | R, L | R}, ev[9], held[9];
	uint32_t all = run(&s, f, 9, ev, held);
	CHECK(all == (AOS_EV_DASH | AOS_EV_SHIELD), "late R: dash, then shield (got %x)", all);
	CHECK(ev[3] == AOS_EV_DASH, "dash resolved");
	CHECK(ev[4] == AOS_EV_SHIELD, "late R shields at once (got %x)", ev[4]);
	CHECK(held[8] == (AOS_HELD_DASH | AOS_HELD_SHIELD), "both held separately (got %x)", held[8]);
}

static void test_released_before_partner_no_shield(void)
{
	/* L tapped and released the same frame R goes down: they never overlapped. */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, R, R, R, R, 0}, ev[6];
	uint32_t all = run(&s, f, 6, ev, NULL);
	CHECK(all == (AOS_EV_DASH | AOS_EV_SHIELD), "no overlap -> L dashes, R shields (got %x)", all);
	CHECK(ev[1] == (AOS_EV_DASH | AOS_EV_SHIELD), "dash and shield on the R frame (%x)", ev[1]);
}

static void test_shield_release_one_side(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L | R, L | R, L, L, L, L, 0}, ev[7], held[7];
	uint32_t all = run(&s, f, 7, ev, held);
	CHECK(all == AOS_EV_SHIELD, "releasing R must not trigger dash (got %x)", all);
	CHECK(held[3] == 0, "nothing held after shield broken (got %x)", held[3]);
}

static void test_hyper_dash(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, 0, L, L, L, L, 0}, ev[8], held[8];
	run(&s, f, 8, ev, held);
	CHECK(ev[1] == AOS_EV_DASH, "first tap dash");
	CHECK(ev[6] == AOS_EV_HYPER_DASH, "second press hyper dash (got %x)", ev[6]);
	CHECK((held[6] & AOS_HELD_HYPER_DASH) && (held[6] & AOS_HELD_DASH), "hyper dash held");
}

static void test_hyper_dash_window_edges(void)
{
	for (int gap = 13; gap <= 17; gap++) {
		aos_input s = fresh(3, 15);
		uint32_t f[40] = {0}, ev[40];
		f[0] = L;
		f[gap] = L;
		uint32_t all = run(&s, f, 40, ev, NULL);
		int want = gap <= 15;
		CHECK(!!(all & AOS_EV_HYPER_DASH) == want, "gap=%d hyper dash=%d expected %d", gap, !!(all & AOS_EV_HYPER_DASH), want);
		CHECK(!!(all & AOS_EV_DASH), "gap=%d first dash", gap);
	}
}

static void test_triple_tap(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, L, 0, L, 0, 0}, ev[7];
	run(&s, f, 7, ev, NULL);
	CHECK(ev[1] == AOS_EV_DASH && ev[3] == AOS_EV_HYPER_DASH && ev[5] == AOS_EV_DASH,
	      "triple tap = dash, hyper dash, dash (%x %x %x)", ev[1], ev[3], ev[5]);
}

static void test_shield_cancels_double_tap(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {L, 0, L | R, 0, L, 0}, ev[6];
	run(&s, f, 6, ev, NULL);
	CHECK(ev[1] == AOS_EV_DASH && ev[2] == AOS_EV_SHIELD, "dash then shield");
	CHECK(ev[5] == AOS_EV_DASH, "dash after shield is plain dash (got %x)", ev[5]);
}

static void test_zero_window(void)
{
	aos_input s = fresh(0, 15);
	uint32_t f[] = {L, L | R, 0, L | R}, ev[4];
	run(&s, f, 4, ev, NULL);
	CHECK(ev[0] == AOS_EV_DASH, "W=0 dash immediate");
	CHECK(ev[1] == AOS_EV_SHIELD, "W=0 staggered: R shields (got %x)", ev[1]);
	CHECK(ev[3] == AOS_EV_SHIELD, "W=0 same-frame shield (got %x)", ev[3]);
}

static void test_face_buttons(void)
{
	aos_input s = fresh(3, 15);
	uint32_t f[] = {AOS_RAW_A | AOS_RAW_X, AOS_RAW_A | AOS_RAW_X | AOS_RAW_B | AOS_RAW_Y, 0}, ev[3], held[3];
	run(&s, f, 3, ev, held);
	CHECK(ev[0] == (AOS_EV_WEAPON_A | AOS_EV_SPECIAL_A), "A+X immediate");
	CHECK(ev[1] == (AOS_EV_WEAPON_B | AOS_EV_SPECIAL_B), "B+Y edge only");
	CHECK(held[1] == (AOS_HELD_WEAPON_A | AOS_HELD_WEAPON_B | AOS_HELD_SPECIAL_A | AOS_HELD_SPECIAL_B), "all held");
	CHECK(held[2] == 0, "released");
}

static void test_l_after_r_joins_shield(void)
{
	/* R first, L within the window: one shield, L never dashes, even after R is let go */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {R, R, L | R, L | R, L, L, L, 0}, ev[8], held[8];
	uint32_t all = run(&s, f, 8, ev, held);
	CHECK(all == AOS_EV_SHIELD && ev[0] == AOS_EV_SHIELD, "one shield (got %x)", all);
	CHECK(held[3] == AOS_HELD_SHIELD && held[5] == 0, "shield while R held, nothing after (got %x %x)", held[3], held[5]);
	/* L long after R: a dash while shielding */
	s = fresh(3, 15);
	uint32_t g[] = {R, R, R, R, R, L | R, L | R, L | R, L | R, L | R}, ev2[10], held2[10];
	all = run(&s, g, 10, ev2, held2);
	CHECK(all == (AOS_EV_SHIELD | AOS_EV_DASH) && ev2[8] == AOS_EV_DASH, "late L dashes (got %x %x)", all, ev2[8]);
	CHECK(held2[9] == (AOS_HELD_SHIELD | AOS_HELD_DASH), "both held (got %x)", held2[9]);
}

static void test_long_hold_no_overflow(void)
{
	aos_input s = fresh(3, 15);
	aos_input_out o;
	int dashes = 0;
	for (int i = 0; i < 100000; i++) {
		o = aos_input_update(&s, L);
		if (o.events & AOS_EV_DASH) dashes++;
	}
	CHECK(dashes == 1 && (o.held & AOS_HELD_DASH), "long hold: one dash, still held");
}

/* Fuzz: random bumper input. Every R press shields on its press frame; shield is held exactly while R is;
 * every L press resolves (Dash / Hyper Dash event, or silently joins an L+R shield) within chord_window
 * frames; deterministic replay. */
static void test_fuzz(void)
{
	srand(12345);
	for (int w = 0; w <= 6; w++) {
		aos_input s = fresh(w, 15), s2 = fresh(w, 15);
		uint32_t prev = 0;
		int l_open = -1; /* frame of the current L press while unresolved */
		for (int i = 0; i < 2000000; i++) {
			uint32_t raw = prev;
			int p = rand() % 100;
			if (p < 12) raw ^= L;
			else if (p < 24) raw ^= R;
			else if (p < 26) raw ^= (L | R);
			aos_input_out o = aos_input_update(&s, raw);
			aos_input_out o2 = aos_input_update(&s2, raw);
			if (o.events != o2.events || o.held != o2.held) {
				CHECK(0, "nondeterministic at %d", i);
				return;
			}
			uint32_t rise = raw & ~prev;
			if (rise & L) { if (l_open >= 0 && s.l.pending) { CHECK(0, "w=%d L press %d never resolved", w, l_open); return; } l_open = i; }
			int sh = !!(o.events & AOS_EV_SHIELD);
			if (sh != !!(rise & R)) { CHECK(0, "w=%d shield event %d without/with R press at %d", w, sh, i); return; }
			if (!!(o.held & AOS_HELD_SHIELD) != !!(raw & R)) { CHECK(0, "w=%d shield held != R at %d", w, i); return; }
			int lres = !!(o.events & (AOS_EV_DASH | AOS_EV_HYPER_DASH));
			if ((o.events & AOS_EV_DASH) && (o.events & AOS_EV_HYPER_DASH)) { CHECK(0, "dash+hyperdash at %d", i); return; }
			if (lres) { if (l_open < 0) { CHECK(0, "w=%d spurious L event at %d", w, i); return; } l_open = -1; }
			if (l_open >= 0 && !s.l.pending && !lres) l_open = -1; /* joined a shield */
			if (l_open >= 0 && i - l_open >= w) { CHECK(0, "w=%d L latency exceeded at %d", w, i); return; }
			prev = raw;
		}
		CHECK(1, "fuzz w=%d", w);
	}
}

/* ---- rebinding (aos_binds) ---- */

static void test_binds_table(void)
{
	aos_binds b;
	aos_binds_default(&b);
	CHECK(aos_binds_valid(&b), "default valid");
	CHECK(b.btn[AOS_ACT_DASH] == AOS_BTN_L && b.btn[AOS_ACT_SHIELD] == AOS_BTN_R && b.btn[AOS_ACT_WEAPON_A] == AOS_BTN_A &&
	          b.btn[AOS_ACT_WEAPON_B] == AOS_BTN_B && b.btn[AOS_ACT_SPECIAL_A] == AOS_BTN_X &&
	          b.btn[AOS_ACT_SPECIAL_B] == AOS_BTN_Y,
	      "default = L Dash, R Shield, A/B weapons, X/Y specials");
	/* default map: physical button -> the logical bit of the same name */
	static const struct {
		int btn;
		uint32_t raw;
	} m[] = {{AOS_BTN_L, L},         {AOS_BTN_R, R},         {AOS_BTN_A, AOS_RAW_A}, {AOS_BTN_B, AOS_RAW_B},
	         {AOS_BTN_X, AOS_RAW_X}, {AOS_BTN_Y, AOS_RAW_Y}, {AOS_BTN_SELECT, 0}};
	for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
		CHECK(aos_binds_map(&b, AOS_PHYS(m[i].btn)) == m[i].raw, "default map %s", aos_btn_name(m[i].btn));
	CHECK(aos_binds_map(&b, 0x7f) == 0x3f, "all buttons");
	CHECK(aos_binds_current()->btn[AOS_ACT_DASH] == AOS_BTN_L, "current starts at the defaults");

	/* free button: just moves, the old one becomes free */
	CHECK(aos_binds_assign(&b, AOS_ACT_DASH, AOS_BTN_SELECT) == -1 && b.btn[AOS_ACT_DASH] == AOS_BTN_SELECT, "to free");
	CHECK(aos_binds_action(&b, AOS_BTN_L) == -1 && aos_binds_map(&b, AOS_PHYS(AOS_BTN_L)) == 0, "L now free");
	CHECK(aos_binds_map(&b, AOS_PHYS(AOS_BTN_SELECT)) == L, "SELECT = Dash");
	/* conflict: the two actions swap */
	CHECK(aos_binds_assign(&b, AOS_ACT_WEAPON_A, AOS_BTN_Y) == AOS_ACT_SPECIAL_B, "conflict reports the other action");
	CHECK(b.btn[AOS_ACT_WEAPON_A] == AOS_BTN_Y && b.btn[AOS_ACT_SPECIAL_B] == AOS_BTN_A && aos_binds_valid(&b), "swapped");
	CHECK(aos_binds_assign(&b, AOS_ACT_WEAPON_A, AOS_BTN_Y) == -1 && b.btn[AOS_ACT_WEAPON_A] == AOS_BTN_Y,
	      "same button: no-op");
	CHECK(aos_binds_assign(&b, AOS_ACT_COUNT, AOS_BTN_A) == -1 && aos_binds_assign(&b, 0, AOS_BTN_COUNT) == -1, "range");
	/* any sequence of assignments keeps a valid table */
	srand(7);
	for (int i = 0; i < 20000; i++) {
		aos_binds_assign(&b, rand() % AOS_ACT_COUNT, rand() % AOS_BTN_COUNT);
		if (!aos_binds_valid(&b)) {
			CHECK(0, "assign broke the table at %d", i);
			break;
		}
	}
	/* invalid tables are refused by set_current (defaults instead) */
	aos_binds bad = {{AOS_BTN_A, AOS_BTN_A, AOS_BTN_B, AOS_BTN_X, AOS_BTN_Y, AOS_BTN_L}};
	CHECK(!aos_binds_valid(&bad), "duplicate invalid");
	bad.btn[1] = 9;
	CHECK(!aos_binds_valid(&bad), "out of range invalid");
	aos_binds_set_current(&b);
	CHECK(!memcmp(aos_binds_current(), &b, sizeof b), "set current");
	aos_binds_set_current(&bad);
	aos_binds d;
	aos_binds_default(&d);
	CHECK(!memcmp(aos_binds_current(), &d, sizeof d), "invalid -> defaults");
}

/* the chord and the double tap follow the Dash / Shield buttons wherever they are bound */
static void test_binds_chord_double_tap(void)
{
	aos_binds b;
	aos_binds_default(&b);
	aos_binds_assign(&b, AOS_ACT_DASH, AOS_BTN_A);   /* Dash on A (Weapon A -> L) */
	aos_binds_assign(&b, AOS_ACT_SHIELD, AOS_BTN_B); /* Shield on B (Weapon B -> R) */
	CHECK(b.btn[AOS_ACT_WEAPON_A] == AOS_BTN_L && b.btn[AOS_ACT_WEAPON_B] == AOS_BTN_R, "weapons on the shoulders");
	const uint32_t pa = AOS_PHYS(AOS_BTN_A), pb = AOS_PHYS(AOS_BTN_B), pl = AOS_PHYS(AOS_BTN_L), pr = AOS_PHYS(AOS_BTN_R);
	/* A then A+B a frame later = one Shield, no Dash */
	aos_input s = fresh(3, 15);
	uint32_t f[] = {pa, pa | pb, pa | pb, pa | pb, pa | pb, 0}, ev[6], held[6];
	for (int i = 0; i < 6; i++)
		f[i] = aos_binds_map(&b, f[i]);
	uint32_t all = run(&s, f, 6, ev, held);
	CHECK(all == AOS_EV_SHIELD && ev[1] == AOS_EV_SHIELD && held[4] == AOS_HELD_SHIELD, "A+B = shield (got %x)", all);
	/* A, A = re-dash */
	s = fresh(3, 15);
	uint32_t g[] = {pa, 0, 0, pa, pa, pa, pa, 0}, ev2[8], held2[8];
	for (int i = 0; i < 8; i++)
		g[i] = aos_binds_map(&b, g[i]);
	run(&s, g, 8, ev2, held2);
	CHECK(ev2[1] == AOS_EV_DASH && ev2[6] == AOS_EV_HYPER_DASH && (held2[6] & AOS_HELD_HYPER_DASH), "A,A = hyper dash");
	/* L / R now fire the weapons at once, no chord */
	s = fresh(3, 15);
	uint32_t h[] = {pl | pr, 0}, ev3[2], held3[2];
	for (int i = 0; i < 2; i++)
		h[i] = aos_binds_map(&b, h[i]);
	run(&s, h, 2, ev3, held3);
	CHECK(ev3[0] == (AOS_EV_WEAPON_A | AOS_EV_WEAPON_B) && held3[0] == (AOS_HELD_WEAPON_A | AOS_HELD_WEAPON_B),
	      "L+R = both weapons (got %x)", ev3[0]);
	/* the same logical input through default vs remapped buttons gives identical events */
	aos_binds def;
	aos_binds_default(&def);
	aos_input s1 = fresh(3, 15), s2 = fresh(3, 15);
	srand(99);
	uint32_t p1 = 0;
	for (int i = 0; i < 200000; i++) {
		if (rand() % 3)
			p1 ^= 1u << (rand() % 6);
		uint32_t q = 0;
		for (int a = 0; a < AOS_ACT_COUNT; a++)
			if (aos_binds_map(&def, p1) & (1u << a))
				q |= AOS_PHYS(b.btn[a]);
		aos_input_out o1 = aos_input_update(&s1, aos_binds_map(&def, p1));
		aos_input_out o2 = aos_input_update(&s2, aos_binds_map(&b, q));
		if (o1.events != o2.events || o1.held != o2.held) {
			CHECK(0, "remapped stream differs at %d", i);
			return;
		}
	}
	CHECK(1, "remapped stream");
}

/* ---- the controls block of the progress save (save.h format 2) ---- */

static uint32_t crc32_ref(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
	}
	return ~c;
}

static void test_save_controls(void)
{
	static aos_save S, T;
	aos_save_defaults(&S);
	aos_binds d;
	aos_binds_default(&d);
	CHECK(!memcmp(S.binds, d.btn, sizeof S.binds), "save defaults = default binds");
	for (int c = 0; c < AOS_SAVE_CHARS; c++)
		CHECK(S.hyperLayout[c][0] == 0xff, "layout defaults");
	/* custom controls round trip */
	aos_binds b = d;
	aos_binds_assign(&b, AOS_ACT_DASH, AOS_BTN_X);
	aos_binds_assign(&b, AOS_ACT_SHIELD, AOS_BTN_SELECT);
	memcpy(S.binds, b.btn, sizeof S.binds);
	const uint8_t lay[5] = {1, 0, 3, 2, 4};
	memcpy(S.hyperLayout[10], lay, 5);
	S.wins = 77;
	static uint8_t buf[AOS_SAVE_FILE_BYTES];
	size_t n = aos_save_encode(&S, buf, sizeof buf);
	CHECK(n == AOS_SAVE_FILE_BYTES && buf[4] == 2, "format 2, %u bytes", (unsigned)n);
	CHECK(aos_save_decode(&T, buf, n) == 1 && !memcmp(T.binds, b.btn, sizeof T.binds) &&
	          !memcmp(T.hyperLayout[10], lay, 5) && T.hyperLayout[3][0] == 0xff && T.wins == 77,
	      "controls round trip");
	/* old (format 1) file: everything else loads, controls are the defaults */
	static uint8_t v1[AOS_SAVE_FILE_BYTES_V1];
	memcpy(v1, buf, sizeof v1);
	const uint32_t pl = AOS_SAVE_FILE_BYTES_V1 - 16, crc = crc32_ref(v1 + 16, pl);
	v1[4] = 1, v1[5] = 0;
	for (int i = 0; i < 4; i++) {
		v1[8 + i] = (uint8_t)(pl >> (8 * i));
		v1[12 + i] = (uint8_t)(crc >> (8 * i));
	}
	CHECK(aos_save_decode(&T, v1, sizeof v1) == 1 && T.wins == 77 && !memcmp(T.binds, d.btn, sizeof T.binds) &&
	          T.hyperLayout[10][0] == 0xff && !T.dirty,
	      "format 1 save loads with default controls");
	/* a damaged controls block (crc still fine): bad rows fall back one by one */
	S.binds[0] = S.binds[1];
	const uint8_t badlay[5] = {4, 0, 1, 2, 3}; /* centre moved: invalid */
	memcpy(S.hyperLayout[11], badlay, 5);
	n = aos_save_encode(&S, buf, sizeof buf);
	CHECK(aos_save_decode(&T, buf, n) == 1 && !memcmp(T.binds, d.btn, sizeof T.binds) && T.hyperLayout[11][0] == 0xff &&
	          !memcmp(T.hyperLayout[10], lay, 5),
	      "invalid binds / layout -> defaults");
	/* file round trip */
	memcpy(S.binds, b.btn, sizeof S.binds);
	CHECK(aos_save_write_file(&S, "build/tests", "build/tests/ctl_progress.sav") == 1, "write");
	CHECK(aos_save_load_file(&T, "build/tests/ctl_progress.sav") == 1 && !memcmp(T.binds, b.btn, sizeof T.binds), "load");
	remove("build/tests/ctl_progress.sav");
}

int main(void)
{
	test_dash_resolves_after_window();
	test_quick_tap_resolves_on_release();
	test_r_shield();
	test_shield_same_frame();
	test_shield_staggered_within_window();
	test_no_shield_after_window();
	test_released_before_partner_no_shield();
	test_shield_release_one_side();
	test_hyper_dash();
	test_hyper_dash_window_edges();
	test_triple_tap();
	test_shield_cancels_double_tap();
	test_zero_window();
	test_face_buttons();
	test_l_after_r_joins_shield();
	test_long_hold_no_overflow();
	test_fuzz();
	test_binds_table();
	test_binds_chord_double_tap();
	test_save_controls();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
