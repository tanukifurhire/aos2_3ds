/*
 * Touch-screen hyper buttons (port addition, user request 2026-11-10): the bottom screen shows five hyper
 * buttons and R became Shield. A touch on a button is turned into exactly the button combination the
 * character's input dispatch expects (docs/re/<char>.md "Input dispatch", src/sim/chars/<char>.c input_a/input_b)
 * and OR'ed into the player's ordinary per-frame input mask (AOS_IN_*), so the simulation, replays and
 * rollback netplay see plain inputs: nothing here touches the simulation state.
 *
 * Slots (same for every character, by the original input):
 *   HT_UP     S+H     HT_LEFT  H (A+H)     HT_RIGHT  B+H     HT_DOWN  D+H     HT_CENTER  S+B+H (Accel Hyper)
 * Standard control scheme (p->scheme 0): while the button is held the mask carries the slot's modifiers +
 * H, and the modifiers that would select a different row are suppressed (cleared until the physical button
 * is let go, so no fake press edge appears when the finger lifts). Advanced scheme (1, "H held + X pressed"):
 * H + the slot's trigger (Shield / S / B / D / A); the trigger is withheld for one frame if it was already
 * held so the sim sees it pressed. Sumika's second form (id 18) reads the standard rows in both schemes.
 *
 * Portable C (host tests: tests/test_hyper_touch.c).
 */
#ifndef AOS_HYPER_TOUCH_H
#define AOS_HYPER_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "../sim/sim.h"

enum { HT_UP, HT_LEFT, HT_RIGHT, HT_DOWN, HT_CENTER, HT_NSLOT };

typedef struct {
	const char *key; /* COMMANDLIST_* name key (commandlist_names.txt); NULL = the character has no such hyper */
	const char *alt; /* variant shown second (a ★/☆ form or a range split), NULL if none */
	float cost;      /* hyper gauge levels (FUN_00780910 argument) */
} ht_move;

/* Slot description for engine character id (3..18). Never NULL (an empty move for unknown ids). */
const ht_move *ht_slot(int chara, int slot);
/* Screen layout (port, user request 2026-11-13): per character a permutation screen position -> slot, editable
 * by the player (Config > Controls > Hyper layout) and kept in the progress save. The centre is always the Accel
 * Hyper; an empty slot moves with the layout like any other. Default (ht_layout_default): left/right are where
 * the thumbs rest, so they are filled first: an empty left/right slot swaps with the bottom (then top) move and
 * the gap shows at the top/bottom. Sumika's second form (18) uses Sumika's layout. The touch -> logical slot
 * conversion is local (the slot becomes ordinary input bits), so netplay sends nothing extra. */
int ht_pos_slot(int chara, int pos);  /* screen position -> slot */
int ht_slot_pos(int chara, int slot); /* slot -> screen position */
void ht_layout_default(int chara, uint8_t pos_slot[HT_NSLOT]);
/* A permutation of the slots with the centre on the centre. */
bool ht_layout_valid(const uint8_t pos_slot[HT_NSLOT]);
void ht_layout_get(int chara, uint8_t pos_slot[HT_NSLOT]);
/* Set chara's layout (invalid -> the default). */
void ht_layout_set(int chara, const uint8_t pos_slot[HT_NSLOT]);
/* Swap what sits at two (non-centre) positions; false if not allowed. */
bool ht_layout_swap(int chara, int pos_a, int pos_b);
bool ht_layout_is_default(int chara);
void ht_layout_reset_all(void);
/* The slot exists, the gauge pays for it and FUN_00780950 allows a hyper now. */
bool ht_ready(const aos_world *w, int player, int slot);

typedef struct {
	int8_t slot;       /* slot synthesised last frame (-1 none) */
	uint8_t age;       /* frames the current slot has been held */
	uint16_t suppress; /* physical modifiers held back until released */
	uint16_t last;     /* the mask returned last frame */
} ht_state;

void ht_init(ht_state *s);
/* One frame: mask = the physical mask (to_mask), slot = the touched button (-1 none). Returns the mask to
 * send. Deterministic, no I/O. */
uint16_t ht_apply(ht_state *s, int chara, int scheme, int slot, uint16_t mask);

#endif
