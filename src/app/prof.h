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
#define PROF_BEGIN(s) prof_begin(s)
#define PROF_END(s) prof_end(s)
#else
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
