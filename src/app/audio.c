/*
 * Sound effects (docs/re/music.md §6, objects.md §5.1).
 *
 * Original rules ported here:
 *  - FUN_007bc9c0(id): plays if SE level != 0 and lockout[id] < 1; sets lockout = 2.
 *  - FUN_007bcaf0(id, n): plays if allowed[id] (once per frame) and lockout[id] == 0;
 *    sets lockout = n.
 *  - FUN_007bcc00, once per frame after the scene update: allowed[] = 1, lockout-- (> 0).
 *  - Each id owns `voices` (se_table.h, FUN_00515800) DirectSound buffers, used
 *    round robin (FUN_00517420): playing restarts the next buffer of that id, so
 *    an id never has more than `voices` instances.
 *  - Volume per id: -(int)(50 - 50*t[lvl]) dB (FUN_007bc8a0/FUN_00518ce0, SSE floats),
 *    t = DAT_0086c890, lvl = DAT_00881eaa (default 5), 0 = SE off.
 * Port-only limit: NDSP has 24 channels; SE get 22 (0..21), so when all are busy
 * the oldest-started voice is stolen (DirectSound had no such limit).
 */
#include "audio.h"
#include "wav.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sim/se_table.h"
#include "prof.h"

#define VOICES 22 /* NDSP channels 0..21 for SE (23 is BGM) */
#define CACHE 72  /* the original has 61 SE slots */
#define MAX_ID_VOICES 10 /* FUN_00515800: abs(voices) must be < 11 */

/* Input-lab slots (placeholder action -> sound pairing). */
static const char *const lab_files[SFX_COUNT] = {
	"romfs:/se/gun12.wav", "romfs:/se/laser02.wav", "romfs:/se/swing.wav", "romfs:/se/bomb2.wav",
	"romfs:/se/air01.wav", "romfs:/se/tm2_sonic004.wav", "romfs:/se/power35.wav", "romfs:/se/reflect.wav",
};

/* DAT_0086c890: SE level -> scale */
static const float se_scale[11] = {0.0f, 0.3f, 0.5f, 0.6f, 0.65f, 0.7f, 0.75f, 0.85f, 0.9f, 0.95f, 1.0f};

typedef struct {
	char path[48];
	void *pcm; /* linear memory, DSP-visible */
	u32 frames;
	u16 channels;
	u16 bits;
	u32 rate;
	bool tried;
} sfx_buf;

typedef struct {
	int16_t id;   /* SE id owning this channel, -1 = lab/free */
	int8_t slot;  /* the id's buffer index */
	u32 seq;      /* start order, for stealing */
} chan_info;

static sfx_buf cache[CACHE];
static int ncache;
static ndspWaveBuf wave[VOICES];
static chan_info chan[VOICES];
static u32 seq;
static bool ready;

static int se_level = 5;                 /* DAT_00881eaa (ConfigData Init default 5) */
static uint8_t lockout[AOS_SE_COUNT];    /* DAT_009b9ad4 */
static bool allowed[AOS_SE_COUNT];       /* DAT_009b9a94 */
static uint8_t next_slot[AOS_SE_COUNT];  /* DAT_00899ae8 round-robin index */
static int8_t slot_chan[AOS_SE_COUNT][MAX_ID_VOICES];
static sfx_buf *id_buf[AOS_SE_COUNT];
static bool id_tried[AOS_SE_COUNT];

static bool load_path(sfx_buf *s)
{
	s->tried = true;
	FILE *f = fopen(s->path, "rb");
	if (!f)
		return false; /* missing files are silent, as in the original (e.g. bomb1.wav) */
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) {
		fclose(f);
		return false;
	}
	u8 *raw = malloc((size_t)n);
	bool ok = raw && fread(raw, 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	aos_wav w;
	if (ok && aos_wav_parse(raw, (size_t)n, &w) == 0 && (s->pcm = linearAlloc(w.pcm_bytes))) {
		memcpy(s->pcm, w.pcm, w.pcm_bytes);
		if (w.bits == 8) /* WAV 8-bit is unsigned, NDSP PCM8 is signed */
			for (u32 i = 0; i < w.pcm_bytes; i++)
				((u8 *)s->pcm)[i] ^= 0x80;
		DSP_FlushDataCache(s->pcm, w.pcm_bytes);
		s->frames = w.frames;
		s->channels = w.channels;
		s->bits = w.bits;
		s->rate = w.sample_rate;
	} else {
		ok = false;
	}
	free(raw);
	return ok;
}

static sfx_buf *get(const char *path)
{
	for (int i = 0; i < ncache; i++)
		if (!strcmp(cache[i].path, path)) {
			if (!cache[i].tried) /* released by audio_se_prepare: reload */
				load_path(&cache[i]);
			return &cache[i];
		}
	if (ncache >= CACHE || strlen(path) >= sizeof cache[0].path)
		return NULL;
	sfx_buf *s = &cache[ncache++];
	memset(s, 0, sizeof *s);
	strcpy(s->path, path);
	load_path(s);
	return s;
}

static void reset_se_state(void)
{
	memset(lockout, 0, sizeof lockout);
	memset(next_slot, 0, sizeof next_slot);
	memset(slot_chan, -1, sizeof slot_chan);
	for (int i = 0; i < AOS_SE_COUNT; i++)
		allowed[i] = true;
	for (int c = 0; c < VOICES; c++)
		chan[c].id = -1;
}

bool audio_init(const char **err)
{
	*err = NULL;
	reset_se_state();
	if (R_FAILED(ndspInit())) {
		*err = "no DSP firmware: audio off"; /* fits 40 cols */
		return false;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	for (int i = 0; i < SFX_COUNT; i++) {
		sfx_buf *s = get(lab_files[i]);
		if ((!s || !s->pcm) && !*err)
			*err = lab_files[i];
	}
	memset(wave, 0, sizeof wave);
	ready = true;
	return true;
}

/* -(int)(50 - 50*t[lvl]) dB as linear gain (SSE single-precision, like the original). */
static float se_gain(void)
{
	volatile float fifty = 50.0f; /* keep the float rounding of the original */
	float m = fifty * se_scale[se_level];
	int db = -(int)(fifty - m);
	return powf(10.0f, (float)db / 20.0f);
}

static void start(int ch, const sfx_buf *s, float gain)
{
	ndspChnWaveBufClear(ch); /* stop whatever this voice was playing */
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(ch, (float)s->rate);
	ndspChnSetFormat(ch, s->bits == 8
		? (s->channels == 2 ? NDSP_FORMAT_STEREO_PCM8 : NDSP_FORMAT_MONO_PCM8)
		: (s->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16));
	float mix[12] = {0};
	mix[0] = mix[1] = gain;
	ndspChnSetMix(ch, mix);
	memset(&wave[ch], 0, sizeof wave[ch]);
	wave[ch].data_vaddr = s->pcm;
	wave[ch].nsamples = s->frames;
	ndspChnWaveBufAdd(ch, &wave[ch]);
	chan[ch].seq = ++seq;
}

/* A free channel, else the one started longest ago (port-only limit). */
static int alloc_channel(void)
{
	int best = 0;
	for (int c = 0; c < VOICES; c++) {
		if (!ndspChnIsPlaying(c))
			return c;
		if (chan[c].seq < chan[best].seq)
			best = c;
	}
	return best;
}

static void claim(int ch, int id, int slot)
{
	int old = chan[ch].id;
	if (old >= 0 && slot_chan[old][chan[ch].slot] == ch)
		slot_chan[old][chan[ch].slot] = -1;
	chan[ch].id = (int16_t)id;
	chan[ch].slot = (int8_t)slot;
	if (id >= 0)
		slot_chan[id][slot] = (int8_t)ch;
}

void audio_se_frame(void)
{
	/* FUN_007bcc00 */
	for (int i = 0; i < AOS_SE_COUNT; i++) {
		allowed[i] = true;
		if (lockout[i] > 0)
			lockout[i]--;
	}
}

void audio_se(int id, int cd)
{
	if (id < 0 || id >= AOS_SE_COUNT || !aos_se_table[id].file || se_level == 0)
		return;
	if (!id_tried[id]) {
		char path[64];
		snprintf(path, sizeof path, "romfs:/se/%s", aos_se_table[id].file);
		id_tried[id] = true;
		id_buf[id] = ready ? get(path) : NULL;
#ifdef AOS_PROFILE
		prof_note("SE 0x%02x (%s) loaded on first use", id, aos_se_table[id].file);
#endif
	}
	if (!id_buf[id] || !id_buf[id]->pcm)
		return; /* seHandle == -1 in the original: no lockout either */
	if (cd == AUDIO_SE_CD_DEFAULT) { /* FUN_007bc9c0 */
		if (lockout[id] >= 1)
			return;
		lockout[id] = 2;
	} else { /* FUN_007bcaf0 */
		if (!allowed[id] || lockout[id] != 0)
			return;
		lockout[id] = (uint8_t)cd;
	}
	allowed[id] = false;

	int nv = aos_se_table[id].voices;
	nv = nv < 1 ? 1 : nv > MAX_ID_VOICES ? MAX_ID_VOICES : nv;
	int slot = next_slot[id] % nv;
	next_slot[id] = (uint8_t)((slot + 1) % nv);
	int ch = slot_chan[id][slot];
	if (ch < 0 || chan[ch].id != id || chan[ch].slot != slot)
		ch = alloc_channel(); /* that buffer isn't sounding: take a free/oldest voice */
	claim(ch, id, slot);
	start(ch, id_buf[id], se_gain());
}

int audio_se_level(void) { return se_level; }

/* ---- per-match SE residency ----
 * All 61 SE files are 10.6 MB of PCM in linear memory (shared with every
 * texture), and loading one the first time it plays reads up to 2.6 MB from
 * the SD card mid-match (100+ ms hitches in the profiler). So at match load we
 * release what the new match can't use and preload what it can: the ids each
 * character was seen to trigger (tests/se_usage.c: all pairings, all CPU
 * levels, random inputs, pinned hyper), the stage's own SE and the small
 * system/menu sounds. Anything else still loads lazily as before, so sound
 * behaviour is unchanged; only memory and timing differ. */
static const uint64_t se_char_mask[19] = { /* generated by tests/se_usage.c (mirror matches) */
	[3] = 0x04000407a0dc7bc0ull,  [4] = 0x0c008625ee391bc0ull,  [5] = 0x0400a801a3299bc0ull,
	[6] = 0x04009809a0e8bb40ull,  [7] = 0x0408840127199b40ull,  [8] = 0x040d8025651edbc0ull,
	[9] = 0x040880452f1e1bc0ull,  [10] = 0x0400040588dc7bc0ull, [11] = 0x0403808187399bc0ull,
	[12] = 0x0c108004243c3bc0ull, [13] = 0x04028400a11dbbc0ull, [14] = 0x040080f1a3291bc0ull,
	[15] = 0x0400002000695bc0ull, [16] = 0x04000140026d5bc0ull, [17] = 0x1e00802804797bc0ull,
	[18] = 0x1e08802c043debc0ull,
};
#define SE_SYSTEM_MASK (0x3full | 0xfull << 0x39) /* ids 0..5 menu/system, 0x39..0x3c hyper level-ups (core.c) */

static uint64_t se_stage_mask(int stage) /* stage_se() calls in src/stage/st_*.c */
{
	switch (stage) {
	case 6: return 1ull << 0x34;  /* bf: bomb2 */
	case 7: return 1ull << 0x35;  /* gr: winda@08 */
	case 8: return 1ull << 0x37;  /* dr: tornade */
	case 9: return 1ull << 0x36;  /* ri: rain1(fade) */
	default: return 0;
	}
}

void audio_se_prepare(int id1, int id2, int stage)
{
	if (!ready)
		return;
	uint64_t want = SE_SYSTEM_MASK | se_stage_mask(stage);
	if (id1 >= 0 && id1 < 19)
		want |= se_char_mask[id1];
	if (id2 >= 0 && id2 < 19)
		want |= se_char_mask[id2];
	/* stop SE voices (between matches nothing should be sounding anyway) */
	for (int ch = 0; ch < VOICES; ch++)
		ndspChnWaveBufClear(ch);
	reset_se_state();
	/* release buffers no wanted id uses (lab sounds stay) */
	for (int i = 0; i < ncache; i++) {
		sfx_buf *s = &cache[i];
		if (!s->pcm)
			continue;
		bool keep = false;
		for (int k = 0; k < SFX_COUNT && !keep; k++)
			keep = !strcmp(s->path, lab_files[k]);
		for (int id = 0; id < AOS_SE_COUNT && !keep; id++)
			keep = (want >> id & 1) && id_buf[id] == s;
		if (keep)
			continue;
		linearFree(s->pcm);
		s->pcm = NULL;
		s->tried = false;
		for (int id = 0; id < AOS_SE_COUNT; id++)
			if (id_buf[id] == s) {
				id_buf[id] = NULL;
				id_tried[id] = false;
			}
	}
	/* preload the wanted ones */
	for (int id = 0; id < AOS_SE_COUNT; id++) {
		if (!(want >> id & 1) || !aos_se_table[id].file || id_tried[id])
			continue;
		char path[64];
		snprintf(path, sizeof path, "romfs:/se/%s", aos_se_table[id].file);
		id_tried[id] = true;
		id_buf[id] = get(path);
	}
}

size_t audio_se_bytes(void)
{
	size_t n = 0;
	for (int i = 0; i < ncache; i++)
		if (cache[i].pcm)
			n += (size_t)cache[i].frames * cache[i].channels * (cache[i].bits / 8);
	return n;
}
void audio_set_se_level(int lvl) { se_level = lvl < 0 ? 0 : lvl > 10 ? 10 : lvl; }

void audio_play(aos_sfx id)
{
	if (!ready || (unsigned)id >= SFX_COUNT)
		return;
	const sfx_buf *s = get(lab_files[id]);
	if (!s || !s->pcm)
		return;
	int ch = alloc_channel();
	claim(ch, -1, 0);
	start(ch, s, 0.8f);
}

void audio_exit(void)
{
	if (!ready)
		return;
	for (int ch = 0; ch < VOICES; ch++)
		ndspChnWaveBufClear(ch);
	ndspExit();
	for (int i = 0; i < ncache; i++)
		if (cache[i].pcm)
			linearFree(cache[i].pcm);
	ncache = 0;
	memset(id_buf, 0, sizeof id_buf);
	memset(id_tried, 0, sizeof id_tried);
	ready = false;
}
