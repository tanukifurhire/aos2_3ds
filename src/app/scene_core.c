/* Portable part of the scene framework (scene.h): registry and switching,
 * menu input, transitions, help-line state, match setup, config. The 3DS
 * main loop and drawing are in scene.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene.h"
#include "scene_core.h"
#include "text.h"

match_setup g_setup;
app_config g_config;
menu_input g_min;
void (*scene_post_frame_hook)(void);

/* ---------------------------------------------------------------- registry */

static const scene_def *defs[SCENE_MAX];
static int cur = SCENE_NONE, prev = SCENE_NONE, req = -1, requested = SCENE_NONE;
static bool init_pending;
static uint32_t frames;

void scene_register(int id, const scene_def *def)
{
	if (id > 0 && id < SCENE_MAX)
		defs[id] = def;
}
const scene_def *scene_get(int id) { return id > 0 && id < SCENE_MAX ? defs[id] : NULL; }
void scene_goto(int next) { req = next; }
int scene_current(void) { return cur; }
int scene_prev(void) { return prev; }
uint32_t scene_frame(void) { return frames; }
int scene_requested(void) { return requested; }
bool scene_switch_pending(void) { return req >= 0; }

static void enter(int id)
{
	requested = id;
	if (id != SCENE_QUIT && !scene_get(id))
		id = SCENE_COMING_SOON;
	cur = id;
	init_pending = true;
	frames = 0;
}

void scene_core_start(int first)
{
	prev = SCENE_NONE;
	req = -1;
	enter(first);
}

const scene_def *scene_core_begin_frame(void)
{
	const scene_def *d = scene_get(cur);
	if (init_pending) {
		init_pending = false;
		min_flush();
		if (d && d->init)
			d->init();
	}
	return d;
}

void scene_core_update(void)
{
	const scene_def *d = scene_get(cur);
	if (d && d->update)
		d->update();
	frames++;
}

bool scene_core_end_frame(void)
{
	if (req >= 0) {
		int next = req;
		req = -1;
		const scene_def *d = scene_get(cur);
		if (d && d->exit)
			d->exit();
		prev = cur;
		enter(next);
	}
	return cur != SCENE_QUIT;
}

/* ---------------------------------------------------------------- input */

void minput_feed(menu_input *mi, uint16_t bits)
{
	static const uint16_t slot_bits[MI_SLOT_COUNT] = {
		[MI_RIGHT] = MB_RIGHT, [MI_LEFT] = MB_LEFT, [MI_UP] = MB_UP, [MI_DOWN] = MB_DOWN,
		[MI_OK] = MB_A, [MI_CANCEL] = MB_B, [MI_ALT_CANCEL] = 0, [MI_ALT_OK] = MB_START,
		[MI_X] = MB_X, [MI_Y] = MB_Y, [MI_L] = MB_L, [MI_R] = MB_R, [MI_START] = MB_START,
		[MI_SELECT] = MB_SELECT, [MI_TOUCH] = MB_TOUCH,
	};
	for (int i = 0; i < MI_SLOT_COUNT; i++) {
		bool on = slot_bits[i] && (bits & slot_bits[i]);
		bool was = slot_bits[i] && (mi->prev & slot_bits[i]);
		uint8_t s = on ? (was ? MIN_HELD : MIN_PRESSED) : (was ? MIN_RELEASED : MIN_UP);
		mi->nr[i] = s;
		if (on)
			mi->held[i] = was ? (uint16_t)(mi->held[i] + 1) : 0;
		else
			mi->held[i] = 0;
		/* FUN_0061c100: directions repeat after 20 frames, then every 5th */
		if (i <= MI_DOWN && on && was && mi->held[i] >= 20 && mi->held[i] % 5 == 0)
			s = MIN_PRESSED;
		mi->st[i] = s;
	}
	mi->prev = bits;
}

bool min_confirm(void) /* FUN_0061c7b0 */
{
	return g_min.st[MI_OK] == MIN_PRESSED ||
	       (g_min.st[MI_ALT_OK] == MIN_PRESSED && g_min.st[MI_CANCEL] != MIN_HELD && g_min.st[MI_CANCEL] != MIN_PRESSED);
}

bool min_cancel(void) /* FUN_0061c7e0 */
{
	return g_min.st[MI_CANCEL] == MIN_PRESSED ||
	       (g_min.st[MI_ALT_CANCEL] == MIN_PRESSED && g_min.st[MI_OK] != MIN_HELD && g_min.st[MI_OK] != MIN_PRESSED);
}

void min_flush(void)
{
	/* pressed -> held so nothing triggers on the frame of a scene switch */
	for (int i = 0; i < MI_SLOT_COUNT; i++) {
		if (g_min.st[i] == MIN_PRESSED)
			g_min.st[i] = MIN_HELD;
		if (g_min.nr[i] == MIN_PRESSED)
			g_min.nr[i] = MIN_HELD;
	}
}

/* ---------------------------------------------------------------- transitions */

static struct {
	int type, done;
	bool on;
	float a, rate;
} fade;

void scene_fade(int type) /* FUN_00561fc0 (types 1..16) */
{
	fade.type = type;
	fade.on = true;
	fade.a = 0;
	fade.done = 0;
	switch (type & 3) {
	case 1: fade.rate = 0.05f; break;
	case 2: fade.rate = 0.01f; break;
	case 3: fade.rate = 0.005f; break;
	default: fade.rate = 0.0025f; break;
	}
}

void scene_fade_update(void) /* FUN_00561c70 */
{
	if (!fade.on)
		return;
	if (fade.done == 1) {
		fade.on = false;
		return;
	}
	fade.a += fade.rate;
	if (fade.a >= 1.0f) {
		fade.a = 1.0f;
		fade.done = 1;
	}
}

bool scene_fade_busy(void) { return fade.on; }

float scene_fade_alpha(void)
{
	if (!fade.on || fade.type < 1 || fade.type > 16)
		return 0;
	return ((fade.type - 1) & 4) ? 1.0f - fade.a : fade.a;
}

bool scene_fade_white(void) { return fade.type >= 9 && fade.type <= 16; }

/* ---------------------------------------------------------------- help line */

static struct {
	char text[512];
	int len_cp, typed;
	bool shown;
	float alpha;
} ann;

void announce_show_text(const char *s)
{
	if (ann.shown && !strcmp(ann.text, s))
		return;
	snprintf(ann.text, sizeof ann.text, "%s", s);
	int n = 0;
	for (const char *p = ann.text; *p; p++)
		n += ((unsigned char)*p & 0xc0) != 0x80;
	ann.len_cp = n;
	ann.typed = 0;
	ann.shown = true;
}

void announce_show(int id) { announce_show_text(announce_by_id(id)); }
void announce_hide(void) { ann.shown = false; }
const char *announce_text(void) { return ann.text; }

void announce_update(void) /* FUN_00495040 */
{
	if (ann.shown) {
		ann.alpha += 0.1f;
		if (ann.alpha >= 1.0f) {
			ann.alpha = 1.0f;
			if (ann.typed < ann.len_cp)
				ann.typed++;
		}
	} else {
		ann.alpha -= 0.1f;
		if (ann.alpha < 0)
			ann.alpha = 0;
	}
}

float announce_alpha(void) { return ann.alpha; }

/* Typed prefix (whole code points) of the current text into out. */
void announce_typed(char *out, size_t sz)
{
	int n = 0;
	size_t i = 0;
	const char *p = ann.text;
	while (*p && i + 4 < sz) {
		if (((unsigned char)*p & 0xc0) != 0x80) {
			if (n == ann.typed)
				break;
			n++;
		}
		out[i++] = *p++;
	}
	out[i] = 0;
}

/* ---------------------------------------------------------------- config */

void config_defaults(app_config *c) /* FUN_004db370 */
{
	memset(c, 0, sizeof *c);
	c->language = LANG_EN;
	c->required_wins = 1;
	c->time_limit = 1;
	c->se_volume = 5;
	c->bgm_volume = 5;
	c->hitstun_meter = true;
	c->p1_color = 2;
	c->p2_color = 0;
	c->pause_hold = true;
	c->pause_on_start = true;
	c->attack_ind_mirror = true;
	c->city_lod = 1; /* STAGE_LOD_CAP: the port's default "Smoke cap" (stages.md §5) */
	c->net_delay = 2;
}

int config_timer_seconds(int t)
{
	static const int s[4] = {90, 180, 270, 999};
	return t >= 0 && t < 4 ? s[t] : 180;
}

const char *config_path = "sdmc:/3ds/aos2_3ds/config.ini";

#define CFG_FIELDS(X) \
	X(language) X(required_wins) X(time_limit) X(se_volume) X(bgm_volume) X(hitstun_meter) X(high_contrast) \
	X(p1_color) X(p2_color) X(tournament) X(pause_hold) X(pause_on_start) X(grey_bg) X(armor_points) \
	X(armor_percent) X(attack_ind) X(attack_ind_mirror) X(city_lod) X(net_delay)

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void config_sanitize(app_config *c)
{
	c->language = lang_available(c->language) ? c->language : LANG_EN;
	c->required_wins = clampi(c->required_wins, 0, 2);
	c->time_limit = clampi(c->time_limit, 0, 3);
	c->se_volume = clampi(c->se_volume, 0, 10);
	c->bgm_volume = clampi(c->bgm_volume, 0, 10);
	c->p1_color = clampi(c->p1_color, 0, 5);
	c->p2_color = clampi(c->p2_color, 0, 5);
	c->city_lod = clampi(c->city_lod, 0, 3);
	c->net_delay = clampi(c->net_delay, 0, 4);
}

void config_load(void)
{
	config_defaults(&g_config);
	FILE *f = fopen(config_path, "r");
	if (!f)
		return;
	char line[128];
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = 0;
		int v = atoi(eq + 1);
#define X(n) if (!strcmp(line, #n)) g_config.n = v;
		CFG_FIELDS(X)
#undef X
	}
	fclose(f);
	config_sanitize(&g_config);
}

void config_save(void)
{
	FILE *f = fopen(config_path, "w");
	if (!f)
		return;
#define X(n) fprintf(f, "%s=%d\n", #n, (int)g_config.n);
	CFG_FIELDS(X)
#undef X
	fclose(f);
}
