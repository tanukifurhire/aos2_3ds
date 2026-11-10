/*
 * aos_input — translates raw 3DS buttons into AoS2 actions.
 *
 * Mapping (user-specified):
 *   L  = Dash              R  = Hyper
 *   A  = Weapon A          B  = Weapon B
 *   X  = Special A         Y  = Special B
 *   L+R pressed together   = Shield
 *   L, L in quick succession = Hyper Dash
 *
 * Because Shield is a chord of the Dash and Hyper buttons, a lone L/R press is
 * held "pending" for up to `chord_window` frames to see whether the partner
 * bumper arrives. That adds up to `chord_window` frames of latency to Dash and
 * Hyper (0 disables the wait: only same-frame presses then form a Shield).
 *
 * The module is a pure function of (state, raw buttons) with no clock, I/O or
 * allocation, so it is deterministic and safe to re-simulate for rollback.
 */
#ifndef AOS_INPUT_H
#define AOS_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device-agnostic raw button bits (see aos_input_from_3ds for libctru keys). */
enum {
	AOS_RAW_L = 1u << 0,
	AOS_RAW_R = 1u << 1,
	AOS_RAW_A = 1u << 2,
	AOS_RAW_B = 1u << 3,
	AOS_RAW_X = 1u << 4,
	AOS_RAW_Y = 1u << 5,
};

/* One-shot events (edge triggered, emitted on the frame they resolve). */
enum {
	AOS_EV_DASH       = 1u << 0,
	AOS_EV_HYPER_DASH = 1u << 1,
	AOS_EV_HYPER      = 1u << 2,
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
	AOS_HELD_HYPER      = 1u << 2,
	AOS_HELD_SHIELD     = 1u << 3,
	AOS_HELD_WEAPON_A   = 1u << 4,
	AOS_HELD_WEAPON_B   = 1u << 5,
	AOS_HELD_SPECIAL_A  = 1u << 6,
	AOS_HELD_SPECIAL_B  = 1u << 7,
};

typedef struct {
	uint8_t chord_window;      /* max frames between L and R presses for Shield */
	uint8_t double_tap_window; /* max frames between two L presses for Hyper Dash */
} aos_input_cfg;

#define AOS_INPUT_DEFAULT_CHORD_WINDOW 3
#define AOS_INPUT_DEFAULT_DOUBLE_TAP_WINDOW 15

typedef enum {
	AOS_RES_NONE = 0,   /* not pressed, or pressed and still pending */
	AOS_RES_DASH,
	AOS_RES_HYPER_DASH,
	AOS_RES_HYPER,
	AOS_RES_SHIELD,
} aos_resolution;

typedef struct {
	uint8_t held;
	uint8_t pending;
	uint8_t age;        /* frames since press, while pending */
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
/* Convert libctru hidKeysHeld() bits into AOS_RAW_* bits. */
uint32_t aos_input_from_3ds(uint32_t keys_held);
#endif

#ifdef __cplusplus
}
#endif

#endif
