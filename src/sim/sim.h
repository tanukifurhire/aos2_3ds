/*
 * AoS2 gameplay simulation — a 1:1 port of aos2.exe's match logic.
 *
 * Conventions (see docs/re/HANDBOOK.md, core.md):
 *  - Every original field keeps its meaning; the original offset is noted on
 *    each member so decompiled code like `*(float *)(p + 0x6c)` maps to p->x.
 *  - Handlers have the original's shape: `bool update(aos_world*, aos_player*)`
 *    (T2, table 0x8809a0) and `void draw(aos_world*, aos_player*)` (T1, 0x880030),
 *    indexed by the same state ids (0..603).
 *  - Character ids are ENGINE ids (Sora 3 … Sumika 2nd 18), as at player+0x10.
 *  - All state is inside aos_world (no globals) so a match can be snapshotted
 *    with memcpy for rollback netplay.
 */
#ifndef AOS_SIM_H
#define AOS_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "amath.h"
#include "rng.h"

#define AOS_GAME_VERSION 0x5a2 /* DAT_0089426c for live matches (core.md §0) */
#define AOS_NUM_STATES 604     /* (0x8809a0 - 0x880030) / 4 */
#define AOS_NUM_CHARS 19       /* stats records; 0..2 unused */

/* Engine character ids (player+0x10). */
enum {
	AOS_CH_SORA = 3, AOS_CH_ALTE, AOS_CH_TSIH, AOS_CH_MIRA, AOS_CH_SHAM, AOS_CH_NATH,
	AOS_CH_STARBREAKER, AOS_CH_SUGURI, AOS_CH_SAKI, AOS_CH_IRU, AOS_CH_NANAKO,
	AOS_CH_KAE, AOS_CH_KYOKO, AOS_CH_HIME, AOS_CH_SUMIKA, AOS_CH_SUMIKA2,
};

/* Common states (core.md §4). */
enum {
	AOS_ST_NEUTRAL = 0, AOS_ST_HITSTUN = 1, AOS_ST_DASH = 2, AOS_ST_SHIELD = 3,
	AOS_ST_BURST = 4, AOS_ST_PUSHED = 5, AOS_ST_KO = 6,
};

/* Button field values (FUN_0061bc70). */
enum { AOS_BTN_UP = 0, AOS_BTN_RELEASED = 1, AOS_BTN_HELD = 2, AOS_BTN_PRESSED = 3 };

/* Raw per-frame input mask bits (FUN_006116c0 layout). */
enum {
	AOS_IN_UP = 0x0001, AOS_IN_DOWN = 0x0002, AOS_IN_LEFT = 0x0004, AOS_IN_RIGHT = 0x0008,
	AOS_IN_A = 0x0010, AOS_IN_B = 0x0020, AOS_IN_S = 0x0040, AOS_IN_D = 0x0080,
	AOS_IN_H = 0x0100, AOS_IN_SHIELD = 0x0200, AOS_IN_PAUSE = 0x0400,
};

/* Status effect slots (core.md §6.8). */
enum {
	AOS_STATUS_HEAT = 1, AOS_STATUS_DRAIN = 2, AOS_STATUS_NODASH = 3, AOS_STATUS_SLOW = 4,
	AOS_STATUS_5 = 5, AOS_STATUS_STUNIMMUNE = 6, AOS_STATUS_OVERHEAT = 7, AOS_STATUS_MARK = 8,
	AOS_STATUS_COUNT = 9,
};

typedef struct aos_player {
	int32_t objId;        /* +0x04 */
	int32_t inputSrc;     /* +0x08 <9 local, 9 none, 10 CPU, 11 net */
	int32_t scheme;       /* +0x0c control scheme 0 (A) / 1 (B) */
	int32_t id;           /* +0x10 engine character id */
	int32_t side;         /* +0x14 1 = P1, 2 = P2 */
	int32_t color;        /* +0x18 palette 0..2 */
	int32_t right, left, up, down; /* +0x1c +0x20 +0x24 +0x28 — must stay contiguous (aos_dirs) */
	int32_t menuOk, menuCancel;    /* +0x2c +0x30 */
	int32_t D, A, B, S, H, Shield; /* +0x34 .. +0x48 */
	int32_t Pause;                 /* +0x4c */
	float hurtShape[4];   /* +0x5c {halfW, halfH, radius, ?} */
	float x, y, z;        /* +0x6c */
	float px, py, pz;     /* +0x78 post-clamp copy */
	float faceAngle;      /* +0x84 */
	float dashAngle;      /* +0x88 */
	uint8_t facingRight;  /* +0x8c */
	int32_t f90;          /* +0x90 */
	float moveAngle;      /* +0x94 */
	float speed;          /* +0x98 */
	float friction;       /* +0x9c */
	uint8_t clampArena;   /* +0xa0 */
	uint8_t fa1;          /* +0xa1 */
	uint8_t hittable;     /* +0xa2 */
	uint8_t statusOn[AOS_STATUS_COUNT];    /* +0xa3 */
	int32_t statusTime[AOS_STATUS_COUNT];  /* +0xac */
	int32_t hp, maxHp, redHp, recovHp;     /* +0xd0 */
	float heat;           /* +0xe0 */
	float hyper;          /* +0xe4 */
	float gaugeA, gaugeB; /* +0xe8 +0xec */
	float hyperMul;       /* +0xf0 */
	int32_t barrierType;  /* +0xf4 */
	int32_t barrierTime;  /* +0xf8 */
	float barrierR;       /* +0xfc */
	float barrierScale;   /* +0x100 */
	float barrierRot;     /* +0x104 */
	int32_t barrierBreakT;/* +0x108 */
	int32_t f10c, f110;   /* +0x10c +0x110 */
	int32_t f114;         /* +0x114 char-specific handle, -1 at init */
	union {               /* +0x118..+0x13c combo display (attacker side) */
		int32_t comboUi[10];
		struct {
			int32_t f118;
			int32_t comboShowDmg;   /* +0x11c */
			int32_t f120;
			int32_t comboBestDmg;   /* +0x124 */
			int32_t comboShowHits;  /* +0x128 (read by several characters' hit logic) */
			int32_t comboBestHits;  /* +0x12c */
			int32_t comboShowTimer; /* +0x130 */
			float comboFadeA;       /* +0x134 */
			int32_t f138;
			float comboFadeB;       /* +0x13c */
		};
	};
	int32_t comboTimer;   /* +0x140 */
	float stun;           /* +0x144 */
	int32_t comboDamage;  /* +0x148 */
	int32_t comboHits;    /* +0x14c */
	int32_t state;        /* +0x150 */
	int32_t timer;        /* +0x154 */
	uint8_t hitFlag;      /* +0x158 */
	uint8_t scratch[16];  /* +0x15c..+0x16b character scratch (e.g. Tsih +0x164 stealth = scratch[8]) */
	int32_t roundsWon;    /* +0x170 */
	int32_t f174;         /* +0x174 (Nath: per-victim 40-frame re-hit lockout, nath.md) */
	/* Port-only: character-specific extended state that the original kept in
	 * the scratch bytes or in its own globals (per-character modules own it). */
	union {
		uint8_t raw[256];
	} ext;
} aos_player;

/* Per-character stats record (0x881f80 + id*0x2dc), gameplay fields only. */
typedef struct {
	float walk;      /* +0x00 */
	float dashSpeed; /* +0x04 */
	float dashHold;  /* +0x08 */
} aos_stats;

/* Gauge cost table entry (0x87c4f0 + state*0xc), see costs.c. */
typedef struct {
	float a, b, hyperGain;
} aos_cost;

struct aos_world;
typedef bool (*aos_update_fn)(struct aos_world *, aos_player *);
typedef void (*aos_draw_fn)(struct aos_world *, aos_player *);
typedef bool (*aos_input_fn)(struct aos_world *, aos_player *);
typedef void (*aos_cpu_fn)(struct aos_world *, aos_player *, uint16_t *mask);

/* Static (read-only after registration) tables — not part of the snapshot. */
typedef struct {
	aos_update_fn T2[AOS_NUM_STATES]; /* 0x8809a0 */
	aos_draw_fn T1[AOS_NUM_STATES];   /* 0x880030 */
	aos_input_fn inputA[AOS_NUM_CHARS]; /* 0x881310 */
	aos_input_fn inputB[AOS_NUM_CHARS]; /* 0x881360 */
	aos_cpu_fn cpu[AOS_NUM_CHARS][4];   /* 0x881400 + id*0x10 + diff*4 */
	aos_stats stats[AOS_NUM_CHARS];
	aos_cost cost[AOS_NUM_STATES];
	int32_t firstState[AOS_NUM_CHARS];   /* character neutral state (common state 0 dispatch) */
	int32_t initialState[AOS_NUM_CHARS]; /* state at round start (core.md §3 end) */
	/* Character hooks inside shared engine code: phase 0 = barrier created
	 * (FUN_006b1b40), 1 = each frame with barrierTime < 21 (player update). */
	void (*barrierHook[AOS_NUM_CHARS])(struct aos_world *, aos_player *, int phase);
} aos_tables;

extern aos_tables aos_tab;

#include "objects.h" /* object pools (beam, missile, laser, hitbox, unit, effect) */

/* Round/match flow and the banner/transition timers that drive it (flow.c,
 * docs/re/hud.md). The PC keeps these in globals; they are simulated inside
 * the tick (FUN_00617a60 / FUN_00608a00) or between frames (FUN_00561c70), so
 * they live in the world (rollback snapshots them). */
#define AOS_END_SETS 4 /* FUN_0060b310 kinds: 0 KO "FINISH!", 1 "TIMEOUT", 2/3 netplay */
typedef struct {
	int32_t firstInit;       /* DAT_00885879: the next Game Init starts a new match */
	int32_t roundIndex;      /* DAT_00885a70: rounds played this match (0-based) */
	int32_t roundSeconds;    /* round time Game Init loads into roundTimer (config DAT_00881eac) */
	int32_t nextRound;       /* DAT_00885878: run Game Init at the start of the next frame */
	int32_t matchEnd;        /* port: match decided and the 0x12 wipe finished -> app leaves (result scene) */
	/* round-start banner, FUN_006179d0 / FUN_00617a60 (on = roundStartBanner) */
	int32_t stT;             /* DAT_0087fafc */
	int32_t stRound;         /* DAT_0087fb00: 0 one-round match, 99 final round, else round number */
	float stAlpha;           /* DAT_0087fb2c */
	float stText;            /* DAT_0087fb04 */
	float stNoise;           /* DAT_0087fb30 */
	float stLetter[9];       /* DAT_0087fb08 */
	int32_t stDelay[9];      /* DAT_0087fb34 */
	/* round-end banner, FUN_0060b310 / FUN_00608a00 (on = roundEndBanner) */
	int32_t enT;             /* DAT_0087fb5c */
	int32_t enNext;          /* DAT_0087fb64 */
	int32_t enType;          /* port: FUN_0060b310 argument (the PC infers it from the delays) */
	float enAlpha;           /* DAT_0087fba4 */
	float enLetter[AOS_END_SETS][12]; /* DAT_0087fb6c / fb88 / fbe0 / fc0c */
	int32_t enDelay[AOS_END_SETS][12]; /* DAT_0087fba8 / fbc4 / fc3c / fc68 */
	/* screen transition, FUN_00561fc0 / FUN_00561c70 (drawn by FUN_005622a0) */
	int32_t fadeOn;          /* DAT_0087e7c0 */
	int32_t fadeType;        /* DAT_0087e7c4 */
	int32_t fadeDone;        /* DAT_0087e7d0 */
	int32_t fadePending;     /* port: FUN_00561c70 runs after the draw; done at the next frame start */
	float fadeA;             /* DAT_0087e7cc */
	float fadeRate;          /* DAT_0087e7c8 */
	float tileA[15][20];     /* DAT_0087e7d4 (+row*0x14+col)*0x10 */
	int32_t tileDelay[15][20]; /* DAT_0087e7e0 */
} aos_flow;

typedef struct aos_world {
	aos_player p[2];
	aos_mt rng;            /* game RNG (0x876ef0), reseeded with 0 every round */
	int32_t frame;         /* DAT_008858f8 match frame counter (reset each round) */
	int32_t roundTimer;    /* DAT_00885974 seconds; 999 = infinite */
	int32_t roundOver;     /* DAT_00885964 == 2 */
	int32_t roundEndBanner;/* DAT_0087fb58 */
	int32_t roundStartBanner; /* DAT_0087faf8 */
	int32_t roundsToWin;   /* DAT_0087faf0 */
	int32_t matchFinished; /* DAT_0088596c */
	int32_t difficulty;    /* DAT_0087fab0 (CPU) */
	int32_t gameMode;      /* DAT_0087faac: 0 normal; 3 = training-like (Sumika 2nd form AI idles); 4 replay */
	int32_t nextObjId;     /* DAT_0088597c */
	aos_flow flow;         /* round/match flow, banners, transitions (flow.c) */
	aos_objects obj;
	/* Events emitted this tick for the platform layer (sounds); not gameplay state. */
	/* Per-character globals that the original kept in static memory shared by both
	 * players (e.g. Mira DAT_00880028). Indexed by engine char id; each module
	 * casts its slice to its own struct. Part of the snapshot. */
	uint8_t charGlobal[AOS_NUM_CHARS][64];
	/* CPU AI (aos_tab.cpu). The original runs the AI in FUN_006116c0, outside the
	 * tick, while the COSMETIC stream (0x875b68, seeded from random_device) is
	 * selected — so AI draws never touch the game RNG. Port: own stream, part of
	 * the snapshot; seeded at match init (the platform may reseed it). */
	aos_mt cpuRng;
	/* Static globals of the AI functions (one int per original global, e.g.
	 * Suguri DAT_009b68c4 / DAT_009b68c0). Shared by both players, as in the PC game. */
	int32_t cpuGlobal[AOS_NUM_CHARS][8];
	uint8_t se_count;
	uint8_t se[32];
	/* Per-entry retrigger lockout for the platform mixer (cosmetic, see
	 * docs/re/music.md §6): AOS_SE_CD_DEFAULT = FUN_007bc9c0 (lock 2 frames),
	 * else the FUN_007bcaf0 cooldown argument. */
	uint8_t se_cd[32];
} aos_world;

#define AOS_SE_CD_DEFAULT 0xff

/* ---- engine API (core.c) ---- */
void aos_register_all(void);  /* fills aos_tab (like FUN_0049ae40) */
void aos_match_init(aos_world *w, const int32_t ids[2], const int32_t schemes[2],
                    const int32_t colors[2], int32_t roundSeconds, int32_t roundsToWin);
void aos_round_init(aos_world *w);           /* Game Init per round (FUN_005f4650) */
void aos_tick(aos_world *w, const uint16_t masks[2]); /* FUN_006116c0 decode + FUN_006129b0 */
void aos_draw_players(aos_world *w);         /* FUN_006b1ea0 */

/* ---- round/match flow (flow.c, docs/re/hud.md §5) ---- */
/* New match with the same characters/settings (roundsWon = 0, round 1). Call
 * when w->flow.matchEnd is set (the PC goes to the result screen there). */
void aos_match_restart(aos_world *w);
bool aos_match_decided(const aos_world *w);       /* FUN_00619c60 */
int aos_match_winner(const aos_world *w);         /* player index with roundsWon >= roundsToWin, -1 none */
void aos_round_end_banner(aos_world *w, int kind); /* FUN_0060b310 */
void aos_screen_fade(aos_world *w, int type);      /* FUN_00561fc0 */
/* The in-match HUD (FUN_006bafd0) is drawn only while this holds (FUN_005f91d0). */
static inline bool aos_hud_visible(const aos_world *w)
{
	return !w->roundStartBanner && w->flow.enAlpha == 0.0f;
}
/* internal hooks called by core.c */
void aos_flow_round_init(aos_world *w);  /* Game Init's banner/timer/transition part */
void aos_flow_frame_start(aos_world *w); /* FUN_006124b0 head: pending FUN_00561c70, Game Init */
void aos_flow_tick(aos_world *w);        /* tick step 8: FUN_00617a60, FUN_00608a00 */
void aos_flow_frame_end(aos_world *w);   /* side effects of the frame's draw (FUN_006b5b50 tail) */
void aos_draw_world(aos_world *w);           /* full gameplay draw order (drawworld.c) */
extern void (*aos_fx_draw_hook)(aos_world *w); /* set by fx.c: particles/sparks/popups/screen fx */
int aos_team_colour(const aos_world *w, int team, int k); /* DAT_008859f4+team*0x10+4k: colour-table index */
uint32_t aos_colour_rgba(int idx, float alpha);         /* 2D colour table 0x87e138 */
void aos_draw_line(float x0, float y0, float x1, float y1, float width, uint32_t rgba, int blend); /* FUN_00457ae0 */

aos_player *aos_opponent(aos_world *w, const aos_player *p); /* FUN_00780480(side) */
static inline int aos_pidx(const aos_world *w, const aos_player *p) { return (int)(p - w->p); }

/* Shared helpers (address = original). */
void aos_set_state(aos_world *w, aos_player *p, int32_t s);      /* state=s,timer=0,hitFlag=0,T2 */
bool aos_spend_hyper(aos_player *p, float cost);                 /* FUN_00780910 */
bool aos_may_hyper(aos_world *w, aos_player *p);                 /* FUN_00780950 */
bool aos_may_shield(aos_world *w, aos_player *p);                /* FUN_007809e0 */
bool aos_use_gauge(aos_player *p, int32_t state);                /* FUN_007807f0 */
float aos_aim(aos_world *w, aos_player *p);                      /* FUN_007804b0 */
float aos_dist(aos_world *w, aos_player *p);                     /* FUN_00780570 */
float aos_dir_angle(const aos_player *p);                        /* FUN_0061b230(p+0x1c) */
void aos_dash(aos_world *w, aos_player *p, int cancel);          /* FUN_006b17d0 */
void aos_shield(aos_world *w, aos_player *p);                    /* FUN_006b1930(p,0) */
void aos_barrier(aos_world *w, aos_player *p, int kind);         /* FUN_006b1b40 */
bool aos_damage(aos_world *w, aos_player *t, int32_t dmg, int32_t type, float stunAdd); /* FUN_00780ab0 */
void aos_hitstun(aos_world *w, aos_player *p, float angle, float speed);                /* FUN_006b19a0 */
void aos_status(aos_world *w, aos_player *p, int k, int32_t frames);                    /* FUN_006b1a40 */
void aos_heal(aos_player *p, float amount);                                             /* FUN_007808a0 */
void aos_se(aos_world *w, int id);                    /* FUN_007bc9c0 (sound is cosmetic) */
void aos_se_cd(aos_world *w, int id, int cd);           /* FUN_007bcaf0 */
void aos_popup(aos_world *w, int kind, const float pos[3], int32_t owner); /* FUN_0065dff0 */
float aos_rand_real(aos_world *w, float a, float b);  /* FUN_00459fd0 on the game stream */
int32_t aos_rand_int(aos_world *w, int32_t lo, int32_t hi); /* FUN_00459f80 */
/* Standard "hyper/accel" start used by every character's hyper states. */
void aos_hyper_flourish(aos_world *w, aos_player *p);

/* ---- CPU AI helpers (aos_tab.cpu[id][diff], called from aos_tick) ---- */
/* FUN_00459f80 / FUN_00459fd0 as seen by the AI: cosmetic stream (see cpuRng). */
static inline int32_t aos_cpu_rand_int(aos_world *w, int32_t lo, int32_t hi) { return aos_rand_i(&w->cpuRng, lo, hi); }
static inline float aos_cpu_rand_real(aos_world *w, float a, float b) { return aos_rand_f(&w->cpuRng, a, b); }
/* FUN_006b04e0(mask, p, target): add the direction bits that move p toward target (x, y). */
static inline void aos_cpu_steer(int32_t *mask, const aos_player *p, const float t[2])
{
	if (t[0] < p->x)
		*mask |= AOS_IN_LEFT;
	if (p->x < t[0])
		*mask |= AOS_IN_RIGHT;
	if (t[1] < p->y)
		*mask |= AOS_IN_DOWN;
	if (p->y < t[1])
		*mask |= AOS_IN_UP;
}

#endif
