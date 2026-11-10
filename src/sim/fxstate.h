/* Screen-effect state owned by fx.c (dim, speedlines, blur, cut-in, camera
 * focus list). Plain data so it can live in aos_objects and be snapshotted:
 * the original saves the blur globals in its rollback snapshot (all.c:288368)
 * and the focus list feeds the camera (FUN_005f8b90).
 *
 * Integration (engine request): objects.h includes this header, adds
 * `aos_fxstate fx;` to aos_objects and `#define AOS_HAVE_FXSTATE 1`.
 * Until then fx.c keeps one file-static instance (visual-only effect). */
#ifndef AOS_FXSTATE_H
#define AOS_FXSTATE_H

#include <stdint.h>

typedef struct {               /* dark/dim list record (FUN_004e9280), 0xc */
	int32_t timer;             /* +0 */
	float level;               /* +4 ramps 0 -> 1 */
	float target;              /* +8 max darkness */
} aos_darkrec;

typedef struct {               /* speedline record (FUN_007bec20), 0x14 */
	int32_t timer;             /* +0 */
	float angle, spread;       /* +4 +8 */
	float level;               /* +0xc */
	float phase;               /* +0x10 texture scroll */
} aos_speedrec;

typedef struct {               /* cut-in record (FUN_004e8100), 0x18 */
	int32_t age;               /* +0 */
	int32_t f04;               /* +4 = 1 */
	int32_t f14;               /* +0x14 = 0 */
} aos_cutinrec;

typedef struct {               /* camera focus entry (FUN_005f8fc0), 0x1c */
	int32_t playerId;          /* +0 objId or -1 */
	float pos[3];              /* +4 */
	float zoom;                /* +0x10 */
	int32_t timer;             /* +0x14 */
	float w;                   /* +0x18 blend weight */
} aos_focusrec;

#define AOS_MAX_DARK 16
#define AOS_MAX_SPEED 16
#define AOS_MAX_CUTIN 4
#define AOS_MAX_FOCUS 16

typedef struct {
	/* blur / zoom-flash globals DAT_0087fdfc..fe10 (FUN_004d7ac0) */
	uint8_t blurOn;            /* 0x87fdfc */
	uint8_t blurCaptured;      /* blur-start counter (0x87fe0c capture flag lives draw-side in fx.c) */
	int32_t blurTimer;         /* 0x87fe08 */
	float blurLevel;           /* 0x87fe00 */
	float blurAmount;          /* 0x87fe04 */
	int32_t blurBlend;         /* 0x87fe10 */
	int32_t ndark, nspeed, ncutin, nfocus;
	aos_darkrec dark[AOS_MAX_DARK];
	aos_speedrec speed[AOS_MAX_SPEED];
	aos_cutinrec cutin[AOS_MAX_CUTIN];
	aos_focusrec focus[AOS_MAX_FOCUS];
} aos_fxstate;

#endif
