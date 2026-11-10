/*
 * Music director — port of the original BGM manager (docs/re/music.md):
 *   FUN_004bee00 request, FUN_004bee60 forced switch, FUN_004beeb0 per-frame
 *   fade/switch, FUN_004bf2e0 volume apply, VS BGM list built in Select Init
 *   FUN_0079acc0, random pick FUN_007aaed0.
 */
#include "music.h"

#include <3ds.h>
#include <stdio.h>

#include "../platform/bgm.h"
#include "../sim/rng.h"
#include "../sim/sim.h"
#include "audio.h"

/* DAT_0086c000: BGM volume level -> scale (identical to the SE table 0x86c890). */
static const float bgm_scale[11] = {0.0f, 0.3f, 0.5f, 0.6f, 0.65f, 0.7f, 0.75f, 0.85f, 0.9f, 0.95f, 1.0f};
#define AYAME_VOL_MAX 1000.0f /* DAT_00814408 clamp */

static int cur;       /* DAT_0087a528 current id (0 = none) */
static int req;       /* DAT_0087a52c requested id */
static bool req_loop; /* DAT_0087a530 */
static int fade;      /* DAT_0087a534 fade counter, 100 = full */
static bool handle;   /* DAT_0087a544 != 0: a stream object exists */
static int level = 5; /* DAT_00881ea8, ConfigData Init default 5 */

static int list[64], nlist; /* DAT_009b8dd4 */
static int sel;             /* DAT_009b6adc */
static aos_mt cosmetic;     /* stands in for the cosmetic MT 0x875b68 */

/* script/en/bgm.txt, indexed by text id - 0x208. Text id of a BGM id =
 * 0x208 + (id <= 0x19 ? id - 10 : id - 14) (stage-select draw FUN_007aabb0,
 * battle "now playing" in Game Init FUN_005f4650). */
static const char *const names[51] = {
	"????", "Need for Speed", "Black Hole", "Distant Thunder", "Swordfish", "Shine", "Expendables",
	"Ribbon", "Movin' Out", "Accelerator", "Remember Me", "Magical Girl of Miracles",
	"Icarus (Realism)", "Green Bird (Realism)", "Rendezvous (Realism)",
	"Daedalus (SUGURI the Best)", "Green Bird (SUGURI the Best)", "Gray Hued (SUGURI the Best)",
	"First Encounter (SUGURI the Best)", "Ice Cage (SUGURI the Best)", "Stratosphere (SUGURI the Best)",
	"Necessary (SUGURI the Best)", "Rendezvous (SUGURI the Best)", "Icarus (SUGURI the Best)",
	"Magical Girl of Miracles (Instrumental) (SUGURI the Best)", "Le Train (SUGURI the Best)",
	"Migratory Bird From North (Hoshi no Kakera)", "Freedom Sky (Hoshi no Kakera)",
	"Linear Light (Hoshi no Kakera)", "Impact (Hoshi no Kakera)", "Stratosphere (Hoshi no Kakera)",
	"Solitary As Fear (Hoshi no Kakera)", "Rendezvous (Hoshi no Kakera)", "Gray Hued (Hoshi no Kakera)",
	"Icarus (TRANCENATION of SUGURI)", "Green Bird (TRANCENATION of SUGURI)",
	"First Encount (TRANCENATION of SUGURI)", "Horizon (TRANCENATION of SUGURI)",
	"Touch & Intense (TRANCENATION of SUGURI)", "Silent Rythm (TRANCENATION of SUGURI)",
	"Impact (TRANCENATION of SUGURI)", "Stratosphere (TRANCENATION of SUGURI)",
	"Difference (TRANCENATION of SUGURI)", "Solitary As Fear (TRANCENATION of SUGURI)",
	"Air Fort (TRANCENATION of SUGURI)", "Rendezvous (TRANCENATION of SUGURI)",
	"High Altitude (TRANCENATION of SUGURI)", "Power Resonance (TRANCENATION of SUGURI)",
	"Green Bird Old FM Tune (TRANCENATION of SUGURI)", "Freedom Sky (TRANCENATION of SUGURI)",
	"Linear Light (TRANCENATION of SUGURI)",
};

const char *music_name(int id)
{
	int t = id <= 0x19 ? id - 10 : id - 14;
	if (id < 10 || (id > 0x15 && id < 0x1a) || t < 0 || t >= 51)
		return "";
	return names[t];
}

static bool have_file(int id)
{
	char path[32];
	snprintf(path, sizeof path, "romfs:/bgm/%d.ogg", id);
	FILE *f = fopen(path, "rb");
	if (f)
		fclose(f);
	return f != NULL;
}

static void push(int id) { if (nlist < (int)(sizeof list / sizeof list[0])) list[nlist++] = id; }

static void push_range_if_owned(int lo, int hi)
{
	/* DLC soundtracks: the original checks Steam DLC ownership (FUN_007c4b10); the
	 * port includes a pack when its tracks were extracted into romfs. */
	if (!have_file(lo))
		return;
	for (int id = lo; id <= hi; id++)
		push(id);
}

void music_init(void)
{
	aos_mt_seed(&cosmetic, (uint32_t)svcGetSystemTick()); /* random_device stand-in */
	/* Select Init FUN_0079acc0 (0x79b713..): "????" first, then the base tracks
	 * whose unlock flag 0x87b753+id is set — note 0x14 before 0x13. The port has
	 * no save data: all base tracks count as unlocked. */
	nlist = 0;
	push(MUSIC_RANDOM);
	static const int base[] = {0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x14, 0x13, 0x15};
	for (unsigned i = 0; i < sizeof base / sizeof base[0]; i++)
		push(base[i]);
	push_range_if_owned(0x1a, 0x1c); /* Realism, app 0x78515 */
	push_range_if_owned(0x1d, 0x27); /* SUGURI the Best, app 0x78516 */
	push_range_if_owned(0x28, 0x2f); /* Hoshi no Kakera, app 0x17c924 */
	push_range_if_owned(0x30, 0x40); /* TRANCENATION, app 0x17c91a */
	sel = 0; /* the index of entry 10 ("????") when entering from the menu */
}

int music_list_count(void) { return nlist; }
int music_list_id(int index) { return index >= 0 && index < nlist ? list[index] : 0; }
int music_sel(void) { return sel; }

void music_sel_step(int dir)
{
	/* FUN_0079c110 state 200: up/down move DAT_009b6adc with wrap, SE 2 each */
	if (!nlist)
		return;
	sel += dir < 0 ? -1 : 1;
	if (sel < 0)
		sel = nlist - 1;
	if (sel >= nlist)
		sel = 0;
	audio_se(2, AUDIO_SE_CD_DEFAULT);
}

int music_resolve(void)
{
	if (!nlist)
		return 0;
	if (list[sel] != MUSIC_RANDOM)
		return list[sel];
	if (nlist < 2)
		return 0;
	/* FUN_007aaed0: rand_int(0,100) % (count - 1) + 1, unsigned (skips "????") */
	uint32_t r = (uint32_t)aos_rand_i(&cosmetic, 0, 100);
	return list[r % (uint32_t)(nlist - 1) + 1];
}

/* ---- request / fade state machine ---- */

void music_request(int id, bool restart, bool loop)
{
	/* FUN_004bee00 (the "suspended" branch DAT_0087a538 is window-focus only) */
	if (restart && cur == id)
		cur = 0;
	req = id;
	req_loop = loop;
}

void music_force(int id, bool loop)
{
	cur = 0; /* FUN_004bee60 */
	req = id;
	req_loop = loop;
}

int music_current(void) { return cur; }

void music_update(void)
{
	/* FUN_004beeb0 */
	if (req == cur)
		return;
	if (fade > 0) {
		fade--;
		if (handle)
			bgm_set_ayame_volume((float)fade * bgm_scale[level]);
	}
	if (fade < 1 || cur == 0) {
		cur = req;
		bgm_stop(); /* +0xc stop, +0x1c release */
		handle = false;
		if (cur != 0) {
			handle = true; /* the stream is created even at level 0 ... */
			if (level != 0) { /* ... but only played when the BGM level is not 0 */
				float v = bgm_scale[level] * 100.0f;
				v = v > AYAME_VOL_MAX ? AYAME_VOL_MAX : v < 0.0f ? 0.0f : v;
				bgm_set_ayame_volume(v);
				if (!bgm_play_ex(cur, req_loop))
					printf("\x1b[33mBGM %d missing\x1b[0m\n", cur);
				fade = 100;
			}
		}
	}
}

int music_level(void) { return level; }

void music_set_level(int lvl)
{
	level = lvl < 0 ? 0 : lvl > 10 ? 10 : lvl;
	/* FUN_004bf2e0: volume = counter * scale, applied to the current stream */
	if (cur != 0) {
		float v = (float)fade * bgm_scale[level];
		v = v > AYAME_VOL_MAX ? AYAME_VOL_MAX : v < 0.0f ? 0.0f : v;
		bgm_set_ayame_volume(v);
	}
}

/* ---- scenes ---- */

void music_scene_select(void) { music_force(MUSIC_SELECT, true); }

void music_battle_start(int id)
{
	/* Game Init FUN_005f4650 (new match only): FUN_004bee00(bgm, 1, 1). In the
	 * original the select/VS jingle has already faded to silence by then
	 * (cur == 0), so the battle track starts at once at full volume; the port has
	 * no VS screen, so it switches without the fade to get the same result. */
	music_force(id, true);
}

void music_battle_exit(void) { music_request(0, true, false); /* FUN_005f5780: (0,1,0) */ }

void music_watch(const aos_world *w)
{
	static bool was_end_wipe;
	bool end_wipe = w->flow.fadeOn && w->flow.fadeType == 0x12;
	if (end_wipe && !was_end_wipe)
		music_request(0, true, true);
	was_end_wipe = end_wipe;
}
