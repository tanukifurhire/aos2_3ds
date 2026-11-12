/* Config > Controls (port, user request 2026-11-13; the PC's key config 0x280 is a keyboard/pad binder):
 * the menu's MS_CONTROLS state. Two pages:
 *   list    rows Dash / Guard / Primary / Sub / Special A / Special B (each shows its button glyph), Hyper layout,
 *           Input test, Reset, Back. A on an action row waits for a button (A B X Y L R SELECT; START or a tap
 *           cancels, 5 s timeout): a button another action uses swaps the two (aos_binds_assign).
 *   layout  the touch hyper panel of one character as a live preview: L/R character, D-pad picks a quadrant,
 *           A picks it up / drops it on another (swap), a drag on the touch screen swaps directly, Y = default
 *           layout of this character, B back. The centre (Accel Hyper) is fixed.
 * Every change goes to the progress save at once (modes_save()->binds / hyperLayout, written by modes_save_poll
 * outside matches) and to the live tables (aos_binds_current, ht layout). Logic is portable (menu input only);
 * drawing is controls_draw.c. */
#ifndef AOS_APP_CONTROLS_H
#define AOS_APP_CONTROLS_H

#include <stdbool.h>

enum { CTR_ACT0 = 0, CTR_LAYOUT = 6, CTR_TEST, CTR_RESET, CTR_BACK, CTR_COUNT };
enum { CT_LIST, CT_WAIT, CT_LAYOUT };

#define CT_WAIT_FRAMES 300
/* bottom-screen list rows (controls_draw.c draws them, the logic hit-tests taps) */
#define CT_ROW_Y0 25
#define CT_ROW_H 16 /* 10 rows end at y 185, above the help-line band (196) */

typedef struct {
	int page;        /* CT_* */
	int cur;         /* list row CTR_* */
	int wait;        /* frames left while waiting for a button */
	int flash_act;   /* action whose glyph flashes after an assignment (-1 none, -2 all) */
	int flash_other; /* the action it swapped with (-1 none) */
	int flash;       /* frames left of that flash */
	/* layout page */
	int chara;       /* engine id 3..17 */
	int pos;         /* cursor screen position HT_UP..HT_DOWN */
	int held;        /* picked-up position, -1 none */
	int drag;        /* touch-down position, -1 none */
	int gen;         /* bumped on every layout / character change (the draw refits the labels) */
	int swapped[2];  /* the two positions of the last swap (flash), -1 none */
	int swap_flash;
	float t;         /* seconds since open (pulses) */
} controls_state;
extern controls_state g_ctl;

/* Touch hit test of the layout page (screen position under x,y; set by controls_draw.c to panel_hit_pos,
 * NULL in host tests = no touch). */
extern int (*controls_hit)(int x, int y);

/* Apply the progress save's bindings + hyper layouts to the live tables (boot, after modes_flow_init). */
void controls_apply_save(void);
void controls_open(void);
/* One frame while the menu is in MS_CONTROLS; false = closed (back to the config list). */
bool controls_update(void);
/* Action label (MENU_CONFIG_KEY_* texts where the PC has one). */
const char *controls_act_label(int act);
/* Help line text of the current row / page. */
const char *controls_help(void);

#endif
