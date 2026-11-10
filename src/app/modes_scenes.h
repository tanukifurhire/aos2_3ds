/* Part D scenes (docs/re/modes.md §D): Endroll (scene 11, endroll.c) and the menu's
 * ranking view / name entry / unlock popup sub-screens (ranking.c logic, portable;
 * ranking_draw.c 3DS drawing). The sub-screens are states of the Menu scene (4):
 *   400   MS_RANKING      ranking view (FUN_0065c670 / draw FUN_0065c7c0), 9 pages
 *   0x19a MS_NAME_ENTRY   arcade name entry (FUN_00658550 / FUN_00658970)
 *   0x1a4 MS_UNLOCK       unlock popups (FUN_00646ee0 / FUN_006471d0)
 */
#ifndef AOS_APP_MODES_SCENES_H
#define AOS_APP_MODES_SCENES_H

#include <stdbool.h>
#include <stdint.h>

void endroll_register(void); /* scene_register(SCENE_ENDROLL, ...) */

enum { MS_NAME_ENTRY = 0x19a, MS_UNLOCK = 0x1a4 }; /* menu states (MS_RANKING = 400 is in menu.h) */

#define RANK_PAGES 9
#define RANK_LETTERS 46 /* name slot values 0..45 (45 = blank) */
#define RANK_BLANK 45

typedef struct {
	int page;        /* DAT_009b164c 0..8 (0..2 = Easy/Normal/Hard tables, 3..8 tournaments) */
	float v[10];     /* DAT_009b1694 row slide */
	float a;         /* DAT_009b1690 ranking panel alpha */
	float b;         /* DAT_009b16bc name-entry alpha */
	int pos;         /* DAT_009b1654 name cursor 0..6 */
	int msg;         /* DAT_009b165c popup index */
	float pa, pb;    /* DAT_009b16c8 / 16cc popup band / text alpha */
	float ring;      /* DAT_009b3504 cursor ring phase */
	int hl_rank;     /* DAT_008940d0 highlighted / entered rank (10 = none) */
	int hl_diff;     /* DAT_0087fab0 of that run */
	int32_t score;   /* DAT_008940cc */
	int chara;       /* DAT_0087fac4 */
	int nmsg;        /* popups pending at Menu Init */
} rank_state;
extern rank_state g_rank;

/* Hooks called by menu.c */
void ranking_menu_init(int prev);  /* end of Menu Init FUN_00642190 (after the path's own setup) */
void ranking_open(void);           /* main menu item 6 (Ranking) */
void ranking_update(void);         /* states 400 / 0x19a / 0x1a4 */
void ranking_tail(void);           /* every menu frame: alphas, row slide */

/* FUN_00659480: one name slot -> ASCII ('A'..'Z', '0'..'9', . / ! ? ( ) " - =, blank) */
char ranking_letter(int32_t v);
/* 7-letter name of table[page][k] (pages 0..2) into out (8 bytes); trim = drop trailing blanks */
void ranking_name(int page, int k, bool trim, char out[8]);
/* tournament pages 3..8: rows (8), rank text, name, character */
int ranking_rows(int page);
const char *ranking_tour_rank(int page, int k);
const char *ranking_tour_name(int page, int k);
int ranking_char(int page, int k);
int32_t ranking_score(int page, int k);
int ranking_title_id(int page); /* text id of the page title */

/* 3DS drawing (ranking_draw.c), called by menu_draw.c */
void ranking_draw_top(void);
void ranking_draw_bottom(void);
void ranking_gfx_free(void);

#endif
