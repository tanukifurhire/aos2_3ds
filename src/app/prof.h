/*
 * On-device profiler + benchmark driver (compile-time option).
 *
 *   PROFILE=1 ./build.sh     -> -DAOS_PROFILE: section timers, memory readout
 *                               on the bottom panel, log in sdmc:/aos2_prof.txt
 *   plain build              -> every macro below compiles to nothing
 *
 * Timers use the ARM11 system tick (268 MHz) and report microseconds as
 * avg / max over 60-frame windows. In an emulator the tick advances per
 * emulated instruction, so emulator numbers are an instruction-count proxy,
 * not real Old 3DS time (cache misses, VFP latency and FCRAM bandwidth are
 * not modelled).
 *
 * Benchmark mode: if sdmc:/aos2_bench.txt exists at start-up (profile build
 * only), the app runs each line "p1 p2 stage frames hyper [difficulty]" as a
 * CPU vs CPU match (hyper 1 = both gauges pinned at 3.0 each frame, which
 * makes the AIs fire hypers as often as they can), logs per-window and
 * per-scenario results to sdmc:/aos2_prof.txt and exits.
 */
#ifndef AOS_PROF_H
#define AOS_PROF_H

#include <stdbool.h>
#include <stdint.h>

enum {
	PROF_TICK,       /* aos_tick */
	PROF_DRAWLIST,   /* aos_draw_world (draw-list build) */
	PROF_STAGE_UPD,  /* stage3ds_tick */
	PROF_STAGE_DRAW, /* stage3ds_draw (command recording/submit) */
	PROF_2D,         /* arena ring + draw-list submit to citro2d */
	PROF_HUD,        /* hud_c2d_draw */
	PROF_AUDIO,      /* SE triggers + BGM/SE frame updates (main thread) */
	PROF_PANEL,      /* bottom-screen panel */
	PROF_FRAMEEND,   /* C3D_FrameEnd (cache flush + GPU kick) */
	PROF_WAIT,       /* C3D_FrameBegin: waiting for vsync / the previous GPU frame */
	PROF_CPU,        /* whole main-thread frame excluding the vsync wait */
	PROF_FRAME,      /* whole frame, wall clock (16.7 ms at 60 FPS) */
	PROF_N
};

struct aos_world;

#ifdef AOS_PROFILE
void prof_begin(int s);
void prof_end(int s);
void prof_frame_start(void);
void prof_frame_done(const struct aos_world *w); /* after C3D_FrameEnd */
void prof_bgm_decode(uint64_t ticks);           /* from the BGM thread */
void prof_note(const char *fmt, ...);           /* line in the log */
void prof_match_begin(const struct aos_world *w, int stage);
void prof_match_end(void);
/* Benchmark mode */
bool bench_active(void);
bool bench_next(int *p1, int *p2, int *stage); /* false = no more scenarios */
void bench_setup(struct aos_world *w);         /* after aos_match_init */
void bench_pre_tick(struct aos_world *w);
bool bench_done(void);                         /* true = this scenario's frames are done */
/* Optional 7th bench column: bit flags that remove one piece of the frame, to see what moves the time. */
enum {
	BENCH_NO_STAGE = 1,     /* skip stage3ds_draw (the update still runs) */
	BENCH_NO_HUD = 2,       /* skip hud_c2d_draw */
	BENCH_NO_SUBMIT = 4,    /* build the draw list but do not submit it to citro2d */
	BENCH_HALF_SPRITES = 8, /* draw every sprite quad at half width/height (1/4 of the fill) */
	BENCH_NO_DRAWLIST = 16, /* skip aos_draw_world and the submit (sim + stage + HUD only) */
	BENCH_ONE_BLEND = 32,   /* (no effect since the 2D batcher, sprbatch.h: blend 1/2 share one GPU state) */
	BENCH_LOW_HP_P1 = 64,   /* test hook: P1 hp pinned to <= 1 (first hit KOs); KO frames logged as "ko" */
	BENCH_LOW_HP_P2 = 128,  /* same for P2 */
};
extern int bench_flags;
/* Per-frame render counters (reset by prof_frame_done). px = screen pixels covered by sprite quads
 * (clipped bounding box), a fill-rate proxy; [0] alpha blend, [1] additive/screen, [2] other. */
typedef struct {
	unsigned quads, tris, texsw, blendsw, stage_draws, stage_verts;
	unsigned invisible; /* draw-list quads skipped as exactly invisible (render.c) */
	float px[3];
} prof_counters;
extern prof_counters prof_cnt;
#define PROF_BEGIN(s) prof_begin(s)
#define PROF_END(s) prof_end(s)
/* Scripted input (sdmc:/aos2_keys.txt, see prof.c): deterministic per-frame key
 * presses, scene waits and two-screen frame dumps for emulator test runs. */
void prof_script_step(void);         /* once per main-loop frame, before input is read */
uint32_t prof_script_keys(void);     /* libctru KEY_* bits held by the script this frame */
bool prof_script_touch(int *x, int *y); /* scripted touch this frame */
int prof_script_decide(void);        /* once after "decide [1|2]": winner 1 = P1, 2 = P2 (Battle) */
void prof_script_frame_end(void);    /* after C3D_FrameEnd: writes requested screenshots */
#else
#define prof_script_step() ((void)0)
#define prof_script_keys() 0u
#define prof_script_touch(x, y) ((void)(x), (void)(y), false)
#define prof_script_decide() 0
#define prof_script_frame_end() ((void)0)
#define PROF_BEGIN(s) ((void)0)
#define PROF_END(s) ((void)0)
#define prof_frame_start() ((void)0)
#define prof_frame_done(w) ((void)0)
#define prof_bgm_decode(t) ((void)0)
#define prof_match_begin(w, st) ((void)0)
#define prof_match_end() ((void)0)
#define bench_active() false
#define bench_setup(w) ((void)0)
#define bench_pre_tick(w) ((void)0)
#define bench_done() false
#endif

#endif
