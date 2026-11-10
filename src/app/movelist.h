/* Pause-menu command list data (port of the PC's per-character builders FUN_00756600 & co., one per
 * character, called from FUN_006aed80): per move the category (FUN_00591860 argument), the name key
 * (FUN_00591730 text id - 0x2b7 = index in commandlist_names.txt) and the input as the original token list
 * (the int arrays passed to FUN_006a1a50). Only the standard-control rows are kept: with the hypers on the
 * touch screen both control schemes show the same list. Portable data. */
#ifndef AOS_APP_MOVELIST_H
#define AOS_APP_MOVELIST_H

#include <stdint.h>

/* Categories (commandlist_categories.txt order) */
enum { ML_REGULAR = 0, ML_SPECIAL = 1, ML_HYPER = 2, ML_ACCEL = 3 };
/* Input tokens; HOLD/CLOSE/LONG/DASHING = COMMANDLISTINSTRUCTION_HOLD/CLOSERANGE/LONGRANGE/ALSOWHILEDASHING */
enum { T_PLUS = 0, T_HOLD = 1, T_CLOSE = 2, T_LONG = 3, T_DASHING = 4, T_A = 10, T_B = 11, T_S = 12, T_H = 13, T_D = 14,
       T_SHIELD = 15 };

typedef struct {
	uint8_t chara, cat, star; /* star: 1 = "*", 2 = "**" (only with the character's NOTE / NOTE1 / NOTE2 line) */
	const char *key;
	uint8_t tok[6];
	uint8_t ntok;
} ml_entry;

/* Moves of engine character id (Sumika's second form uses Sumika's list, like the PC); returns the count. */
int movelist_get(int chara, const ml_entry **out);

#endif
