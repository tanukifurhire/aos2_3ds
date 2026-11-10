/* Logo (scene 1) and Menu (scene 4: title, main menu, sub-menus, config):
 * modes.md §A.3-A.5. Logic in menu.c (portable, host-tested by
 * tests/test_menu.c), drawing in menu_draw.c (3DS). */
#ifndef AOS_APP_MENU_H
#define AOS_APP_MENU_H

#include <stdbool.h>

/* Menu states (DAT_009b1610). */
enum {
	MS_TITLE = 10,
	MS_MAIN = 100,
	MS_MATCH = 200,
	MS_DIFFICULTY = 300,
	MS_RANKING = 400,
	MS_MEMORY = 500,
	MS_CONFIG = 600,
	MS_LEAVING = 900,
	MS_TRAINING = 0x44c,
	MS_MANUAL = 0x456,
};

/* Main-menu items (DAT_009b1618). */
enum { ITEM_ARCADE, ITEM_STORY, ITEM_ONLINE, ITEM_MATCH, ITEM_TRAINING, ITEM_MEMORY, ITEM_RANKING, ITEM_CONFIG, ITEM_EXIT, ITEM_COUNT };

/* Config rows kept on 3DS (modes_menu_config.tsv row ids; >= 100 port-only). */
enum {
	CR_LANGUAGE = 0, CR_REQUIREDWINS = 6, CR_TIMELIMIT = 7, CR_SFX = 9, CR_BGM = 10, CR_CONTROLS = 12,
	CR_HITSTUN = 13, CR_HICONTRAST = 14, CR_P1COLOR = 15, CR_P2COLOR = 16, CR_TOURNAMENT = 17, CR_PAUSEHOLD = 18,
	CR_PAUSEMENU = 19, CR_GREYBG = 21, CR_ARMOR = 22, CR_ARMORPCT = 23, CR_ATTACKIND = 24, CR_ATTACKMIRROR = 25,
	CR_EXIT = 26,
	CR_CITYLOD = 100, CR_NETDELAY = 101,
};
#define MENU_CFG_ROWS 21
extern const int menu_cfg_rows[MENU_CFG_ROWS];

typedef struct {
	int state, counter;      /* 1610, 1614 */
	int cursor;              /* 1618 main item */
	int match_cur;           /* 161c: 0 2P, 1 CPU */
	int train_cur;           /* 1620: 0 How to Play, 1 Practice */
	int page;                /* 1624: manual page 0..8 */
	int diff;                /* 163c */
	int cfg_cur;             /* 1640: index into menu_cfg_rows */
	int diff_from;           /* state that opened the difficulty panel (100 or 200) */
	float a_logo, a_band;    /* 1664, 1668 */
	float a_diff, a_match, a_train, a_manual, a_cfg; /* 166c, 1674, 1678, 167c, 1680 */
	float flash;             /* 1660 white flash */
	float kick;              /* 16d0 carousel glide */
	int manual_scroll;       /* port: How to Play vertical scroll (px) */
	int cfg_lang;            /* 2044: language copy applied on exit */
} menu_state;
extern menu_state g_menu;

/* Ask the next Menu Init to resume the main menu (state 100, cursor kept,
 * BGM 0x18) instead of the title: port-only return paths (coming soon,
 * netplay lobby, input lab, the port's select screen). */
void menu_resume_next(void);

void menu_logic_init(void);   /* Menu Init FUN_00642190 */
void menu_logic_update(void); /* FUN_00643150 */
void menu_logic_exit(void);   /* Menu Exit FUN_00642880: g_setup + next scene */
/* Sum of the open sub-panels' alpha (the band's raise factor). */
float menu_sub_alpha(void);
/* Config value helpers (shared with the draw). */
int menu_cfg_count(int row);            /* number of options, 0 = no value (Controls/Exit) */
int menu_cfg_value(int row);            /* current option index */
const char *menu_cfg_label(int row);    /* label text */
const char *menu_cfg_option(int row, int i, char *buf, int bufsz); /* option text */
int menu_cfg_announce(int row);         /* announce id (-1 = port text, see menu_cfg_help) */
const char *menu_cfg_help(int row);     /* help text for the bottom screen */

/* Logo scene (FUN_006321a0). */
typedef struct {
	int state, counter;
} logo_state;
extern logo_state g_logo;
void logo_logic_init(void);
void logo_logic_update(void);

/* Platform hooks implemented by menu_draw.c (stubbed in host tests). */
void menu_apply_language(int lang); /* reload strings (+ fonts) */
void menu_apply_city_lod(int lod);
void menu_scenes_register(void); /* logo, menu, coming soon (menu_draw.c) */

#endif
