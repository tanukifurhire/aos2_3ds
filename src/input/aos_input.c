#include "aos_input.h"

#include <string.h>

/* ---- bindings (table-driven: action -> physical button) ---- */

static const uint8_t default_btn[AOS_ACT_COUNT] = {
	[AOS_ACT_DASH] = AOS_BTN_L,        [AOS_ACT_SHIELD] = AOS_BTN_R,     [AOS_ACT_WEAPON_A] = AOS_BTN_A,
	[AOS_ACT_WEAPON_B] = AOS_BTN_B,    [AOS_ACT_SPECIAL_A] = AOS_BTN_X,  [AOS_ACT_SPECIAL_B] = AOS_BTN_Y,
};

void aos_binds_default(aos_binds *b) { memcpy(b->btn, default_btn, sizeof b->btn); }

int aos_binds_valid(const aos_binds *b)
{
	uint32_t used = 0;
	for (int a = 0; a < AOS_ACT_COUNT; a++) {
		if (b->btn[a] >= AOS_BTN_COUNT || (used & AOS_PHYS(b->btn[a])))
			return 0;
		used |= AOS_PHYS(b->btn[a]);
	}
	return 1;
}

int aos_binds_action(const aos_binds *b, int btn)
{
	for (int a = 0; a < AOS_ACT_COUNT; a++)
		if (b->btn[a] == btn)
			return a;
	return -1;
}

int aos_binds_assign(aos_binds *b, int act, int btn)
{
	if (act < 0 || act >= AOS_ACT_COUNT || btn < 0 || btn >= AOS_BTN_COUNT || b->btn[act] == btn)
		return -1;
	int other = aos_binds_action(b, btn);
	if (other >= 0)
		b->btn[other] = b->btn[act]; /* conflict: the two actions trade buttons */
	b->btn[act] = (uint8_t)btn;
	return other;
}

uint32_t aos_binds_map(const aos_binds *b, uint32_t phys)
{
	uint32_t r = 0;
	for (int a = 0; a < AOS_ACT_COUNT; a++)
		if (phys & AOS_PHYS(b->btn[a]))
			r |= 1u << a;
	return r;
}

const char *aos_btn_name(int btn)
{
	static const char *const n[AOS_BTN_COUNT] = {"A", "B", "X", "Y", "L", "R", "SELECT"};
	return btn >= 0 && btn < AOS_BTN_COUNT ? n[btn] : "?";
}

static aos_binds cur = {{AOS_BTN_L, AOS_BTN_R, AOS_BTN_A, AOS_BTN_B, AOS_BTN_X, AOS_BTN_Y}};

const aos_binds *aos_binds_current(void) { return &cur; }

void aos_binds_set_current(const aos_binds *b)
{
	if (b && aos_binds_valid(b))
		cur = *b;
	else
		aos_binds_default(&cur);
}

#ifdef __3DS__
#include <3ds.h>
uint32_t aos_input_phys_from_3ds(uint32_t k)
{
	static const uint32_t key[AOS_BTN_COUNT] = {KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_SELECT};
	uint32_t r = 0;
	for (int b = 0; b < AOS_BTN_COUNT; b++)
		if (k & key[b])
			r |= AOS_PHYS(b);
	return r;
}
uint32_t aos_input_from_3ds(uint32_t k) { return aos_binds_map(&cur, aos_input_phys_from_3ds(k)); }
#endif

void aos_input_init(aos_input *s, const aos_input_cfg *cfg)
{
	memset(s, 0, sizeof *s);
	if (cfg) {
		s->cfg = *cfg;
	} else {
		s->cfg.chord_window = AOS_INPUT_DEFAULT_CHORD_WINDOW;
		s->cfg.double_tap_window = AOS_INPUT_DEFAULT_DOUBLE_TAP_WINDOW;
	}
	s->frames_since_l_press = 0xff;
}

static void press(aos_bumper *b)
{
	b->pending = 1;
	b->age = 0;
	b->resolution = AOS_RES_NONE;
}

static uint32_t resolve_l(aos_input *s)
{
	s->l.pending = 0;
	if (s->double_tap_armed && s->l_press_gap <= s->cfg.double_tap_window) {
		s->double_tap_armed = 0; /* a third tap starts a fresh pair */
		s->l.resolution = AOS_RES_HYPER_DASH;
		return AOS_EV_HYPER_DASH;
	}
	s->double_tap_armed = 1;
	s->l.resolution = AOS_RES_DASH;
	return AOS_EV_DASH;
}

aos_input_out aos_input_update(aos_input *s, uint32_t raw)
{
	aos_input_out o = {0, 0};
	uint32_t rise = raw & ~s->prev_raw;
	s->prev_raw = raw;

	s->l.held = (raw & AOS_RAW_L) != 0;
	s->r.held = (raw & AOS_RAW_R) != 0;

	/* Age the presses made before this frame. */
	if (s->l.age < 0xff) s->l.age++;
	if (s->r.age < 0xff) s->r.age++;
	if (s->frames_since_l_press < 0xff) s->frames_since_l_press++;

	if (rise & AOS_RAW_L) {
		press(&s->l);
		s->l_press_gap = s->frames_since_l_press;
		s->frames_since_l_press = 0;
	}
	/* R = Shield, resolved on the press frame. A pending L press (still held) joins it: L+R is one Shield, no
	 * Dash. Pending presses are never older than chord_window, so the two presses are within it. */
	if (rise & AOS_RAW_R) {
		s->r.pending = 0;
		s->r.age = 0;
		s->r.resolution = AOS_RES_SHIELD;
		o.events |= AOS_EV_SHIELD;
		if (s->l.pending && s->l.held) {
			s->l.pending = 0;
			s->l.resolution = AOS_RES_SHIELD;
			s->double_tap_armed = 0;
		}
	}
	/* L pressed shortly after R (R still held from that press): part of the same L+R Shield. */
	if ((rise & AOS_RAW_L) && s->r.held && s->r.resolution == AOS_RES_SHIELD && s->r.age <= s->cfg.chord_window &&
	    !(rise & AOS_RAW_R)) {
		s->l.pending = 0;
		s->l.resolution = AOS_RES_SHIELD;
		s->double_tap_armed = 0;
	}

	/* A lone L press resolves when the window closes or the button is let go. */
	if (s->l.pending && (!s->l.held || s->l.age >= s->cfg.chord_window))
		o.events |= resolve_l(s);

	/* Held states. A press keeps its resolution until released: letting go of R never turns the L of an L+R
	 * Shield into a Dash. Shield is held while R is (alone or with L). */
	if (s->r.held && s->r.resolution == AOS_RES_SHIELD)
		o.held |= AOS_HELD_SHIELD;
	if (s->l.held && s->l.resolution == AOS_RES_DASH)
		o.held |= AOS_HELD_DASH;
	if (s->l.held && s->l.resolution == AOS_RES_HYPER_DASH)
		o.held |= AOS_HELD_DASH | AOS_HELD_HYPER_DASH;
	if (!s->l.held && !s->l.pending) s->l.resolution = AOS_RES_NONE;
	if (!s->r.held) s->r.resolution = AOS_RES_NONE;

	/* Face buttons: no ambiguity, no delay. */
	if (rise & AOS_RAW_A) o.events |= AOS_EV_WEAPON_A;
	if (rise & AOS_RAW_B) o.events |= AOS_EV_WEAPON_B;
	if (rise & AOS_RAW_X) o.events |= AOS_EV_SPECIAL_A;
	if (rise & AOS_RAW_Y) o.events |= AOS_EV_SPECIAL_B;
	if (raw & AOS_RAW_A) o.held |= AOS_HELD_WEAPON_A;
	if (raw & AOS_RAW_B) o.held |= AOS_HELD_WEAPON_B;
	if (raw & AOS_RAW_X) o.held |= AOS_HELD_SPECIAL_A;
	if (raw & AOS_RAW_Y) o.held |= AOS_HELD_SPECIAL_B;
	return o;
}
