/* Streaming BGM player (Ogg Vorbis via Tremor -> NDSP channel 23).
 * Tracks are romfs:/bgm/<id>.ogg; ids and loop points follow the original
 * game's table (docs/re/bgm_table.tsv, FUN_004bdc80). */
#ifndef AOS_BGM_H
#define AOS_BGM_H

#include <stdbool.h>

bool bgm_init(void);              /* needs ndspInit() to have succeeded */
bool bgm_play(int id);            /* stops the current track, loops; false if missing */
bool bgm_play_ex(int id, bool loop); /* loop=false: play once, then silence (Ayame +4) */
void bgm_stop(void);
void bgm_set_volume(float v);     /* linear gain 0..1 */
void bgm_set_ayame_volume(float v); /* original's Ayame volume 0..100 (-(100-v)^2 cB) */
float bgm_ayame_gain(float v);    /* the linear gain bgm_set_ayame_volume applies */
int bgm_current(void);            /* -1 when stopped */
void bgm_exit(void);

#endif
