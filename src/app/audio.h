#ifndef AOS_AUDIO_H
#define AOS_AUDIO_H

#include <stdbool.h>

/* Sound effect slots used by the input lab (see tools/aos2pack sounds list). */
typedef enum {
	SFX_WEAPON_A, SFX_WEAPON_B, SFX_SPECIAL_A, SFX_SPECIAL_B,
	SFX_DASH, SFX_HYPER_DASH, SFX_HYPER, SFX_SHIELD,
	SFX_COUNT
} aos_sfx;

/* Initialise NDSP and load all SFX from romfs. Returns false (and the game
 * keeps running silently) if the DSP is unavailable, e.g. no dspfirm.cdc
 * dumped on real hardware. Missing files are reported in *err. */
bool audio_init(const char **err);
void audio_play(aos_sfx s);
void audio_exit(void);

/* Game sound effects by original id (sim/se_table.h), with the original's
 * retrigger rules (docs/re/music.md §6). cd = aos_world.se_cd entry:
 * AUDIO_SE_CD_DEFAULT for FUN_007bc9c0, else the FUN_007bcaf0 lockout. */
#define AUDIO_SE_CD_DEFAULT 0xff /* == AOS_SE_CD_DEFAULT */
void audio_se(int id, int cd);
void audio_se_frame(void);       /* FUN_007bcc00: once per frame, after the frame's sounds */
int audio_se_level(void);        /* 0..10, default 5 (DAT_00881eaa) */
void audio_set_se_level(int lvl);
#include <stddef.h>
size_t audio_se_bytes(void);     /* PCM bytes of the loaded SE (linear memory) */
/* At match load: free SE PCM the match can't use, preload what it can
 * (engine character ids, resolved stage id). Sounds not preloaded still load
 * on first use. */
void audio_se_prepare(int id1, int id2, int stage);

#endif
