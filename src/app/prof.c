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
#include "audio.h"
#include "../sim/draw.h"
#include "../sim/sim.h"

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
static int nwin, nscen;
static float gpu_acc, gpu_max, sgpu, sgpu_max;
static int dcmd_max, obj_max, sdcmd_max, sobj_max;
static u32 lin_min, heap_min;
static FILE *plog;

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
	log_open();
	if (!plog)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(plog, fmt, ap);
	va_end(ap);
	fputc('\n', plog);
	fflush(plog);
}

static u32 heap_free(void)
{
	struct mallinfo mi = mallinfo();
	return __ctru_heap_size - (u32)mi.arena + (u32)mi.fordblks;
}

void prof_begin(int s) { t0[s] = svcGetSystemTick(); }

static u64 wait_this_frame;

/* Frame capture: if sdmc:/aos2_cap.txt holds "first count", the top-screen
 * frames of world frames first..first+count-1 are written to
 * sdmc:/cap/f<frame>.raw (the render target's raw colour buffer: 240x400
 * RGBA8, 8x8 Morton tiles, rotated; tools/capconv.py makes PNGs). Taken after C3D_FrameBegin,
 * when the previous frame is complete and the target not yet cleared. */
static int cap_first = -1, cap_count, cap_pending = -1;
static bool cap_on;

static void cap_init(void)
{
	if (cap_first != -1)
		return;
	cap_first = -2;
	FILE *f = fopen("sdmc:/aos2_cap.txt", "r");
	if (!f)
		return;
	if (fscanf(f, "%d %d", &cap_first, &cap_count) != 2 || cap_first < 0)
		cap_first = -2;
	fclose(f);
	if (cap_first >= 0) {
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
	FILE *f = fopen(path, "wb");
	if (f) {
		fwrite(t->frameBuf.colorBuf, 1, n, f);
		fclose(f);
	}
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
	if (frame_wall0) {
		u64 d = now - frame_wall0;
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
	u64 cpu = svcGetSystemTick() - frame_t0 - wait_this_frame;
	wait_this_frame = 0;
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
	if (++nwin < WIN)
		return;
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
	panel_line(5, C2D_Color32(180, 255, 180, 255), "lin free %luK heap free %luK vram %luK", (unsigned long)(linearSpaceFree() >> 10),
	           (unsigned long)(heap_free() >> 10), (unsigned long)(vramSpaceFree() >> 10));
	if (plog) {
		fprintf(plog, "win");
		for (int s = 0; s < PROF_N; s++)
			fprintf(plog, " %s %u/%u", names[s], us(acc[s] / WIN), us(mx[s]));
		fprintf(plog, " gpu %.2f/%.2f bgm %u/%u obj %d dcmd %d lin %lu heap %lu\n", (double)(gpu_acc / WIN), (double)gpu_max,
		        us(b / WIN), us(bm), obj_max, dcmd_max, (unsigned long)(linearSpaceFree() >> 10),
		        (unsigned long)(heap_free() >> 10));
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
	nwin = 0;
	nscen++;
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
	nwin = nscen = 0;
	frame_wall0 = 0;
	lin_min = heap_min = 0;
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
	fflush(plog);
}

/* ---------------------------------------------------------------- benchmark */

typedef struct {
	int p1, p2, stage, frames, hyper, diff;
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
				bench_sc s = {0, 0, 0, 0, 0, 2};
				if (line[0] == '#')
					continue;
				if (sscanf(line, "%d %d %d %d %d %d", &s.p1, &s.p2, &s.stage, &s.frames, &s.hyper, &s.diff) >= 4)
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
	prof_note("bench scenario %d: %d vs %d stage %d frames %d hyper %d difficulty %d", cur, sc[cur].p1, sc[cur].p2,
	          sc[cur].stage, sc[cur].frames, sc[cur].hyper, sc[cur].diff);
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
}

bool bench_done(void) { return cur >= 0 && cur < nsc && --frames_left < 0; }
#endif
