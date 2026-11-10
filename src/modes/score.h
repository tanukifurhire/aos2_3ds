/*
 * Match score, ranking, unlocks and clear records (docs/re/modes.md §D.2, §D.4.4, §D.5).
 * Pure C, runs outside the tick; reads only the end-of-match facts the sim exposes.
 */
#ifndef AOS_MODES_SCORE_H
#define AOS_MODES_SCORE_H

#include <stdint.h>

#include "../sim/sim.h"
#include "save.h"

/* Unlock-message vector 0x8940d4 (FUN_004aa120 push): 1000+char, 2000+stage, 3000+bgm. */
#define MODES_MAX_MSGS 16
typedef struct {
	int32_t n;
	int32_t id[MODES_MAX_MSGS];
} modes_msgs;
/* Text id of the popup line for a message (MENU_UNLOCK_CHARACTER / STAGE / BGM, FUN_006471d0). */
static inline int modes_msg_text(int32_t id) { return id < 2000 ? 0x23b : id < 3000 ? 0x23c : 0x23d; }

/* Game Exit FUN_005f5780 (DAT_00885970 == 1 part): winner side and the sums of the winner's
 * FUN_0061ac80 records shown by the result screen (FUN_007975a0). */
typedef struct {
	int32_t winner;  /* DAT_009b6930: 0 P1, 1 P2 (p0.hp <= p1.hp -> 1) */
	int32_t seconds; /* DAT_009b694c sum of +4 */
	int32_t hp;      /* DAT_009b6950 sum of +8 */
	int32_t damage;  /* DAT_009b6954 sum of +0xc */
	int32_t hits;    /* DAT_009b6958 max of +0x10 */
	int32_t bonus;   /* DAT_009b6948 sum of +0 */
	int32_t score;   /* DAT_009b695c sum of +0x14 */
} modes_result;
void modes_match_result(const aos_world *w, modes_result *out);

/* FUN_00792f40: insert (score, char) into the difficulty's top 10; returns the position
 * 0..9 or 10 (not ranked). The new entry's name is 7 x '-' (0x2d). Strictly greater
 * scores rank above equal ones already in the table. Sets s->dirty. */
int modes_rank_insert(aos_save *s, int difficulty, int32_t score, int32_t character);
/* Name entry result (FUN_00658550 writes the letters; port helper). */
void modes_rank_set_name(aos_save *s, int difficulty, int pos, const int32_t name[AOS_RANK_NAME]);

/* FUN_004a8390 arcade-end unlock check (clears msgs first, then pushes; saves). */
void modes_unlocks_arcade(aos_save *s, modes_msgs *msgs);
/* FUN_007c3e80 story-end: story achievement unless gameOver (DAT_009b692d) or difficulty 0,
 * then the unlock checks (msgs are NOT cleared, unlike arcade; saves). */
void modes_unlocks_story(aos_save *s, modes_msgs *msgs, int gameOver, int difficulty, int32_t character);
/* Endroll Exit FUN_00519f60 bookkeeping: only if continues == 0, record the clear
 * (arcade: per difficulty + char flag; story: count + char flag) and ACH_NOCONTINUE. Saves. */
void modes_record_clear(aos_save *s, int mode, int difficulty, int32_t character, int32_t continues);

/* "cleared(c)" mark on the select screen (FUN_0079fb30). */
static inline int modes_arcade_cleared(const aos_save *s, int difficulty, int c)
{
	return difficulty >= 0 && difficulty < 3 && c >= 0 && c < AOS_SAVE_CHARS && s->arcadeClear[difficulty][c];
}
static inline int modes_story_cleared(const aos_save *s, int c)
{
	return c >= 0 && c < AOS_SAVE_CHARS && s->storyClear[c];
}

#endif
