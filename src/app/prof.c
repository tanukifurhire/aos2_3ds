/* On-device profiler and benchmark driver; see prof.h. Empty unless AOS_PROFILE. */
#include "prof.h"

#ifdef AOS_PROFILE
#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/panel.h"
#include "../platform/render.h"
#include <sys/stat.h>
#include "../platform/spritebank.h"
#include "../platform/sprbatch.h"
#include "audio.h"
#include "../sim/draw.h"
#include "../sim/sim.h"
#include "scene.h"

#define WIN 60
#define TICKS_PER_US (SYSCLOCK_ARM11 / 1000000.0)

extern u32 __ctru_heap_size, __ctru_linear_heap_size;

static const char *const names[PROF_N] = {"tick", "drawlist", "stageUpd", "stageDraw", "2d", "hud",
                                          "audio", "panel", "frameEnd", "wait", "cpu", "frame"};

static u64 t0[PROF_N];
static u64 acc[PROF_N], mx[PROF_N];   /* current window */
static u64 sacc[PROF_N], smx[PROF_N]; /* whole scenario */
static volatile u64 bgm_acc, bgm_max; /* written by the BGM thread */
static u64 sbgm, sbgm_max;
static u64 frame_t0, frame_wall0;
static int nwin, nscen, mframes;
static float gpu_acc, gpu_max, sgpu, sgpu_max;
static int dcmd_max, obj_max, sdcmd_max, sobj_max;
static u32 lin_min, heap_min;
static FILE *plog;
static __thread int heap_depth; /* heap counter (below): inside an outer wrapper / the profiler: don't count */

int bench_flags;
prof_counters prof_cnt;
/* render counters: window sum/max and scenario sum/max; object pools: max per scenario */
/* texsw / blendsw / draws: the 2D batcher's texture changes, blend-state changes and draw calls (sprbatch.h,
 * top + bottom screen); before it, blendsw counted citro2d flushes for blend changes */
enum { RC_QUADS, RC_TRIS, RC_TEXSW, RC_BLENDSW, RC_SDRAWS, RC_SVERTS, RC_PX, RC_PXADD, RC_CMDBUF, RC_DRAWS, RC_INVIS, RC_N };
static const char *const rc_names[RC_N] = {"quads", "tris", "texsw", "blendsw", "stgDraws", "stgVerts", "px", "pxAdd", "cmdbuf%", "draws", "invis"};
static double rc_acc[RC_N], rc_sacc[RC_N];
static float rc_max[RC_N], rc_smax[RC_N];
static float cmdbuf_use;
enum { OB_BEAM, OB_MISSILE, OB_LASER, OB_ENEMY, OB_EFFECT, OB_PARTICLE, OB_SPARK, OB_DCMD, OB_N };
static const char *const ob_names[OB_N] = {"beam", "missile", "laser", "enemy", "effect", "particle", "spark", "dcmd"};
static int ob_max[OB_N];
/* cpu-time histogram (0.5 ms buckets up to 64 ms) for percentiles and frames over budget */
#define HB 128
static unsigned hist[HB], over_cpu, over_frame;

static void log_open(void)
{
	if (!plog) {
		plog = fopen("sdmc:/aos2_prof.txt", "a");
		if (plog)
			setvbuf(plog, NULL, _IOFBF, 4096);
	}
}

void prof_note(const char *fmt, ...)
{
	heap_depth++; /* the profiler's own stdio is not the game's heap traffic */
	log_open();
	if (!plog) {
		heap_depth--;
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vfprintf(plog, fmt, ap);
	va_end(ap);
	fputc('\n', plog);
	fflush(plog);
	heap_depth--;
}

/* ---- heap traffic counter: the match loop must not allocate ----
 * Profile builds link with -Wl,--wrap for newlib's malloc family (both the public calls and the reentrant
 * _r functions newlib itself uses, e.g. fopen) and libctru's linear / VRAM allocators (build.sh). Every call
 * bumps prof_heap_ops; prof_frame_done logs each match frame (after the first two: lazy one-time setup) that
 * made any, with the first caller's return address (look it up in aos2_3ds.map / addr2line), and the
 * scenario summary reports "heapFrames" / "heapOps" (both must be 0). Only the main (game) thread counts;
 * other threads (the BGM stream thread opens its Ogg track with ~600 Tremor allocations a few frames into
 * every match) are reported separately as "heapOther". The profiler's own logging is excluded (newlib's
 * printf of floats allocates its dtoa buffers lazily). */
volatile unsigned prof_heap_ops, prof_heap_other;
static volatile u32 heap_first_lr;
static void *main_tls;           /* the game thread (set from the main thread in prof_frame_start) */

static void heap_note(void *lr)
{
	if (heap_depth)
		return;
	if (main_tls && getThreadLocalStorage() != main_tls) {
		prof_heap_other++;
		return;
	}
	if (!heap_first_lr)
		heap_first_lr = (u32)lr;
	prof_heap_ops++;
}

#define HEAP_OUTER(lr, call) \
	do { \
		heap_note(lr); \
		heap_depth++; \
		call; \
		heap_depth--; \
	} while (0)

struct _reent;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__real_memalign(size_t, size_t);
void __real_free(void *);
void *__real__malloc_r(struct _reent *, size_t);
void *__real__realloc_r(struct _reent *, void *, size_t);
void *__real__memalign_r(struct _reent *, size_t, size_t);
void __real__free_r(struct _reent *, void *);
void *__real_linearMemAlign(size_t, size_t);
void *__real_linearAlloc(size_t);
void *__real_linearRealloc(void *, size_t);
void __real_linearFree(void *);
void *__real_vramAlloc(size_t);
void *__real_vramAllocAt(size_t, int);
void *__real_vramMemAlign(size_t, size_t);
void *__real_vramMemAlignAt(size_t, size_t, int);
void __real_vramFree(void *);
void *__wrap_malloc(size_t);
void *__wrap_calloc(size_t, size_t);
void *__wrap_realloc(void *, size_t);
void *__wrap_memalign(size_t, size_t);
void __wrap_free(void *);
void *__wrap__malloc_r(struct _reent *, size_t);
void *__wrap__realloc_r(struct _reent *, void *, size_t);
void *__wrap__memalign_r(struct _reent *, size_t, size_t);
void __wrap__free_r(struct _reent *, void *);
void *__wrap_linearMemAlign(size_t, size_t);
void *__wrap_linearAlloc(size_t);
void *__wrap_linearRealloc(void *, size_t);
void __wrap_linearFree(void *);
void *__wrap_vramAlloc(size_t);
void *__wrap_vramAllocAt(size_t, int);
void *__wrap_vramMemAlign(size_t, size_t);
void *__wrap_vramMemAlignAt(size_t, size_t, int);
void __wrap_vramFree(void *);

void *__wrap_malloc(size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_malloc(n)); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_calloc(a, b)); return p; }
void *__wrap_realloc(void *q, size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_realloc(q, n)); return p; }
void *__wrap_memalign(size_t a, size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_memalign(a, n)); return p; }
void __wrap_free(void *q)
{
	if (q)
		HEAP_OUTER(__builtin_return_address(0), __real_free(q));
}
void *__wrap__malloc_r(struct _reent *r, size_t n) { heap_note(__builtin_return_address(0)); return __real__malloc_r(r, n); }
void *__wrap__realloc_r(struct _reent *r, void *q, size_t n) { heap_note(__builtin_return_address(0)); return __real__realloc_r(r, q, n); }
void *__wrap__memalign_r(struct _reent *r, size_t a, size_t n) { heap_note(__builtin_return_address(0)); return __real__memalign_r(r, a, n); }
void __wrap__free_r(struct _reent *r, void *q)
{
	if (q)
		heap_note(__builtin_return_address(0));
	__real__free_r(r, q);
}
void *__wrap_linearMemAlign(size_t n, size_t a) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_linearMemAlign(n, a)); return p; }
void *__wrap_linearAlloc(size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_linearAlloc(n)); return p; }
void *__wrap_linearRealloc(void *q, size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_linearRealloc(q, n)); return p; }
void __wrap_linearFree(void *q)
{
	if (q)
		HEAP_OUTER(__builtin_return_address(0), __real_linearFree(q));
}
void *__wrap_vramAlloc(size_t n) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_vramAlloc(n)); return p; }
void *__wrap_vramAllocAt(size_t n, int pos) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_vramAllocAt(n, pos)); return p; }
void *__wrap_vramMemAlign(size_t n, size_t a) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_vramMemAlign(n, a)); return p; }
void *__wrap_vramMemAlignAt(size_t n, size_t a, int pos) { void *p; HEAP_OUTER(__builtin_return_address(0), p = __real_vramMemAlignAt(n, a, pos)); return p; }
void __wrap_vramFree(void *q)
{
	if (q)
		HEAP_OUTER(__builtin_return_address(0), __real_vramFree(q));
}

static unsigned heap_seen, heap_frames, heap_ops_match, heap_logged, heap_other0;

/* once per match frame (prof_frame_done) */
static void heap_check(int mframe)
{
	unsigned n = prof_heap_ops, d = n - heap_seen;
	u32 lr = heap_first_lr;
	heap_seen = n;
	heap_first_lr = 0;
	if (!d || mframe <= 2)
		return; /* the first frames: one-time lazy setup (stage caches, stdio) */
	heap_frames++;
	heap_ops_match += d;
	if (heap_logged < 32) {
		heap_logged++;
		prof_note("heap: %u allocator calls in match frame %d (first caller %08lx)", d, mframe, (unsigned long)lr);
	}
}

static u32 heap_free(void)
{
	struct mallinfo mi = mallinfo();
	return __ctru_heap_size - (u32)mi.arena + (u32)mi.fordblks;
}

void prof_begin(int s)
{
	if (s == PROF_FRAMEEND)
		cmdbuf_use = C3D_GetCmdBufUsage();
	t0[s] = svcGetSystemTick();
}

static u64 wait_this_frame;

/* Frame capture: if sdmc:/aos2_cap.txt holds "first count", the top-screen
 * frames of world frames first..first+count-1 are written to
 * sdmc:/cap/f<frame>.raw (the render target's raw colour buffer: 240x400
 * RGBA8, 8x8 Morton tiles, rotated; tools/capconv.py makes PNGs). Taken after C3D_FrameBegin,
 * when the previous frame is complete and the target not yet cleared. */
static int cap_first = -1, cap_count, cap_pending = -1;
static bool cap_on;
/* More lines "m first count" capture by the frame count since the match started (all rounds; the
 * benchmark runs the same deterministic match, so these are the same scene in every build). */
#define CAP_M 16
static int capm_first[CAP_M], capm_count[CAP_M], ncapm;

static void cap_init(void)
{
	if (cap_first != -1)
		return;
	cap_first = -2;
	FILE *f = fopen("sdmc:/aos2_cap.txt", "r");
	if (!f)
		return;
	char line[64];
	while (fgets(line, sizeof line, f)) {
		int a, b;
		if (line[0] == 'm') {
			if (ncapm < CAP_M && sscanf(line + 1, "%d %d", &a, &b) == 2)
				capm_first[ncapm] = a, capm_count[ncapm++] = b;
		} else if (cap_first < 0 && sscanf(line, "%d %d", &a, &b) == 2 && a >= 0) {
			cap_first = a;
			cap_count = b;
		}
	}
	fclose(f);
	if (cap_first >= 0 || ncapm) {
		mkdir("sdmc:/cap", 0777);
		cap_on = true;
	}
}

static void cap_grab(void)
{
	C3D_RenderTarget *t = render_last_target();
	if (cap_pending < 0 || !cap_on || !t)
		return;
	size_t n = (size_t)t->frameBuf.width * t->frameBuf.height * 4;
	GSPGPU_InvalidateDataCache(t->frameBuf.colorBuf, n);
	char path[48];
	snprintf(path, sizeof path, "sdmc:/cap/f%06d.raw", cap_pending);
	heap_depth++;
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(t->frameBuf.colorBuf, 1, n, f);
		fclose(f);
	}
	heap_depth--;
	cap_pending = -1;
}

void prof_end(int s)
{
	if (s == PROF_WAIT)
		cap_grab();
	u64 d = svcGetSystemTick() - t0[s];
	if (s == PROF_WAIT)
		wait_this_frame += d;
	acc[s] += d;
	if (d > mx[s])
		mx[s] = d;
}

void prof_bgm_decode(uint64_t ticks)
{
	bgm_acc += ticks;
	if (ticks > bgm_max)
		bgm_max = ticks;
}

void prof_frame_start(void)
{
	u64 now = svcGetSystemTick();
	if (!main_tls)
		main_tls = getThreadLocalStorage();
	if (frame_wall0) {
		u64 d = now - frame_wall0;
		if (d > (u64)(17500 * TICKS_PER_US))
			over_frame++;
		acc[PROF_FRAME] += d;
		if (d > mx[PROF_FRAME])
			mx[PROF_FRAME] = d;
	}
	frame_wall0 = now;
	frame_t0 = now;
}

static unsigned us(u64 t) { return (unsigned)(t / TICKS_PER_US); }

static void mem_sample(void)
{
	u32 l = linearSpaceFree(), h = heap_free();
	if (!lin_min || l < lin_min)
		lin_min = l;
	if (!heap_min || h < heap_min)
		heap_min = h;
}

void prof_frame_done(const struct aos_world *cw)
{
	const aos_world *w = cw;
	cap_init();
	if (cap_first >= 0 && (int)w->frame >= cap_first && (int)w->frame < cap_first + cap_count)
		cap_pending = (int)w->frame;
	for (int i = 0; i < ncapm; i++)
		if (mframes + 1 >= capm_first[i] && mframes + 1 < capm_first[i] + capm_count[i])
			cap_pending = 1000000 + mframes + 1; /* f1xxxxxx.raw = match frame xxxxxx */
	u64 cpu = svcGetSystemTick() - frame_t0 - wait_this_frame;
	wait_this_frame = 0;
	heap_check(mframes + 1);
	if (++mframes == 30) { /* maxima exclude the match load and texture uploads of the first frames */
		memset(mx, 0, sizeof mx);
		memset(smx, 0, sizeof smx);
		memset(hist, 0, sizeof hist);
		over_cpu = over_frame = 0;
	}
	acc[PROF_CPU] += cpu;
	if (cpu > mx[PROF_CPU])
		mx[PROF_CPU] = cpu;
	float g = C3D_GetProcessingTime(); /* ms, CPU side of the previous frame per citro3d */
	float gd = C3D_GetDrawingTime();   /* ms, GPU drawing of the previous frame */
	(void)g;
	gpu_acc += gd;
	if (gd > gpu_max)
		gpu_max = gd;
	int objs = w->obj.nbeam + w->obj.nmissile + w->obj.nlaser + w->obj.nenemy + w->obj.neffect + w->obj.nwhip +
	           w->obj.nparticle + w->obj.nspark;
	if (objs > obj_max)
		obj_max = objs;
	if (aos_dl.n > dcmd_max)
		dcmd_max = aos_dl.n;
	{
		float v[RC_N] = {(float)prof_cnt.quads, (float)prof_cnt.tris, (float)spr_stats.texsw, (float)spr_stats.statesw,
		                 (float)prof_cnt.stage_draws, (float)prof_cnt.stage_verts,
		                 prof_cnt.px[0] + prof_cnt.px[1] + prof_cnt.px[2], prof_cnt.px[1], cmdbuf_use * 100.0f,
		                 (float)spr_stats.draws, (float)prof_cnt.invisible};
		memset(&spr_stats, 0, sizeof spr_stats);
		for (int k = 0; k < RC_N; k++) {
			rc_acc[k] += v[k];
			if (v[k] > rc_max[k])
				rc_max[k] = v[k];
		}
		memset(&prof_cnt, 0, sizeof prof_cnt);
		const int ob[OB_N] = {w->obj.nbeam, w->obj.nmissile, w->obj.nlaser, w->obj.nenemy, w->obj.neffect,
		                      w->obj.nparticle, w->obj.nspark, aos_dl.n};
		for (int k = 0; k < OB_N; k++)
			if (ob[k] > ob_max[k])
				ob_max[k] = ob[k];
		unsigned cu = us(cpu);
		hist[cu / 500 < HB ? cu / 500 : HB - 1]++;
		if (cu > 16667)
			over_cpu++;
	}
	if (++nwin < WIN)
		return;
	heap_depth++; /* window log + panel lines: profiler-only stdio */
	mem_sample();
	u64 b = bgm_acc, bm = bgm_max;
	bgm_acc = bgm_max = 0;
	/* bottom panel: 3 lines of 64 chars */
	panel_line(3, C2D_Color32(180, 255, 180, 255), "tick %u/%u dl %u/%u stg %u+%u 2d %u hud %u", us(acc[PROF_TICK] / WIN),
	           us(mx[PROF_TICK]), us(acc[PROF_DRAWLIST] / WIN), us(mx[PROF_DRAWLIST]), us(acc[PROF_STAGE_UPD] / WIN),
	           us(acc[PROF_STAGE_DRAW] / WIN), us(acc[PROF_2D] / WIN), us(acc[PROF_HUD] / WIN));
	panel_line(4, C2D_Color32(180, 255, 180, 255), "cpu %u/%u frm %u/%u gpu %.1f/%.1f bgm %u%% obj %d dc %d",
	           us(acc[PROF_CPU] / WIN), us(mx[PROF_CPU]), us(acc[PROF_FRAME] / WIN), us(mx[PROF_FRAME]),
	           (double)(gpu_acc / WIN), (double)gpu_max, (unsigned)(b * 100 / (acc[PROF_FRAME] ? acc[PROF_FRAME] : 1)),
	           obj_max, dcmd_max);
	panel_line(5, heap_frames ? C2D_Color32(255, 120, 120, 255) : C2D_Color32(180, 255, 180, 255),
	           "lin free %luK heap free %luK vram %luK allocfr %u", (unsigned long)(linearSpaceFree() >> 10),
	           (unsigned long)(heap_free() >> 10), (unsigned long)(vramSpaceFree() >> 10), heap_frames);
	if (plog) {
		fprintf(plog, "win");
		for (int s = 0; s < PROF_N; s++)
			fprintf(plog, " %s %u/%u", names[s], us(acc[s] / WIN), us(mx[s]));
		fprintf(plog, " gpu %.2f/%.2f bgm %u/%u obj %d dcmd %d lin %lu heap %lu\n", (double)(gpu_acc / WIN), (double)gpu_max,
		        us(b / WIN), us(bm), obj_max, dcmd_max, (unsigned long)(linearSpaceFree() >> 10),
		        (unsigned long)(heap_free() >> 10));
		fprintf(plog, "rc");
		for (int k = 0; k < RC_N; k++)
			fprintf(plog, " %s %.0f/%.0f", rc_names[k], rc_acc[k] / WIN, (double)rc_max[k]);
		fprintf(plog, " wf %d hp %d/%d\n", (int)w->frame, (int)w->p[0].hp, (int)w->p[1].hp);
	}
	for (int s = 0; s < PROF_N; s++) {
		sacc[s] += acc[s];
		if (mx[s] > smx[s])
			smx[s] = mx[s];
		acc[s] = mx[s] = 0;
	}
	sbgm += b;
	if (bm > sbgm_max)
		sbgm_max = bm;
	sgpu += gpu_acc;
	if (gpu_max > sgpu_max)
		sgpu_max = gpu_max;
	if (obj_max > sobj_max)
		sobj_max = obj_max;
	if (dcmd_max > sdcmd_max)
		sdcmd_max = dcmd_max;
	gpu_acc = gpu_max = 0;
	obj_max = dcmd_max = 0;
	for (int k = 0; k < RC_N; k++) {
		rc_sacc[k] += rc_acc[k];
		if (rc_max[k] > rc_smax[k])
			rc_smax[k] = rc_max[k];
		rc_acc[k] = rc_max[k] = 0;
	}
	nwin = 0;
	nscen++;
	heap_depth--;
}

void prof_match_begin(const struct aos_world *cw, int stage)
{
	const aos_world *w = cw;
	log_open();
	memset(acc, 0, sizeof acc);
	memset(mx, 0, sizeof mx);
	memset(sacc, 0, sizeof sacc);
	memset(smx, 0, sizeof smx);
	sbgm = sbgm_max = 0;
	sgpu = sgpu_max = 0;
	sobj_max = sdcmd_max = 0;
	nwin = nscen = mframes = 0;
	frame_wall0 = 0;
	lin_min = heap_min = 0;
	memset(rc_acc, 0, sizeof rc_acc);
	memset(rc_sacc, 0, sizeof rc_sacc);
	memset(rc_max, 0, sizeof rc_max);
	memset(rc_smax, 0, sizeof rc_smax);
	memset(ob_max, 0, sizeof ob_max);
	memset(hist, 0, sizeof hist);
	over_cpu = over_frame = 0;
	memset(&prof_cnt, 0, sizeof prof_cnt);
	heap_seen = prof_heap_ops;
	heap_other0 = prof_heap_other;
	heap_first_lr = 0;
	heap_frames = heap_ops_match = heap_logged = 0;
	mem_sample();
	prof_note("match %d vs %d stage %d: heap %lu KB (free %lu KB), linear %lu KB (free %lu KB), vram free %lu KB, "
	          "sizeof(aos_world) %u, sprite textures %lu KB linear + %lu KB VRAM, SE PCM %lu KB",
	          (int)w->p[0].id, (int)w->p[1].id, stage, (unsigned long)(__ctru_heap_size >> 10),
	          (unsigned long)(heap_free() >> 10), (unsigned long)(__ctru_linear_heap_size >> 10),
	          (unsigned long)(linearSpaceFree() >> 10), (unsigned long)(vramSpaceFree() >> 10), (unsigned)sizeof(aos_world),
	          (unsigned long)(spritebank_bytes(false) >> 10), (unsigned long)(spritebank_bytes(true) >> 10),
	          (unsigned long)(audio_se_bytes() >> 10));
}

void prof_match_end(void)
{
	if (!plog || !nscen)
		return;
	u64 n = (u64)nscen * WIN;
	fprintf(plog, "summary frames %u:", (unsigned)n);
	for (int s = 0; s < PROF_N; s++)
		fprintf(plog, " %s %u/%u", names[s], us(sacc[s] / n), us(smx[s]));
	fprintf(plog, " gpu %.2f/%.2f bgm %u/%u objmax %d dcmdmax %d linmin %lu heapmin %lu sepcm %lu\n", (double)(sgpu / (float)n),
	        (double)sgpu_max, us(sbgm / n), us(sbgm_max), sobj_max, sdcmd_max, (unsigned long)(lin_min >> 10),
	        (unsigned long)(heap_min >> 10), (unsigned long)(audio_se_bytes() >> 10));
	fprintf(plog, "summary2 flags %d vramfree %lu", bench_flags, (unsigned long)(vramSpaceFree() >> 10));
	for (int k = 0; k < RC_N; k++)
		fprintf(plog, " %s %.0f/%.0f", rc_names[k], rc_sacc[k] / (double)n, (double)rc_smax[k]);
	for (int k = 0; k < OB_N; k++)
		fprintf(plog, " %smax %d", ob_names[k], ob_max[k]);
	/* cpu percentiles from the histogram (bucket upper bound, us) */
	unsigned tot = 0;
	for (int i = 0; i < HB; i++)
		tot += hist[i];
	const double q[3] = {0.5, 0.95, 0.99};
	for (int j = 0; j < 3; j++) {
		unsigned run = 0;
		int i = 0;
		for (; i < HB - 1; i++) {
			run += hist[i];
			if (run >= q[j] * tot)
				break;
		}
		fprintf(plog, " cpuP%d %u", (int)(q[j] * 100), (unsigned)(i + 1) * 500);
	}
	fprintf(plog, " cpuOver16.7 %u framesOver17.5 %u heapFrames %u heapOps %u heapOther %u\n", over_cpu, over_frame, heap_frames,
	        heap_ops_match, prof_heap_other - heap_other0);
	fflush(plog);
}

/* ---------------------------------------------------------------- benchmark */

typedef struct {
	int p1, p2, stage, frames, hyper, diff, flags;
} bench_sc;

static bench_sc sc[32];
static int nsc = -1, cur = -1, frames_left;

bool bench_active(void)
{
	if (nsc < 0) {
		nsc = 0;
		FILE *f = fopen("sdmc:/aos2_bench.txt", "r");
		if (f) {
			char line[128];
			while (nsc < 32 && fgets(line, sizeof line, f)) {
				bench_sc s = {0, 0, 0, 0, 0, 2, 0};
				if (line[0] == '#')
					continue;
				if (sscanf(line, "%d %d %d %d %d %d %d", &s.p1, &s.p2, &s.stage, &s.frames, &s.hyper, &s.diff, &s.flags) >= 4)
					sc[nsc++] = s;
			}
			fclose(f);
			prof_note("bench: %d scenarios", nsc);
		}
	}
	return nsc > 0;
}

bool bench_next(int *p1, int *p2, int *stage)
{
	if (!bench_active() || ++cur >= nsc) {
		prof_note("bench: done");
		return false;
	}
	*p1 = sc[cur].p1;
	*p2 = sc[cur].p2;
	*stage = sc[cur].stage;
	frames_left = sc[cur].frames;
	bench_flags = sc[cur].flags;
	prof_note("bench scenario %d: %d vs %d stage %d frames %d hyper %d difficulty %d flags %d", cur, sc[cur].p1, sc[cur].p2,
	          sc[cur].stage, sc[cur].frames, sc[cur].hyper, sc[cur].diff, sc[cur].flags);
	return true;
}

void bench_setup(struct aos_world *w)
{
	if (cur < 0 || cur >= nsc)
		return;
	w->p[0].inputSrc = w->p[1].inputSrc = 10;
	w->difficulty = sc[cur].diff;
}

void bench_pre_tick(struct aos_world *w)
{
	if (cur >= 0 && cur < nsc && sc[cur].hyper)
		w->p[0].hyper = w->p[1].hyper = 3.0f;
	for (int i = 0; i < 2 && cur >= 0 && cur < nsc; i++) {
		aos_player *p = &w->p[i];
		if ((bench_flags & (BENCH_LOW_HP_P1 << i)) && p->hp > 1)
			p->hp = 1;
		if ((bench_flags & (BENCH_LOW_HP_P1 << i)) && p->state == AOS_ST_KO && p->timer == 0)
			prof_note("ko p%d round frame %d match frame %d\n", i + 1, (int)w->frame, mframes);
	}
}

bool bench_done(void) { return cur >= 0 && cur < nsc && --frames_left < 0; }

/* ---------------------------------------------------------------- scripted input
 * sdmc:/aos2_keys.txt (profile builds only): one command per line, run in order, one
 * main-loop frame at a time, so runs are repeatable (window key presses can be lost):
 *   wait N              idle N frames
 *   scene ID [max]      wait until scene ID is current (gives up after max frames, default 3600)
 *   key K[+K..] [h] [g] hold the keys h frames (default 4), then release g frames (default 6)
 *                       K = A B X Y L R UP DOWN LEFT RIGHT START SELECT
 *   hold K[+K..] N      hold for N frames, no release gap
 *   touch X Y [h]       touch the bottom screen at (X, Y) for h frames (default 4) + 6 released
 *   touchdown X Y       keep touching (X, Y) through the following commands (e.g. a shot of the pressed state)
 *   touchup             release it
 *   shot NAME           dump this frame's top + bottom targets to sdmc:/cap/NAME_{top,bot}.raw
 *                       (tools/capconv.py)
 *   decide [W]          Battle: end the match now, W = 1 P1 wins (default) / 2 P2 wins (main.c fake record)
 *   skipif ID N         skip the next N commands while scene ID is current
 *   quit                leave the app
 * '#' starts a comment. Each executed command is logged to sdmc:/cap/script_log.txt. */
#define SCR_MAX 512
typedef struct {
	char op;   /* w s k h t p d q */
	int a, b, c;
	uint32_t keys;
	char name[40];
} scr_cmd;
static scr_cmd scr[SCR_MAX];
static int scr_n = -1, scr_pc, scr_frame, scr_left, scr_phase, scr_wait_scene;
static uint32_t scr_keys;
static int scr_tx, scr_ty;
static bool scr_touch;
static bool scr_touch_hold; /* touchdown .. touchup */
static int scr_hx, scr_hy;
static int scr_decide;
static char scr_shot[40];
static FILE *scr_log;

static uint32_t scr_keymask(const char *t)
{
	static const struct {
		const char *n;
		uint32_t k;
	} m[] = {{"A", KEY_A}, {"B", KEY_B}, {"X", KEY_X}, {"Y", KEY_Y}, {"L", KEY_L}, {"R", KEY_R},
	         {"UP", KEY_DUP}, {"DOWN", KEY_DDOWN}, {"LEFT", KEY_DLEFT}, {"RIGHT", KEY_DRIGHT},
	         {"START", KEY_START}, {"SELECT", KEY_SELECT}};
	uint32_t k = 0;
	char buf[64];
	snprintf(buf, sizeof buf, "%s", t);
	for (char *p = strtok(buf, "+"); p; p = strtok(NULL, "+"))
		for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
			if (!strcmp(p, m[i].n))
				k |= m[i].k;
	return k;
}

static void scr_load(void)
{
	scr_n = 0;
	FILE *f = fopen("sdmc:/aos2_keys.txt", "r");
	if (!f)
		return;
	char line[128];
	while (scr_n < SCR_MAX && fgets(line, sizeof line, f)) {
		char op[16] = "", a1[40] = "";
		int x = 0, y = 0, z = 0;
		char *h = strchr(line, '#');
		if (h)
			*h = 0;
		int n = sscanf(line, "%15s %39s %d %d", op, a1, &y, &z);
		if (n < 1)
			continue;
		scr_cmd c = {0};
		if (!strcmp(op, "wait") && n >= 2)
			c = (scr_cmd){.op = 'w', .a = atoi(a1)};
		else if (!strcmp(op, "scene") && n >= 2)
			c = (scr_cmd){.op = 's', .a = atoi(a1), .b = n >= 3 ? y : 3600};
		else if (!strcmp(op, "key") && n >= 2)
			c = (scr_cmd){.op = 'k', .keys = scr_keymask(a1), .a = n >= 3 ? y : 4, .b = n >= 4 ? z : 6};
		else if (!strcmp(op, "hold") && n >= 3)
			c = (scr_cmd){.op = 'k', .keys = scr_keymask(a1), .a = y, .b = 0};
		else if (!strcmp(op, "touchdown") && n >= 3)
			c = (scr_cmd){.op = 'T', .a = atoi(a1), .b = y};
		else if (!strcmp(op, "touchup"))
			c.op = 'U';
		else if (!strcmp(op, "touch") && n >= 3) {
			x = atoi(a1);
			c = (scr_cmd){.op = 't', .a = x, .b = y, .c = n >= 4 ? z : 4};
		} else if (!strcmp(op, "shot") && n >= 2) {
			c.op = 'p';
			snprintf(c.name, sizeof c.name, "%s", a1);
		} else if (!strcmp(op, "skipif") && n >= 3)
			c = (scr_cmd){.op = 'i', .a = atoi(a1), .b = y};
		else if (!strcmp(op, "decide"))
			c = (scr_cmd){.op = 'd', .a = n >= 2 && atoi(a1) == 2 ? 2 : 1};
		else if (!strcmp(op, "quit"))
			c.op = 'q';
		else
			continue;
		scr[scr_n++] = c;
	}
	fclose(f);
	if (scr_n) {
		mkdir("sdmc:/cap", 0777);
		scr_log = fopen("sdmc:/cap/script_log.txt", "w");
	}
}

static void scr_note(const char *what)
{
	if (!scr_log)
		return;
	fprintf(scr_log, "frame %d scene %d pc %d: %s\n", scr_frame, scene_current(), scr_pc, what);
	fflush(scr_log);
}

void prof_script_step(void)
{
	if (scr_n < 0)
		scr_load();
	scr_keys = 0;
	scr_touch = scr_touch_hold;
	if (scr_touch_hold) {
		scr_tx = scr_hx;
		scr_ty = scr_hy;
	}
	scr_frame++;
	while (scr_pc < scr_n) {
		scr_cmd *c = &scr[scr_pc];
		switch (c->op) {
		case 'w':
			if (scr_phase == 0) {
				scr_left = c->a;
				scr_phase = 1;
			}
			if (scr_left-- > 0)
				return;
			break;
		case 's':
			if (scene_current() == c->a) {
				scr_note("scene reached");
				break;
			}
			if (++scr_wait_scene < c->b)
				return;
			scr_note("scene wait timed out");
			break;
		case 'k':
		case 't':
			if (scr_phase == 0) {
				scr_left = c->op == 'k' ? c->a : c->c;
				scr_phase = 1;
				scr_note(c->op == 'k' ? "key" : "touch");
			}
			if (scr_phase == 1) {
				if (scr_left-- > 0) {
					if (c->op == 'k')
						scr_keys = c->keys;
					else {
						scr_touch = true;
						scr_tx = c->a;
						scr_ty = c->b;
					}
					return;
				}
				scr_phase = 2;
				scr_left = c->op == 'k' ? c->b : 6;
			}
			if (scr_left-- > 0)
				return;
			break;
		case 'p':
			snprintf(scr_shot, sizeof scr_shot, "%s", c->name);
			scr_note(c->name);
			scr_pc++;
			scr_phase = 0;
			scr_wait_scene = 0;
			return; /* the dump happens after this frame is drawn */
		case 'T':
			scr_touch_hold = true;
			scr_hx = c->a;
			scr_hy = c->b;
			scr_touch = true;
			scr_tx = c->a;
			scr_ty = c->b;
			scr_note("touchdown");
			break;
		case 'U':
			scr_touch_hold = false;
			scr_note("touchup");
			break;
		case 'd':
			scr_decide = c->a;
			scr_note("decide");
			break;
		case 'i':
			if (scene_current() == c->a) {
				scr_note("skipif");
				scr_pc += c->b;
			}
			break;
		case 'q':
			scr_note("quit");
			scene_goto(SCENE_QUIT);
			break;
		}
		scr_pc++;
		scr_phase = 0;
		scr_wait_scene = 0;
	}
}

uint32_t prof_script_keys(void) { return scr_keys; }

bool prof_script_touch(int *x, int *y)
{
	if (!scr_touch)
		return false;
	*x = scr_tx;
	*y = scr_ty;
	return true;
}

int prof_script_decide(void)
{
	int d = scr_decide;
	scr_decide = 0;
	return d;
}

static void scr_dump(C3D_RenderTarget *t, const char *path)
{
	if (!t)
		return;
	size_t n = (size_t)t->frameBuf.width * t->frameBuf.height * 4;
	GSPGPU_InvalidateDataCache(t->frameBuf.colorBuf, n);
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(t->frameBuf.colorBuf, 1, n, f);
		fclose(f);
	}
}

void prof_script_frame_end(void)
{
	if (!scr_shot[0])
		return;
	C3D_FrameBegin(0); /* waits until the GPU finished this frame */
	char path[80];
	snprintf(path, sizeof path, "sdmc:/cap/%s_top.raw", scr_shot);
	scr_dump(scene_top(), path);
	snprintf(path, sizeof path, "sdmc:/cap/%s_bot.raw", scr_shot);
	scr_dump(scene_bottom(), path);
	C3D_FrameEnd(0);
	scr_shot[0] = 0;
}
#endif
