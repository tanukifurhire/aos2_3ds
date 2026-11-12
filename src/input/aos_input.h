/*
 * aos_input — translates raw 3DS buttons into AoS2 actions.
 *
 * Mapping (user-specified, revised 2026-11-10: hypers moved to the touch
 * screen, src/input/hyper_touch.h):
 *   L  = Dash              R  = Shield
 *   A  = Weapon A          B  = Weapon B
 *   X  = Special A         Y  = Special B
 *   L+R pressed together   = Shield (as before)
 *   L, L in quick succession = Hyper Dash
 *
 * R resolves to Shield at once. A lone L press is held "pending" for up to
 * `chord_window` frames to see whether R arrives (then the pair is one Shield
 * and no Dash comes out); an L press shortly after R (within `chord_window`)
 * joins that Shield too. That adds up to `chord_window` frames of latency to
 * Dash only (0 disables the wait: only same-frame presses then form a pair).
 *
 * The module is a pure function of (state, raw buttons) with no clock, I/O or
 * allocation, so it is deterministic and safe to re-simulate for rollback.
 *
 * Rebinding (port, 2026-11-13): the AOS_RAW_* bits are *logical* buttons (the
 * Dash button, the Shield button, ...). A binding table (aos_binds) maps each
 * action to one physical button (A B X Y L R SELECT; the Old 3DS has no ZL/ZR/
 * C-stick, Circle Pad / D-pad stay movement, START stays Pause) and
 * aos_binds_map turns physical bits into logical ones. So the L+R chord is
 * "Dash button + Shield button" and the double tap is "Dash button twice"
 * wherever they are bound. Netplay sends the resulting game mask, so rebinding
 * is local and rollback-safe.
 */
#ifndef AOS_INPUT_H
#define AOS_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logical button bits fed to aos_input_update (named after the default buttons; aos_binds_map makes them). */
enum {
	AOS_RAW_L = 1u << 0, /* the Dash button */
	AOS_RAW_R = 1u << 1, /* the Shield button */
	AOS_RAW_A = 1u << 2, /* Weapon A */
	AOS_RAW_B = 1u << 3, /* Weapon B */
	AOS_RAW_X = 1u << 4, /* Special A */
	AOS_RAW_Y = 1u << 5, /* Special B */
};

/* Actions that can be rebound; AOS_RAW_* bit of action a = 1 << a. */
enum {
	AOS_ACT_DASH,
	AOS_ACT_SHIELD,
	AOS_ACT_WEAPON_A,
	AOS_ACT_WEAPON_B,
	AOS_ACT_SPECIAL_A,
	AOS_ACT_SPECIAL_B,
	AOS_ACT_COUNT
};
/* Physical buttons an action can sit on (Old 3DS); AOS_PHYS bit of button b = 1 << b. */
enum { AOS_BTN_A, AOS_BTN_B, AOS_BTN_X, AOS_BTN_Y, AOS_BTN_L, AOS_BTN_R, AOS_BTN_SELECT, AOS_BTN_COUNT };
#define AOS_PHYS(b) (1u << (b))

typedef struct {
	uint8_t btn[AOS_ACT_COUNT]; /* AOS_BTN_* per action, all different */
} aos_binds;

void aos_binds_default(aos_binds *b); /* L Dash, R Shield, A/B weapons, X/Y specials */
/* Every action on a valid button, no button used twice. */
int aos_binds_valid(const aos_binds *b);
/* Put action act on button btn. If another action had btn it takes act's old button (a swap); returns that
 * action, or -1 if btn was free / nothing changed. */
int aos_binds_assign(aos_binds *b, int act, int btn);
/* Action bound to button btn, -1 none. */
int aos_binds_action(const aos_binds *b, int btn);
/* Physical bits (AOS_PHYS) -> logical bits (AOS_RAW_*). */
uint32_t aos_binds_map(const aos_binds *b, uint32_t phys);
/* Short button name ("A", "L", "SELECT"). */
const char *aos_btn_name(int btn);

/* The bindings in use (aos_input_from_3ds; default until the save is applied). Set copies after validating
 * (an invalid table = defaults). */
const aos_binds *aos_binds_current(void);
void aos_binds_set_current(const aos_binds *b);

/* One-shot events (edge triggered, emitted on the frame they resolve). */
enum {
	AOS_EV_DASH       = 1u << 0,
	AOS_EV_HYPER_DASH = 1u << 1,
	/* 1u << 2 was AOS_EV_HYPER (R = Hyper until 2026-11-10) */
	AOS_EV_SHIELD     = 1u << 3,
	AOS_EV_WEAPON_A   = 1u << 4,
	AOS_EV_WEAPON_B   = 1u << 5,
	AOS_EV_SPECIAL_A  = 1u << 6,
	AOS_EV_SPECIAL_B  = 1u << 7,
};

/* Held states (true every frame the action is being held). */
enum {
	AOS_HELD_DASH       = 1u << 0, /* also set while a Hyper Dash is held */
	AOS_HELD_HYPER_DASH = 1u << 1,
	/* 1u << 2 was AOS_HELD_HYPER */
	AOS_HELD_SHIELD     = 1u << 3,
	AOS_HELD_WEAPON_A   = 1u << 4,
	AOS_HELD_WEAPON_B   = 1u << 5,
	AOS_HELD_SPECIAL_A  = 1u << 6,
	AOS_HELD_SPECIAL_B  = 1u << 7,
};

typedef struct {
	uint8_t chord_window;      /* max frames between L and R presses for one L+R Shield */
	uint8_t double_tap_window; /* max frames between two L presses for Hyper Dash */
} aos_input_cfg;

#define AOS_INPUT_DEFAULT_CHORD_WINDOW 3
#define AOS_INPUT_DEFAULT_DOUBLE_TAP_WINDOW 15

typedef enum {
	AOS_RES_NONE = 0,   /* not pressed, or pressed and still pending */
	AOS_RES_DASH,
	AOS_RES_HYPER_DASH,
	AOS_RES_SHIELD,
} aos_resolution;

typedef struct {
	uint8_t held;
	uint8_t pending;
	uint8_t age;        /* frames since the press (saturating) */
	uint8_t resolution; /* aos_resolution for the current press */
} aos_bumper;

typedef struct {
	aos_input_cfg cfg;
	uint32_t prev_raw;
	aos_bumper l, r;
	uint8_t frames_since_l_press; /* saturating */
	uint8_t l_press_gap;          /* gap measured at the current L press */
	uint8_t double_tap_armed;     /* previous L press resolved as plain Dash */
} aos_input;

typedef struct {
	uint32_t events;
	uint32_t held;
} aos_input_out;

void aos_input_init(aos_input *s, const aos_input_cfg *cfg /* NULL = defaults */);
aos_input_out aos_input_update(aos_input *s, uint32_t raw_held);

#ifdef __3DS__
/* libctru hidKeysHeld() bits -> AOS_PHYS bits. */
uint32_t aos_input_phys_from_3ds(uint32_t keys_held);
/* libctru hidKeysHeld() bits -> logical AOS_RAW_* bits through the current bindings. */
uint32_t aos_input_from_3ds(uint32_t keys_held);
#endif

#ifdef __cplusplus
}
#endif

#endif
