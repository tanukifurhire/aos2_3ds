#include "bgm.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

#include "../app/prof.h"

#define CHANNEL 23
#define NBUF 3
#define BUF_SAMPLES 4096 /* per channel; ~93 ms at 44.1 kHz */
#define BGM_SYSCORE_LIMIT 40 /* % of the system core granted to the app (APT_SetAppCpuTimeLimit) */

/* Loop start points from the original BGM table (FUN_004bdc80, stores to
 * 0x87a560+12*id). Every other track loops from the beginning. Units are
 * PCM samples (UNVERIFIED: Ayame's unit — 942005 samples = 21.4 s). */
static ogg_int64_t loop_start(int id)
{
	switch (id) {
	case 22: return 942005;
	case 23: return 2722597;
	default: return 0;
	}
}

static OggVorbis_File vf;
static bool vf_open;
static int cur_id = -1;
static int channels = 2;
static s16 *pcm[NBUF];
static ndspWaveBuf wb[NBUF];
static Thread thread;
static LightEvent ev;
static LightLock lock;
static volatile bool quit;
static bool inited;
static bool loop = true; /* false = play once (Ayame method +4), true = loop (+8) */
static float volume = 1.0f;

/* Decode one buffer worth of samples; loops at EOF. Caller holds lock. */
static u32 fill(s16 *dst)
{
	u32 want = BUF_SAMPLES * (u32)channels * 2, got = 0;
	int section, guard = 0;
	while (got < want) {
		long n = ov_read(&vf, (char *)dst + got, (int)(want - got), &section);
		if (n > 0) {
			got += (u32)n;
			guard = 0;
		} else if (n == 0) { /* EOF -> loop, or end of a play-once track */
			if (!loop || ov_pcm_seek(&vf, loop_start(cur_id)) != 0 || ++guard > 2)
				break;
		} else if (++guard > 8) { /* decode errors: give up on this buffer */
			break;
		}
	}
	return got / ((u32)channels * 2);
}

static void queue(int i)
{
#ifdef AOS_PROFILE
	u64 t0 = svcGetSystemTick();
	u32 n = fill(pcm[i]);
	prof_bgm_decode(svcGetSystemTick() - t0);
#else
	u32 n = fill(pcm[i]);
#endif
	if (!n)
		return;
	DSP_FlushDataCache(pcm[i], n * (u32)channels * 2);
	wb[i].nsamples = n;
	ndspChnWaveBufAdd(CHANNEL, &wb[i]);
}

/* A track change: opening the Ogg (Tremor scans a seekable file for its
 * links) and decoding the first 3 buffers took ~100 ms on the game thread
 * (profiled as a hitch at every round/match start), so bgm_play_ex only
 * checks the file and hands it to the stream thread, which does this. */
static FILE *pending_f;
static int pending_id = -1;
static bool pending_loop;

static void open_pending_locked(void)
{
	FILE *f = pending_f;
	int id = pending_id;
	pending_f = NULL;
	pending_id = -1;
	if (ov_open(f, &vf, NULL, 0) != 0) { /* ov_open owns f on success */
		fclose(f);
		cur_id = -1;
		return;
	}
	vorbis_info *vi = ov_info(&vf, -1);
	channels = vi->channels == 1 ? 1 : 2;
	vf_open = true;
	cur_id = id;
	loop = pending_loop;
	ndspChnReset(CHANNEL);
	ndspChnSetInterp(CHANNEL, NDSP_INTERP_POLYPHASE);
	ndspChnSetRate(CHANNEL, (float)vi->rate);
	ndspChnSetFormat(CHANNEL, channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {0};
	mix[0] = mix[1] = volume;
	ndspChnSetMix(CHANNEL, mix);
	memset(wb, 0, sizeof wb);
	for (int i = 0; i < NBUF; i++) {
		wb[i].data_vaddr = pcm[i];
		queue(i);
	}
}

static void stream_thread(void *arg)
{
	(void)arg;
	while (!quit) {
		LightLock_Lock(&lock);
		if (pending_f)
			open_pending_locked();
		else if (vf_open)
			for (int i = 0; i < NBUF; i++)
				if (wb[i].status == NDSP_WBUF_DONE)
					queue(i);
		LightLock_Unlock(&lock);
		LightEvent_Wait(&ev);
	}
}

static void ndsp_cb(void *arg)
{
	(void)arg;
	LightEvent_Signal(&ev);
}

bool bgm_init(void)
{
	if (inited)
		return true;
	for (int i = 0; i < NBUF; i++) {
		pcm[i] = linearAlloc(BUF_SAMPLES * 2 * sizeof(s16));
		if (!pcm[i])
			return false;
	}
	LightEvent_Init(&ev, RESET_ONESHOT);
	LightLock_Init(&lock);
	ndspSetCallback(ndsp_cb, NULL);
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	quit = false;
	/* Tremor decoding costs ~3 ms per 60 Hz frame on an Old 3DS (profiled:
	 * ~20% of the 268 MHz app core), so keep it off the game loop's core:
	 * New 3DS core 2 if we may use it, else the system core (core 1) with an
	 * app time limit, else the app core as before. 3 x 93 ms of buffered PCM
	 * absorb the system core's coarser scheduling. */
	bool n3ds = false;
	APT_CheckNew3DS(&n3ds);
	if (n3ds)
		thread = threadCreate(stream_thread, NULL, 32 * 1024, prio - 1, 2, false);
	if (!thread && R_SUCCEEDED(APT_SetAppCpuTimeLimit(BGM_SYSCORE_LIMIT)))
		thread = threadCreate(stream_thread, NULL, 32 * 1024, prio - 1, 1, false);
	if (!thread)
		thread = threadCreate(stream_thread, NULL, 32 * 1024, prio - 1, -2, false);
	if (!thread)
		return false;
	inited = true;
	return true;
}

static void close_locked(void)
{
	if (pending_f) {
		fclose(pending_f);
		pending_f = NULL;
		pending_id = -1;
	}
	ndspChnWaveBufClear(CHANNEL);
	if (vf_open)
		ov_clear(&vf);
	vf_open = false;
	cur_id = -1;
}

void bgm_stop(void)
{
	if (!inited)
		return;
	LightLock_Lock(&lock);
	close_locked();
	LightLock_Unlock(&lock);
}

bool bgm_play(int id) { return bgm_play_ex(id, true); }

bool bgm_play_ex(int id, bool looped)
{
	if (!inited)
		return false;
	char path[48];
	snprintf(path, sizeof path, "romfs:/bgm/%d.ogg", id);
	LightLock_Lock(&lock);
	close_locked();
	FILE *f = fopen(path, "rb");
	if (!f) {
		LightLock_Unlock(&lock);
		return false;
	}
	pending_f = f; /* opened + started by the stream thread (open_pending_locked) */
	pending_id = id;
	pending_loop = looped;
	cur_id = id;
	LightLock_Unlock(&lock);
	LightEvent_Signal(&ev);
	return true;
}

void bgm_set_volume(float v)
{
	volume = v < 0 ? 0 : v > 1 ? 1 : v;
	float mix[12] = {0};
	mix[0] = mix[1] = volume;
	ndspChnSetMix(CHANNEL, mix);
}

/* Ayame.dll SetVolume (vtable +0x14 -> 0x100224d0, applied by 0x10022950):
 * v >= 100 -> 0, v <= 0 -> -10000, else DirectSound volume = -pow(100, log10(100 - v))
 * = -(100 - v)^2 hundredths of a dB. */
float bgm_ayame_gain(float v)
{
	if (v >= 100.0f)
		return 1.0f;
	if (v <= 0.0f)
		return 0.0f;
	float hdb = (100.0f - v) * (100.0f - v); /* attenuation, 1/100 dB */
	return powf(10.0f, -hdb / 2000.0f);
}

void bgm_set_ayame_volume(float v) { bgm_set_volume(bgm_ayame_gain(v)); }

int bgm_current(void) { return cur_id; }

void bgm_exit(void)
{
	if (!inited)
		return;
	bgm_stop();
	quit = true;
	LightEvent_Signal(&ev);
	threadJoin(thread, U64_MAX);
	threadFree(thread);
	ndspSetCallback(NULL, NULL);
	for (int i = 0; i < NBUF; i++)
		linearFree(pcm[i]);
	inited = false;
}
