/* Music director: the original's BGM request/fade state machine and VS-mode
 * BGM selection (docs/re/music.md). Call music_update() once per frame. */
#ifndef AOS_APP_MUSIC_H
#define AOS_APP_MUSIC_H

#include <stdbool.h>

/* BGM ids used by the menus (docs/re/music.md §3). */
enum {
	MUSIC_NONE = 0,
	MUSIC_RESULT = 1,  /* result screen jingle, once (FUN_00796030) */
	MUSIC_VERSUS = 2,  /* "VS" screen jingle, once (FUN_0079c110 state 300) */
	MUSIC_SELECT = 0x16, /* character select (FUN_0079acc0) */
	MUSIC_LOBBY = 0x17,  /* online lobby (FUN_007b6630) */
	MUSIC_MENU = 0x18,   /* main menu (FUN_00642190) */
	MUSIC_RANDOM = 10,   /* list entry "????" (BGM_RAMDOM) */
};

void music_init(void);   /* builds the BGM list; seeds the cosmetic RNG */
void music_update(void); /* FUN_004beeb0: once per frame */

/* FUN_004bee00(id, restart, loop): queue id; fades the current track out first. */
void music_request(int id, bool restart, bool loop);
/* FUN_004bee60(id, loop): switch to id on the next update without a fade. */
void music_force(int id, bool loop);
int music_current(void); /* DAT_0087a528 */

/* Volume levels 0..10 (config, default 5): BGM = DAT_00881ea8. */
int music_level(void);
void music_set_level(int lvl); /* applies immediately (FUN_004bf2e0) */

/* VS stage-select BGM list (DAT_009b8dd4) and cursor (DAT_009b6adc). */
int music_list_count(void);
int music_list_id(int index);
int music_sel(void);
void music_sel_step(int dir); /* up/down with wrap, plays cursor SE 2 */
const char *music_name(int id); /* script/en/bgm.txt name, "" if none */
/* Select Exit (FUN_0079bfc0): the selection's BGM id, "????" resolved with the
 * cosmetic RNG (FUN_007aaed0) — never the game RNG. */
int music_resolve(void);

/* Scene helpers (what the original does at these points). */
void music_scene_select(void);       /* Select Init: force 0x16, loop */
void music_battle_start(int id);     /* Game Init of a new match */
void music_battle_exit(void);        /* Game Exit FUN_005f5780: fade out */
struct aos_world;
/* Per battle frame: follows the world's flow. When the match-end wipe starts
 * (FUN_00608a00: FUN_00561fc0(0x12) + FUN_004bee00(0,1,1)) the BGM fades out. */
void music_watch(const struct aos_world *w);

#endif
