/* Mira (engine id 6, states 0x7c..0x9d) — 1:1 port of aos2.exe's Mira code.
 * Spec: docs/re/mira.md. Every handler names its original function.
 * Version 0x5a2 branches only (AOS_GAME_VERSION). */
#include <string.h>

#include "../draw.h"
#include "../fx.h"
#include "../objects.h"
#include "../sim.h"

#define ID AOS_CH_MIRA

void aos_se_cd(aos_world *w, int id, int cd);

/* DAT_00880028: Quick Slash lunge speed, one static shared by both players. */
typedef struct {
	float lunge;
} mira_global;

static mira_global *glob(aos_world *w) { return (mira_global *)(void *)w->charGlobal[ID]; }

/* Player +0x90 and enemy +0x74 are floats in the original but int32 in the
 * engine structs; keep the original storage location through memcpy. */
static float p90(const aos_player *p) { float f; memcpy(&f, &p->f90, 4); return f; }
static void set_p90(aos_player *p, float f) { memcpy(&p->f90, &f, 4); }
static float e74(const aos_enemy *e) { float f; memcpy(&f, &e->f74, 4); return f; }
static void set_e74(aos_enemy *e, float f) { memcpy(&e->f74, &f, 4); }

/* Per-team colour slot DAT_008859f4 + team*0x10 + 4k. */
static int32_t team_col(aos_world *w, int32_t team, int k) { return aos_team_colour(w, team, k); }

static void fx0(aos_world *w, int id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t team)
{
	aos_spawn_effect(w, 0, id, variant, pos, angle, owner, 0, team);
}

static void fx6(aos_world *w, int id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	aos_spawn_effect(w, ID, id, 0, pos, angle, owner, age0, team);
}

/* dash sparks: r = rand(lo,hi); n = randInt(0,100)%8; common effect 0xa28+n colour 0xd */
static void sparks(aos_world *w, aos_player *p, float lo, float hi, float dir)
{
	float r = aos_rand_real(w, lo, hi);
	int n = aos_rand_int(w, 0, 100) % 8;
	fx0(w, 0xa28 + n, 0xd, &p->x, r + dir, p->objId, p->side);
}

static void face_x(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	p->faceAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
	p->dashAngle = p->faceAngle;
}

/* FUN_006f5c00: index of the first deployed chakram of that team, or -1. */
static int chakram_find(aos_world *w, int32_t side)
{
	for (int i = 0; i < w->obj.nenemy; i++) {
		aos_enemy *e = &w->obj.enemy[i];
		if (e->team == side && e->type == ID && e->f10 == 100)
			return i;
	}
	return -1;
}

/* FUN_0061c730: a direction is held and within ±90° of target. */
static bool input_toward(const aos_player *p, float target)
{
	float a = aos_dir_angle(p);
	if (!(a <= AOS_PI))
		return false;
	/* FUN_005f0f60(&a, target, 0): signed wrapped difference */
	aos_angle_normalize(&a);
	aos_angle_normalize(&target);
	float d = target - a;
	if (d <= AOS_PI) {
		if (d < -3.1415927f)
			d = AOS_2PI + d;
	} else {
		d = -(AOS_2PI - d);
	}
	return aos_fabs(d) < 1.5707964f;
}

/* FUN_00780a50 */
static bool may_force_hitstun(aos_world *w, aos_player *o)
{
	return o->barrierTime == 0 && o->state != AOS_ST_KO && o->hittable &&
	       o->statusOn[AOS_STATUS_STUNIMMUNE] != 1 && !w->roundOver;
}

static void freeze_opp(aos_world *w, aos_player *p)
{
	aos_player *o = aos_opponent(w, p);
	if (may_force_hitstun(w, o)) {
		o->state = AOS_ST_HITSTUN;
		o->timer = 0;
		o->speed = 0;
	}
}

/* FUN_005f15a0: intersection of line a-b with line c-d (no parallel check). */
static void isect(const float *a, const float *b, const float *c, const float *d, float *out)
{
	float a1 = a[1] - b[1], b1 = b[0] - a[0], c1 = (b[0] - a[0]) * a[1] - (b[1] - a[1]) * a[0];
	float a2 = c[1] - d[1], b2 = d[0] - c[0], c2 = (d[0] - c[0]) * c[1] - (d[1] - c[1]) * c[0];
	float den = a1 * b2 - b1 * a2;
	out[0] = (b2 * c1 - b1 * c2) / den;
	out[1] = (a1 * c2 - a2 * c1) / den;
	out[2] = 0;
}

/* Tornado origin: opponent projected onto the arena-centre line along a (0x94 / 0x9d). */
static void tornado_origin(aos_world *w, aos_player *p, float a, float out[3])
{
	aos_player *o = aos_opponent(w, p);
	(void)p;
	float l1[2] = {aos_cos(a) * 1000.0f, aos_sin(a) * 1000.0f};
	float l2[2] = {-aos_cos(a) * 1000.0f, -aos_sin(a) * 1000.0f};
	float m1[2] = {aos_cos(a + 1.5707964f) * 1000.0f + o->x, aos_sin(a + 1.5707964f) * 1000.0f + o->y};
	float m2[2] = {aos_cos(a - 1.5707964f) * 1000.0f + o->x, aos_sin(a - 1.5707964f) * 1000.0f + o->y};
	isect(l1, l2, m1, m2, out);
}


/* ------------------------------------------------------------------ */
/* whips (type 6) — visual only. FUN_007c5d50 / FUN_007c6570 / FUN_007c5e70 */

static int whip_spawn(aos_world *w, int32_t kind, int32_t slot, int32_t serial) /* FUN_007c5d50 */
{
	if (kind != 0 && kind != 1)
		return -1;
	int i = aos_whip_new(w, ID, kind, slot, serial); /* +0x08 = player slot (p+0x18) */
	if (i < 0)
		return -1;
	w->obj.whip[i].f10 = serial; /* +0x10 = missile serial */
	return i;
}

static bool whip_update(aos_world *w, aos_whip *h) /* FUN_007c6570 */
{
	aos_missile *m = NULL;
	for (int i = 0; i < w->obj.nmissile; i++)
		if (w->obj.missile[i].serial == h->f10) {
			m = &w->obj.missile[i];
			break;
		}
	if (!m)
		return false;
	float wd = h->id == 0 ? 7.5f : 5.0f; /* DAT_00814150 / DAT_00813f40 */
	float c = aos_cos(m->angle + 1.5707964f), s = aos_sin(m->angle + 1.5707964f);
	float b[2] = {-c * wd + m->pos[0], -s * wd + m->pos[1]};
	float a[2] = {c * wd + m->pos[0], s * wd + m->pos[1]};
	/* node k: [0..2] point A (+0x14), [3..5] point B (+0x20), [6] used flag (+0x2c) */
	for (int k = AOS_WHIP_NODES - 1; k >= 0; k--) {
		if (h->node[k][6] == 0.0f) {
			h->node[k][3] = b[0];
			h->node[k][4] = b[1];
			h->node[k][0] = a[0];
			h->node[k][1] = a[1];
			h->node[k][6] = 1.0f;
			return true;
		}
	}
	for (int k = AOS_WHIP_NODES - 1; k > 0; k--)
		memcpy(h->node[k], h->node[k - 1], sizeof(float) * 6);
	h->node[0][3] = b[0];
	h->node[0][4] = b[1];
	h->node[0][0] = a[0];
	h->node[0][1] = a[1];
	return true;
}

static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float x0, float y0,
                      float x1, float y1, float rot, float a, int blend);

/* Bank of the Mira player whose palette is `slot` (whips store p+0x18, not the owner id). */
static int slot_bank(aos_world *w, int32_t slot)
{
	for (int i = 0; i < 2; i++)
		if (w->p[i].id == ID && w->p[i].color == slot)
			return i;
	for (int i = 0; i < 2; i++)
		if (w->p[i].id == ID)
			return i;
	return AOS_BANK_P0;
}

static void whip_draw(aos_world *w, aos_whip *h) /* FUN_007c5e70 */
{
	/* Textured strip: for k = 0..18 with nodes k and k+1 in use, quad B[k] (u 0, v 12.8k),
	 * B[k+1] (u 0, v 12.8(k+1)), A[k+1] (u 256), A[k] (u 256); colour 0x80ffffff
	 * (DAT_00828324 = 12.8, DAT_00814160 = 256). Each trapezoid is emitted as the
	 * rectangle that best fits it (same centre, mean width, segment length). */
	int tex = aos_tex(h->id == 0 ? "mr_whip0" : "mr_whip1");
	int bank = slot_bank(w, h->ownerId);
	for (int k = 0; k < AOS_WHIP_NODES - 1; k++) {
		const float *n0 = h->node[k], *n1 = h->node[k + 1];
		if (n0[6] == 0.0f || n1[6] == 0.0f)
			continue;
		float c0x = (n0[0] + n0[3]) * 0.5f, c0y = (n0[1] + n0[4]) * 0.5f;
		float c1x = (n1[0] + n1[3]) * 0.5f, c1y = (n1[1] + n1[4]) * 0.5f;
		float w0 = aos_sqrt((n0[0] - n0[3]) * (n0[0] - n0[3]) + (n0[1] - n0[4]) * (n0[1] - n0[4]));
		float w1 = aos_sqrt((n1[0] - n1[3]) * (n1[0] - n1[3]) + (n1[1] - n1[4]) * (n1[1] - n1[4]));
		float dx = c1x - c0x, dy = c1y - c0y;
		float len = aos_sqrt(dx * dx + dy * dy);
		float pos[3] = {(c0x + c1x) * 0.5f, (c0y + c1y) * 0.5f, 0};
		/* local x = across (B -> A, texture u), local y = along the strip (texture v) */
		float ax = (n0[0] - n0[3]) + (n1[0] - n1[3]), ay = (n0[1] - n0[4]) + (n1[1] - n1[4]);
		aos_dcmd *c = blit(tex, bank, pos, (w0 + w1) * 0.25f, len * 0.5f, 0, 12.8f * (float)k, 256.0f,
		                   12.8f * (float)(k + 1), aos_atan2(ay, ax), 128.0f / 255.0f, AOS_BLEND_ALPHA);
		(void)c;
	}
}

/* ------------------------------------------------------------------ */
/* beams (type 6) FUN_004adc50 / FUN_004aec70 / FUN_004ae000 / FUN_004ae960 */

static int beam_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 0x6e && id != 200 && id != 0xc9 && id != 300)
		return -1;
	int i = aos_beam_new(w, ID, id, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_beam *b = &w->obj.beam[i];
	switch (id) {
	case 100:
		b->speed = 0;
		b->scale = 3.0f;
		b->damage = 0x28; /* v>0x5a0 (old 0x50) */
		b->shape = (aos_shape){0, 0, 10.0f};
		b->active = 0;
		b->knockback = 5.0f;
		break;
	case 0x6e:
		b->speed = 12.0f;
		b->scale = 3.0f;
		b->damage = 0x28;
		b->shape = (aos_shape){0, 0, 10.0f};
		b->knockback = 5.0f;
		break;
	case 200:
	case 0xc9:
		b->speed = 16.0f;
		b->f6c = id == 200 ? 0.05f : -0.05f;
		b->damage = 200;
		b->shape = (aos_shape){0, 0, 16.0f};
		b->knockback = 5.0f;
		b->id = 200;
		break;
	case 300:
		b->scale = 0;
		b->speed = 24.0f;
		b->damage = 1;
		b->shape = (aos_shape){0, 0, 10.0f};
		b->knockback = 0;
		break;
	}
	return i;
}

static bool beam_update(aos_world *w, aos_beam *b)
{
	switch (b->id) {
	case 100:
		b->scale -= 0.2f;
		if (b->scale < 1.0f)
			b->scale = 1.0f;
		if (b->age == 5)
			b->active = 1;
		if (b->age > 300) {
			b->id = 0xbe;
			b->active = 0;
			b->scale = 3.0f;
			fx0(w, 0x516, team_col(w, b->team, 2), b->pos, 0, b->ownerId, b->team);
		}
		break;
	case 0x6e:
		b->scale -= 0.2f;
		if (b->scale < 1.0f)
			b->scale = 1.0f;
		b->pos[0] += aos_cos(b->moveAngle) * b->speed;
		b->pos[1] += aos_sin(b->moveAngle) * b->speed;
		b->speed *= 0.95f;
		if (b->age > 0x50 || (b->age > 5 && !aos_in_arena(b->pos, 10.0f))) {
			b->id = 0xbe;
			b->active = 0;
			b->scale = 3.0f;
			fx0(w, 0x516, team_col(w, b->team, 2), b->pos, 0, b->ownerId, b->team);
		}
		break;
	case 0xbe:
		b->scale -= 0.5f;
		if (b->scale <= 0.0f)
			return false;
		break;
	case 200:
		if (b->age < 10)
			b->speed *= 0.75f;
		else if (b->speed < 20.0f)
			b->speed += 0.5f;
		if (b->alpha <= 0.0f)
			b->alpha = 0;
		else
			b->alpha -= 0.01f;
		b->moveAngle += b->f6c * b->alpha;
		b->pos[0] += aos_cos(b->moveAngle) * b->speed;
		b->pos[1] += aos_sin(b->moveAngle) * b->speed;
		b->angle += b->f6c * 10.0f;
		if (b->angle > AOS_PI)
			b->angle -= AOS_2PI;
		if (b->age > 0x32 && !aos_in_arena(b->pos, 10.0f))
			return false;
		fx6(w, 400, b->pos, b->angle, b->ownerId, 0, b->team);
		break;
	case 300:
		b->scale += 0.1f;
		if (b->scale > 1.0f) {
			b->scale = 1.0f;
			b->damage = 0x32;
		}
		b->speed *= 0.85f;
		b->pos[0] += aos_cos(b->moveAngle) * b->speed;
		b->pos[1] += aos_sin(b->moveAngle) * b->speed;
		b->angle += 0.05f;
		if (b->age > 0x96) {
			int32_t c = team_col(w, b->team, 0);
			fx0(w, 0x516, c, b->pos, 0, b->ownerId, b->team);
			for (int k = 0; k < 6; k++) /* DAT_00813f44, DAT_00853b78 */
				aos_spawn_particle(w, 0x456, c, b->pos, ((float)k / 6.0f) * AOS_2PI, b->ownerId, 0, 2);
			return false;
		}
		break;
	}
	return true;
}

static void beam_on_hit(aos_world *w, aos_beam *b) /* FUN_004ae960 */
{
	int32_t c = team_col(w, b->team, 0);
	if (b->id == 200) {
		for (int k = -3; k < 3; k++) {
			float base = ((float)k / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, c, b->pos, base + r, b->ownerId, b->team);
		}
		fx0(w, 0x516, c, b->pos, 0, b->ownerId, b->team);
		aos_se(w, 0x13);
	} else if (b->id == 300) {
		fx0(w, 0x516, c, b->pos, 0, b->ownerId, b->team);
		for (int k = 0; k < 6; k++)
			aos_spawn_particle(w, 0x456, c, b->pos, ((float)k / 6.0f) * AOS_2PI, b->ownerId, 0, 1);
		aos_se(w, 0x15);
	}
}

static int owner_bank(aos_world *w, int32_t ownerId)
{
	aos_player *o = aos_player_by_objid(w, ownerId);
	return o ? aos_pidx(w, o) : AOS_BANK_P0;
}

/* 2D sprite blit (vtable +0x68 of a texture): dst half-size in world units, src rect in pixels. */
static aos_dcmd *blit(int tex, int bank, const float pos[3], float hw, float hh, float x0, float y0,
                      float x1, float y1, float rot, float a, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = hw;
	c->halfH = hh;
	c->hasSrc = 1;
	c->src[0] = x0;
	c->src[1] = y0;
	c->src[2] = x1;
	c->src[3] = y1;
	c->rotZ = aos_rad2bam(rot);
	c->rgba = aos_rgba(1, 1, 1, a);
	c->blend = (uint8_t)blend;
	return c;
}

static float a255(float v) { return (float)(int)v / 255.0f; } /* (int)(K*alpha) << 24 */

static void beam_draw(aos_world *w, aos_beam *b) /* FUN_004ae000 */
{
	if (b->id == 100 || b->id == 0x6e || b->id == 0xbe) {
		/* shared bullet atlas DAT_0087fde4, colour cell = DAT_008859fc[team] (slot 2) */
		int c = aos_team_colour(w, b->team, 2);
		int tex = aos_tex("beamsphere");
		float x0 = (float)(((c % 10) & 1) * 100 + 0x32 + (c / 10) * 0xfa), y0 = (float)(((c % 10) / 2) * 100);
		blit(tex, AOS_BANK_COMMON, b->pos, 30.0f * b->scale, 30.0f * b->scale, x0, y0, x0 + 99.0f, y0 + 99.0f,
		     0, a255(96.0f * b->alpha), AOS_BLEND_ALPHA); /* DAT_008140e8, DAT_00814118 */
		x0 = (float)((c / 10) * 0xfa);
		y0 = (float)((c % 10) * 0x32);
		blit(tex, AOS_BANK_COMMON, b->pos, 15.0f * b->scale, 15.0f * b->scale, x0, y0, x0 + 49.0f, y0 + 49.0f,
		     0, a255(255.0f * b->alpha), AOS_BLEND_ADD_ALPHA); /* DAT_00853b88, DAT_00853b94 */
	} else if (b->id == 200) {
		/* sprite 0x9e frame 1 (blend 1) then frame 0 (blend 2), half 32*scale (DAT_008140dc) */
		int bank = owner_bank(w, b->ownerId);
		float h = 32.0f * b->scale;
		blit(aos_texf("mr_blade0%d", 1), bank, b->pos, h, h, 0, 0, 127, 127, b->angle, 1, AOS_BLEND_ALPHA);
		blit(aos_texf("mr_blade0%d", 0), bank, b->pos, h, h, 0, 0, 127, 127, b->angle, 1, AOS_BLEND_ADD_ALPHA);
	} else if (b->id == 300) {
		/* sprite 0x9f frame ((age/2)%5)*2+1 (blend 1) then that-1 (blend 2), half 24*scale (DAT_00853b8c) */
		int bank = owner_bank(w, b->ownerId);
		int f = ((b->age / 2) % 5) * 2 + 1;
		float h = 24.0f * b->scale;
		blit(aos_texf("mr_caltrap0%d", f), bank, b->pos, h, h, 0, 0, 127, 127, b->angle, 1, AOS_BLEND_ALPHA);
		blit(aos_texf("mr_caltrap0%d", f - 1), bank, b->pos, h, h, 0, 0, 127, 127, b->angle, 1, AOS_BLEND_ADD_ALPHA);
	}
}

/* ------------------------------------------------------------------ */
/* missiles (type 6) FUN_0066e4b0 / FUN_0066fd50 / FUN_0066e660 / FUN_0066faa0 / FUN_0066fc20 */

static int missile_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200)
		return -1;
	int i = aos_missile_new(w, ID, id, pos, angle, owner, age0, team, p18);
	if (i < 0)
		return -1;
	aos_missile *m = &w->obj.missile[i];
	m->growScale = 0;
	if (id == 100) {
		m->speed = 16.0f;
		m->hp = 0x50;     /* v>0x58a */
		m->damage = 0xb4; /* v>0x58a */
	} else {
		m->speed = 30.0f;
		m->hp = 0x3c;
		m->damage = 0xa0;
	}
	m->shapeBody.radius = 8.0f;
	m->shapeProj.radius = 8.0f;
	m->knockback = 10.0f;
	return i;
}

static bool missile_on_killed(aos_world *w, aos_missile *m) /* FUN_0066fc20 */
{
	if (m->id == 100 || m->id == 0x65 || m->id == 200) {
		fx0(w, 0x262, 0, m->pos, 0, m->ownerId, m->team);
		fx0(w, 0x515, 1, m->pos, 0, m->ownerId, m->team);
		aos_se(w, 0x1d);
	}
	return false;
}

static bool missile_on_hit(aos_world *w, aos_missile *m) /* FUN_0066faa0 */
{
	if (m->id == 100 || m->id == 200) {
		int32_t c = team_col(w, m->team, m->id == 100 ? 0 : 1);
		fx0(w, 0x516, c, m->pos, 0, m->ownerId, m->team);
		fx0(w, 0x9c4, c, m->pos, m->angle, m->ownerId, m->team);
		aos_se(w, 0x15);
	}
	return false;
}

static bool missile_update(aos_world *w, aos_missile *m) /* FUN_0066fd50 */
{
	if (m->id == 100) {
		m->growScale += 0.1f;
		if (m->growScale > 1.0f)
			m->growScale = 1.0f;
		if (m->age < 0x14)
			m->speed *= 0.85f;
		else if (m->speed < 20.0f)
			m->speed += 0.5f;
		m->pos[0] += aos_cos(m->moveAngle) * m->speed;
		m->pos[1] += aos_sin(m->moveAngle) * m->speed;
		if (!aos_in_arena(m->pos, 0)) {
			m->id = 0x65;
			m->age = 0;
			m->shapeBody.radius = 10.0f;
			fx0(w, 0x516, team_col(w, m->team, 0), m->pos, 0, m->ownerId, m->team);
			aos_se_cd(w, 0x16, 3);
		}
	} else if (m->id == 0x65) {
		if (m->age > 100) {
			missile_on_killed(w, m); /* calls 0x881af8[type] itself */
			return false;
		}
	} else if (m->id == 200) {
		m->growScale += 0.1f;
		if (m->growScale > 1.0f)
			m->growScale = 1.0f;
		if (m->age < 0x1389) {
			if (m->speed > 5.0f)
				m->speed *= 0.85f;
		} else {
			m->speed += 0.2f;
		}
		m->pos[0] += aos_cos(m->moveAngle) * m->speed;
		m->pos[1] += aos_sin(m->moveAngle) * m->speed;
		if (m->age < 6 && m->age % 2 == 0) {
			float r = aos_rand_real(w, -0.2f, 0.2f);
			int n = aos_rand_int(w, 0, 100) % 8;
			fx0(w, 0xa28 + n, 0xc, m->pos, r + m->angle, m->ownerId, m->team);
		}
		if (!aos_in_arena(m->pos, 10.0f)) {
			if (m->age > 5000)
				return false;
			if (!aos_in_arena(m->pos, 0)) {
				float a = aos_atan2(m->pos[1], m->pos[0]);
				m->pos[0] = aos_cos(a) * 540.0f;
				m->pos[1] = aos_sin(a) * 540.0f;
				aos_player *o = &w->p[m->team == 1 ? 1 : 0]; /* FUN_00780480(team) */
				m->moveAngle = aos_atan2(o->y - m->pos[1], o->x - m->pos[0]);
				m->angle = m->moveAngle;
				m->age += 5000;
				m->speed = 5.0f;
				int32_t c = team_col(w, m->team, 1);
				fx0(w, 0x547, c, m->pos, m->moveAngle, m->ownerId, m->team);
				fx0(w, 0x546, c, m->pos, m->moveAngle, m->ownerId, m->team);
				aos_se_cd(w, 0xb, 4);
			}
		}
	}
	return true;
}

/* CModel quad (sprite registered S×S): pos, scale (sx, sy), rotZ, colour (a,r,g,b), blend. */
static aos_dcmd *quad(int tex, int bank, const float pos[3], float half, float sx, float sy, float rot,
                      float a, float r, float g, float bl, int blend)
{
	aos_dcmd *c = aos_draw(tex, bank, pos[0], pos[1], 0);
	c->halfW = c->halfH = half;
	c->sx = sx;
	c->sy = sy;
	c->rotZ = aos_rad2bam(rot);
	c->rgba = aos_rgba(r, g, bl, a);
	c->blend = (uint8_t)blend;
	return c;
}

static void missile_draw(aos_world *w, aos_missile *m) /* FUN_0066e660 */
{
	int bank = owner_bank(w, m->ownerId);
	float g = m->growScale;
	float sw = aos_sin(((float)(m->age % 50) / 50.0f) * AOS_2PI);
	float a1 = sw * 0.15f + 0.85f; /* DAT_00813f20, DAT_00814120 */
	float a2 = sw * 0.25f + 0.75f; /* DAT_00813f24, DAT_00813f3c */
	if (m->id == 100 || m->id == 0x65) {
		/* sprite 0xa0 (mr_knife, S=80); the stuck kunai adds 0xa6 (mr_impact, S=128) */
		float sx = 3.0f - g * 2.0f;
		if (m->id == 0x65)
			quad(aos_texf("mr_impact%d", 0), bank, m->pos, 64.0f, g * a1 * 0.5f, g * a1 * 0.5f, m->angle,
			     a1 * 0.5f, 1, 1, 1, AOS_BLEND_ALPHA);
		if (1.0f <= g)
			quad(aos_texf("mr_knife0%d", 0), bank, m->pos, 40.0f, sx, g, m->angle, 1, 1, 1, 1, AOS_BLEND_ALPHA);
		else
			quad(aos_texf("mr_knife0%d", 0), bank, m->pos, 40.0f, sx, g, m->angle, 1, 0, 0, 0, AOS_BLEND_ALPHA);
		quad(aos_texf("mr_knife0%d", 1), bank, m->pos, 40.0f, sx, g, m->angle, a1, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
		quad(aos_texf("mr_knife0%d", 2), bank, m->pos, 40.0f, sx, g, m->angle, a2, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
		if (m->id == 0x65)
			quad(aos_texf("mr_impact%d", 1), bank, m->pos, 64.0f, g * a1 * 0.5f, g * a1 * 0.5f, m->angle,
			     a2, 0.5f, 0.5f, 0.5f, AOS_BLEND_ADD_ALPHA);
	} else if (m->id == 200) {
		/* sprite 0xa1 (mr_gimlet, S=80): frame 0 never gets a colour call (default white) */
		quad(aos_texf("mr_gimlet0%d", 0), bank, m->pos, 40.0f, 2.0f - g, g, m->angle, 1, 1, 1, 1, AOS_BLEND_ALPHA);
		if (g == 1.0f) { /* frames 1/2 have no scale call -> (1,1) */
			quad(aos_texf("mr_gimlet0%d", 1), bank, m->pos, 40.0f, 1, 1, m->angle, a1, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
			quad(aos_texf("mr_gimlet0%d", 2), bank, m->pos, 40.0f, 1, 1, m->angle, a2, 1, 1, 1, AOS_BLEND_ADD_ALPHA);
		}
	}
}

/* ------------------------------------------------------------------ */
/* hitboxes (type 6) FUN_005e5480 / FUN_005e5af0 */

static int hitbox_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team)
{
	int32_t dmg;
	aos_shape s = {0, 0, 0};
	float kb;
	uint8_t rehit = 0;
	switch (id) {
	case 0xb4: dmg = 0x8c; s.radius = 60.0f; kb = 10.0f; rehit = 1; break;
	case 100: dmg = 0x28; s.radius = 60.0f; kb = 15.0f; break;
	case 0xbe: dmg = 0x32; s.radius = 60.0f; kb = 5.0f; break;
	case 0xc3: dmg = 0x78; s.radius = 60.0f; kb = 15.0f; rehit = 1; break;
	case 200: dmg = 0x1e; s.radius = 60.0f; kb = 0; break;          /* dead id */
	case 0xd2: dmg = 0; s.radius = 70.0f; kb = 0; break;            /* v>0x5a0 DAT_008145ec */
	case 300: dmg = 0x78; s.halfW = 60.0f; s.halfH = 80.0f; kb = 2.5f; break;
	case 0x136: dmg = 0x50; s.halfW = 60.0f; s.halfH = 80.0f; kb = 2.5f; break;
	case 0x140: dmg = 0xa0; s.halfW = 80.0f; s.halfH = 40.0f; kb = 0; break;
	case 400: dmg = 0x78; s.halfW = 100.0f; s.halfH = 50.0f; kb = 0; break;
	case 0x191: dmg = 0; s.radius = 100.0f; kb = 0; break;          /* dead id */
	case 0x19a: dmg = 0x50; s.halfW = 120.0f; s.halfH = 60.0f; kb = 0; break;
	case 0x1a4: dmg = 200; s.halfW = 132.0f; s.halfH = 65.5f; kb = 2.5f; break; /* v>0x5a0 */
	default: return -1;
	}
	int i = aos_hitbox_new(w, ID, id, pos, angle, owner, zero, team);
	if (i < 0)
		return -1;
	aos_hitbox *h = &w->obj.hitbox[i];
	h->hitType = 3;
	h->damage = dmg;
	h->shape = s;
	h->knockback = kb;
	h->rehitFlag = rehit;
	return i;
}

static void streaks4(aos_world *w, aos_hitbox *hb)
{
	float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
	int32_t c = team_col(w, hb->team, 1);
	for (int k = -2; k < 2; k++) {
		float base = ((float)k / 2.0f) * AOS_PI;
		float r = aos_rand_real(w, -0.25f, 0.25f);
		fx0(w, 0x456, c, hb->pos, base + r + r0, hb->ownerId, hb->team);
	}
	fx0(w, 0x515, c, hb->pos, 0, hb->ownerId, hb->team);
	aos_se_cd(w, 0x16, 4);
}

static void hitbox_on_hit(aos_world *w, aos_hitbox *hb, aos_player *v) /* FUN_005e5af0 */
{
	int32_t c0 = team_col(w, hb->team, 0);
	aos_player *own = (hb->team == 1 || hb->team == 2) ? &w->p[hb->team - 1] : NULL; /* FUN_00780450 */
	switch (hb->id) {
	case 0xb4:
		if (v->state == AOS_ST_HITSTUN && v->speed > 2.0f) {
			hb->dealsDamage = 0;
			return;
		}
		streaks4(w, hb);
		return;
	case 100:
	case 0xbe:
	case 0xc3:
		if (hb->id != 0xc3 || v->state != AOS_ST_HITSTUN || v->speed <= 2.0f)
			streaks4(w, hb);
		else
			hb->dealsDamage = 0;
		return;
	case 200: { /* dead id (never spawned) */
		float r0 = aos_rand_real(w, -AOS_PI, AOS_PI);
		if (hb->damage > 0) {
			float r = aos_rand_real(w, -0.25f, 0.25f);
			int n = aos_rand_int(w, 0, 100) % 4;
			fx0(w, 0xa32 + n, 0xc, &v->x, r + hb->angle, hb->ownerId, hb->team);
			fx0(w, 0x9c4, c0, &v->x, r0, hb->ownerId, hb->team);
		}
		return;
	}
	case 0xd2:
		if (v->state == AOS_ST_HITSTUN && v->timer < 6)
			hb->dealsDamage = 0;
		return;
	case 300:
	case 0x136: {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c0, &v->x, r, hb->ownerId, hb->team);
		if (hb->id == 300)
			fx0(w, 0xaf0, c0, &v->x, 0, hb->ownerId, hb->team);
		aos_spawn_particle(w, 1000, c0, &v->x, hb->angle, hb->ownerId, 0, 4);
		aos_spawn_spark(w, 1000, c0, 10, 4, hb->angle, &v->x, hb->ownerId);
		if (own)
			own->hitFlag = 1;
		aos_se_cd(w, 0x16, 3);
		return;
	}
	case 0x140: {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c0, &v->x, r, hb->ownerId, hb->team);
		fx0(w, 0x9c4, c0, &v->x, r + 1.5707964f, hb->ownerId, hb->team);
		fx0(w, 0xaf0, c0, &v->x, 0, hb->ownerId, hb->team);
		aos_spawn_particle(w, 1000, c0, &v->x, hb->angle, hb->ownerId, 0, 4);
		aos_spawn_spark(w, 1000, c0, 10, 4, hb->angle, &v->x, hb->ownerId);
		aos_se(w, 0x17);
		aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
		return;
	}
	case 400:
		if (own)
			own->hitFlag = 1;
		aos_se(w, 0x16);
		return;
	case 0x19a: {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, c0, &v->x, r, hb->ownerId, hb->team);
		aos_spawn_particle(w, 1000, c0, &v->x, hb->angle, hb->ownerId, 0, 4);
		aos_spawn_spark(w, 1000, c0, 10, 4, hb->angle, &v->x, hb->ownerId);
		aos_se(w, 0x16);
		return;
	}
	case 0x1a4:
		fx0(w, 0xaf0, c0, &v->x, 0, hb->ownerId, hb->team);
		if (aos_fabs(hb->angle) <= 1.5707964f)
			fx0(w, 0x9c5, c0, &v->x, hb->angle - 0.25f, hb->ownerId, hb->team);
		else
			fx0(w, 0x9c5, c0, &v->x, hb->angle + 0.25f, hb->ownerId, hb->team);
		aos_spawn_particle(w, 1000, c0, &v->x, hb->angle, hb->ownerId, 0, 4);
		aos_spawn_spark(w, 1000, c0, 10, 4, hb->angle, &v->x, hb->ownerId);
		aos_se(w, 0x17);
		aos_screen_blur(w, 10, 0.75f, 1);
		return;
	default:
		return;
	}
}

/* ------------------------------------------------------------------ */
/* lasers (type 6) FUN_0061fbb0 / FUN_00620750 / FUN_0061fd00 / FUN_00620470 */

static int laser_spawn(aos_world *w, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 0x96)
		return -1;
	int i = aos_laser_new(w, ID, id, pos, angle, owner, team, p18);
	if (i < 0)
		return -1;
	aos_laser *l = &w->obj.laser[i];
	l->hitType = 3;
	l->blockMode = 1;
	l->knockback = 0;
	l->damage = id == 100 ? 0x23 : 0x6e;
	l->stun = 1;
	return i;
}

static void laser_hb(aos_world *w, aos_laser *l, const float pos[3])
{
	/* even age, 14 < age < 90: zero-damage holding hitbox 0xd2 at the segment */
	if (l->age % 2 == 0 && l->age > 0xe && l->age < 0x5a) {
		int h = aos_spawn_hitbox(w, ID, 0xd2, pos, l->angle, l->ownerId, 0, l->team);
		if (l->age % 10 != 0 && h >= 0)
			w->obj.hitbox[h].damage = 0;
	}
}

static bool laser_update(aos_world *w, aos_laser *l) /* FUN_00620750 */
{
	if (l->id == 100) {
		l->nseg = 0;
		if (l->age > 0x78)
			return false;
		aos_shape s = {0, 0, (l->age % 5 == 0 && l->age > 0xe && l->age < 0x5b) ? 60.0f : 0.0f};
		for (int k = -20; k < 20; k++) {
			float pos[3];
			pos[0] = aos_cos(l->angle) * (float)k * 50.0f + l->origin[0];
			pos[1] = aos_sin(l->angle) * (float)k * 50.0f + l->origin[1];
			pos[2] = 0;
			float wv = aos_sin(((float)((l->age + k) % 20) / 20.0f) * AOS_2PI);
			pos[0] = aos_cos(l->angle + 1.5707964f) * wv * 30.0f + pos[0];
			wv = aos_sin(((float)((l->age + k) % 20) / 20.0f) * AOS_2PI);
			pos[1] = aos_sin(l->angle + 1.5707964f) * wv * 30.0f + pos[1];
			aos_laser_push_seg(l, &s, pos);
			laser_hb(w, l, pos);
		}
	} else if (l->id == 0x96) {
		l->nseg = 0;
		if (l->age > 0x78)
			return false;
		if (l->age > 0xe) {
			aos_shape s = {0, 0, (l->age % 5 == 0 && l->age < 0x5b) ? 80.0f : 0.0f};
			for (int k = -20; k < 20; k++) {
				float pos[3];
				float along = (float)k * 50.0f;
				float wv = aos_sin(((float)((l->age + k) % 40) / 40.0f) * AOS_2PI);
				pos[0] = aos_cos(l->angle) * (along + wv * 20.0f) + l->origin[0];
				wv = aos_sin(((float)((l->age + k) % 40) / 40.0f) * AOS_2PI);
				pos[1] = aos_sin(l->angle) * (along + wv * 20.0f) + l->origin[1];
				pos[2] = 0;
				int m = (l->age + k) % 16; /* C remainder, negative for negative age+k */
				float w2 = aos_sin(((float)m / 16.0f) * AOS_2PI);
				pos[0] = aos_cos(l->angle + 1.5707964f) * w2 * 40.0f + pos[0];
				w2 = aos_sin(((float)m / 16.0f) * AOS_2PI);
				pos[1] = aos_sin(l->angle + 1.5707964f) * w2 * 40.0f + pos[1];
				aos_laser_push_seg(l, &s, pos);
				int ak = k < 0 ? -k : k;
				if (ak > 2 && l->age < 100 && (l->age + k) % 4 == 0) {
					float r = aos_rand_real(w, -0.05f, 0.05f);
					int n = aos_rand_int(w, 0, 100) % 8;
					fx0(w, 0xa64 + n, 0xd, pos, r + l->angle, l->ownerId, l->team);
				}
				laser_hb(w, l, pos);
			}
		}
	}
	return true;
}

static void laser_on_hit(aos_world *w, aos_laser *l, int32_t seg, aos_player *v) /* FUN_00620470 */
{
	(void)seg;
	if (l->id == 100) {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		fx0(w, 0x9c4, team_col(w, l->team, 0), &v->x, r, l->ownerId, l->team);
		v->moveAngle = (l->age % 10 == 0) ? l->angle + 1.5707964f : l->angle - 1.5707964f;
		v->faceAngle = v->moveAngle;
		v->speed = 5.0f;
		aos_se_cd(w, 0x15, 5);
	} else if (l->id == 0x96) {
		aos_rand_real(w, -AOS_PI, AOS_PI); /* drawn and discarded */
		int32_t c = team_col(w, l->team, 0);
		v->moveAngle = (l->age % 10 == 0) ? l->angle + 1.5707964f : l->angle - 1.5707964f;
		for (int k = -3; k < 3; k++) {
			float base = ((float)k / 3.0f) * AOS_PI;
			float r = aos_rand_real(w, -0.25f, 0.25f);
			fx0(w, 0x456, c, &v->x, base + r, l->ownerId, l->team);
		}
		v->faceAngle = v->moveAngle;
		v->speed = 5.0f;
		aos_se_cd(w, 0x15, 5);
	}
}

static void laser_draw(aos_world *w, aos_laser *l) /* FUN_0061fd00 */
{
	int bank = owner_bank(w, l->ownerId);
	for (int i = 0; i < l->nseg; i++) {
		int k = i - 20;
		int ak = k < 0 ? -k : k; /* FUN_007e1bee abs */
		int frame = ((l->age + i * 2) / 2) % 4;
		float hw = 100.0f; /* DAT_00853b90 */
		float hh;
		int alpha;
		if (l->id == 100) {
			hh = (float)(ak * 0xf + 0xf0);
			alpha = 0x48;
		} else {
			hh = (float)(ak * ak) * 1.5f + 180.0f; /* DAT_00814110, DAT_00815258 */
			alpha = 0x30;
		}
		if (l->age < 10)
			hh = ((float)l->age / 10.0f) * hh;
		if (100 < l->age) {
			float f = (float)(0x78 - l->age) / 20.0f; /* DAT_008140fc */
			hh = f * hh;
			alpha = (int)(f * (l->id == 100 ? 72.0f : 48.0f)); /* DAT_008178ac / DAT_008140e4 */
		}
		int tex = aos_texf("mr_tor0%d", frame);
		blit(tex, bank, l->seg[i].pos, hw, hh, 0, 0, 127, 127, l->angle, (float)alpha / 255.0f, AOS_BLEND_ALPHA);
		hh = hh * 0.75f;  /* DAT_00813f3c */
		hw = hw + 0.75f;  /* literal: the half-width gets +0.75, not ×0.75 */
		blit(tex, bank, l->seg[i].pos, hw, hh, 0, 0, 127, 127, l->angle, 64.0f / 255.0f, AOS_BLEND_ADD_ALPHA);
	}
}

/* ------------------------------------------------------------------ */
/* enemies (type 6) FUN_00523620 / FUN_00523af0 / FUN_005257b0 / FUN_005253c0 */

static int enemy_spawn(aos_world *w, int32_t id, int32_t owner, int32_t team, int32_t p18)
{
	if (id != 100 && id != 200 && id != 300 && id != 400 && id != 0x191 && id != 0x192)
		return -1;
	int32_t grp = aos_enemy_group_next(w); /* FUN_0055efc0, once per spawn (every id) */
	int i = aos_enemy_new(w, ID, owner, team, p18);
	if (i < 0)
		return -1;
	aos_enemy *e = &w->obj.enemy[i];
	e->serial = grp;
	e->f10 = id;
	e->f84 = 0;
	e->mirrorChildren = 0;
	e->motionId = 0;
	e->rehitTimer = 0;
	switch (id) {
	case 100:
		e->f7c = 100;
		e->hp = e->maxHp = 500;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 30.0f};
		e->scale[0] = 1.0f;
		e->scale[1] = 0;
		e->scale[2] = 0;
		break;
	case 200:
		e->f7c = 200;
		e->hp = e->maxHp = 0x32;
		e->active = 1;
		e->shape = (aos_shape){0, 0, 20.0f};
		e->scale[0] = 0;
		e->scale[1] = 0;
		e->scale[2] = 1.0f;
		break;
	case 300:
		e->f7c = 300;
		e->hp = e->maxHp = 999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
		break;
	default: /* 400, 401, 402 */
		e->f7c = 400;
		e->hp = e->maxHp = 999;
		e->active = 0;
		e->shape = (aos_shape){0, 0, 0};
		e->scale[0] = 0;
		e->scale[1] = 2.0f;
		e->scale[2] = 1.0f;
		break;
	}
	return i;
}

static bool enemy_on_killed(aos_world *w, aos_enemy *e) /* FUN_005253c0 */
{
	switch (e->f10) {
	case 100:
	case 0xb4:
	case 0xb9:
	case 0xbe:
		fx0(w, 600, 0, e->pos, 0, e->ownerId, e->team);
		for (int k = 0; k < 5; k++) {
			float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pos[0] = aos_rand_real(w, -50.0f, 50.0f) + pos[0];
			pos[1] = aos_rand_real(w, -50.0f, 50.0f) + pos[1];
			fx0(w, 0x834, 0, pos, 0, e->ownerId, e->team);
		}
		for (int k = 0; k < 5; k++) {
			float pos[3] = {e->pos[0], e->pos[1], e->pos[2]};
			pos[0] = aos_rand_real(w, -30.0f, 30.0f) + pos[0];
			pos[1] = aos_rand_real(w, -30.0f, 30.0f) + pos[1];
			aos_spawn_effect(w, 0, 500, 0, pos, 0, e->ownerId, k * -3 - 10, e->team);
		}
		aos_se(w, 0x1a);
		return false;
	case 200: {
		e->f84 = 100;
		e->hp = 999;
		e->timer2 = 0;
		e->active = 0;
		int32_t c = team_col(w, e->team, 0);
		fx0(w, 0x515, c, e->pos, 0, e->ownerId, e->team);
		fx0(w, 0x516, c, e->pos, 0, e->ownerId, e->team);
		int fi = aos_spawn_effect(w, ID, 500, 0, e->pos, e->angle, e->ownerId, 0, e->team);
		if (fi >= 0)
			w->obj.effect[fi].age = e->age;
		aos_se(w, 0x1d);
		return true;
	}
	default:
		return false;
	}
}

static void chakram_burst(aos_world *w, aos_enemy *e)
{
	for (int k = -9; k < 9; k++)
		aos_spawn_beam(w, ID, 0x6e, e->pos, e->angle2 + AOS_PI + (float)k * 0.2f, e->ownerId, 0, e->team);
	fx0(w, 0xaf0, team_col(w, e->team, 1), e->pos, 0, e->ownerId, e->team);
	aos_se_cd(w, 0x19, 3);
	aos_screen_blur(w, 10, 0.75f, 1); /* DAT_00813f3c */
	e->f84 = 900;
	e->timer2 = 0;
}

static bool enemy_update(aos_world *w, aos_enemy *e) /* FUN_00523af0 */
{
	switch (e->f7c) {
	case 0xbe: /* Sky-Soaring chakram */
		if (e->f84 == 0) {
			if (e->f70 < 20.0f)
				e->f70 += 1.0f;
			e->angle += e->scale[0] * 0.5f;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			e->pos[0] += aos_cos(e->angle2) * e->f70;
			e->pos[1] += aos_sin(e->angle2) * e->f70;
			aos_spawn_hitbox(w, ID, 0xbe, e->pos, e->angle2, e->ownerId, 0, e->team);
			fx6(w, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->f70 > 0.0f && e->age % 2 == 0)
				aos_spawn_beam(w, ID, 100, e->pos, e->angle2, e->ownerId, 0, e->team);
			if (e->f70 >= 20.0f && !aos_in_arena(e->pos, 10.0f)) {
				e->f84 = 10;
				e->timer2 = 0;
				e->f70 = 0;
				set_e74(e, aos_atan2(e->pos[1], e->pos[0]));
				aos_se_cd(w, 0x19, 3);
				aos_screen_blur(w, 10, 0.75f, 1);
			}
		} else {
			if (e->timer2 < 0x1c2) {
				if (e->timer2 > 99) {
					e->f70 += 0.00025f;
					if (e->f70 > 0.015f)
						e->f70 = 0.015f;
				}
			} else {
				e->f70 *= 0.95f;
			}
			set_e74(e, e74(e) + e->f70);
			e->pos[0] = aos_cos(e74(e)) * 540.0f;
			e->pos[1] = aos_sin(e74(e)) * 540.0f;
			e->angle += e->scale[0] * 0.5f;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			if (e->timer2 < 0x1c2)
				aos_spawn_hitbox(w, ID, 0xc3, e->pos, aos_atan2(-e->pos[1], -e->pos[0]), e->ownerId, 0, e->team);
			if (e->timer2 < 0x1c2 && e->timer2 > 100)
				fx6(w, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->timer2 == 500)
				return false;
		}
		break;
	case 100: { /* orbiting chakram */
		aos_player *o = aos_player_by_objid(w, e->ownerId);
		if (!o)
			return true; /* original dereferences unconditionally; the owner always exists */
		if (e->f84 == 0) {
			e->f70 += 2.0f;
			e->scale[2] = e->f70 / 120.0f;
			if (e->f70 > 120.0f) {
				e->f70 = 120.0f;
				e->f84 = 100;
				e->scale[2] = 1.0f;
				e->active = 1;
			}
			set_e74(e, e->scale[2] * 0.05f + e74(e));
			if (e74(e) > AOS_PI)
				set_e74(e, e74(e) - AOS_2PI);
			e->angle = e->scale[2] * 0.5f + e->angle;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			e->pos[0] = aos_cos(e->angle2 + e74(e)) * e->f70 + o->x;
			e->pos[1] = aos_sin(e->angle2 + e74(e)) * e->f70 + o->y;
		} else if (e->f84 == 100) {
			/* owner state/timer seen after the owner's t++ (one frame late, mira.md §0.4) */
			set_e74(e, e->scale[0] * 0.05f + e74(e));
			if (o->state == 0x91)
				set_e74(e, e->scale[0] * 0.1f + e74(e));
			if (e74(e) > AOS_PI)
				set_e74(e, e74(e) - AOS_2PI);
			e->angle = e->scale[0] * 0.5f + e->angle;
			if (o->state == 0x91)
				e->angle = e->scale[0] * 0.1f + e->angle;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			e->pos[0] = aos_cos(e->angle2 + e74(e)) * e->f70 + o->x;
			e->pos[1] = aos_sin(e->angle2 + e74(e)) * e->f70 + o->y;
			/* the `t % 1` test at 0x523edc is always true */
			if (o->state == 0x91 && o->timer > 0xb && o->timer < 0x13 && aos_use_gauge(o, 0x91)) {
				aos_spawn_beam(w, ID, 0x6e, e->pos, e->angle, e->ownerId, 0, e->team);
				aos_spawn_beam(w, ID, 0x6e, e->pos, e->angle + 2.0943952f, e->ownerId, 0, e->team);
				aos_spawn_beam(w, ID, 0x6e, e->pos, e->angle - 2.0943952f, e->ownerId, 0, e->team);
			}
			if (o->state == 0x91) {
				fx6(w, 100, e->pos, e->angle, e->ownerId, 0, e->team);
				float a = o->faceAngle + AOS_PI;
				aos_angle_normalize(&a);
				aos_spawn_hitbox(w, ID, 100, e->pos, a, e->ownerId, 0, e->team);
			}
		}
		break;
	}
	case 0xb4: /* Soaring chakram */
		if (e->f84 == 0) {
			if (e->f70 < 20.0f) {
				e->f70 += 1.0f;
				if (e->f70 > 20.0f)
					e->f70 = 20.0f; /* v>0x58a */
			}
			if (!aos_in_arena(e->pos, -10.0f))
				e->f70 -= 0.5f;
			e->angle = e->scale[0] * 0.5f + e->angle;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			float step = e->f70;
			if (!aos_in_arena(e->pos, -10.0f) && e->f70 > 0.0f)
				step *= 1.5f;
			e->pos[0] += aos_cos(e->angle2) * step;
			e->pos[1] += aos_sin(e->angle2) * step;
			aos_spawn_hitbox(w, ID, 0xb4, e->pos, e->angle2, e->ownerId, 0, e->team);
			fx6(w, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->f70 > 0.0f && e->age % 2 == 0)
				aos_spawn_beam(w, ID, 100, e->pos, e->angle2, e->ownerId, 0, e->team);
			/* literal: once slowed in the r>=530 zone it never reaches 20 -> never bursts */
			if (e->f70 >= 20.0f && !aos_in_arena(e->pos, -10.0f))
				chakram_burst(w, e);
		} else {
			e->scale[2] -= 0.02f;
			e->angle = e->scale[2] * e->scale[0] * 0.5f + e->angle;
			if (e->scale[2] <= 0.0f)
				return false;
		}
		break;
	case 0xb9: /* Baiting chakram */
		if (e->f84 == 0) {
			e->scale[2] += 0.1f;
			if (e->scale[2] >= 1.0f) {
				e->scale[2] = 1.0f;
				e->f84 = 100;
				e->timer2 = 0;
			}
		} else if (e->f84 == 100) {
			if (e->f70 >= 0.0f) {
				if (e->f70 < 20.0f)
					e->f70 += 1.0f;
				aos_player *o = &w->p[e->team == 1 ? 1 : 0]; /* FUN_00780480(team) */
				aos_angle_toward(&e->angle2, aos_atan2(o->y - e->pos[1], o->x - e->pos[0]), 0.01f);
			} else {
				e->f70 += 1.0f;
				if (e->f70 >= 0.0f)
					e->angle2 = aos_rand_real(w, -0.5f, 0.5f) + e->angle2; /* gameplay RNG */
			}
			float step = e->f70;
			if (!aos_in_arena(e->pos, -10.0f)) {
				if (step >= 0.0f) {
					step *= 1.5f;
					if (step > 20.0f)
						step = 20.0f;
				} else {
					step -= 0.5f;
				}
			}
			e->pos[0] += aos_cos(e->angle2) * step;
			e->pos[1] += aos_sin(e->angle2) * step;
			e->angle = e->scale[2] * e->scale[0] * 0.5f + e->angle;
			if (e->angle > AOS_2PI)
				e->angle -= AOS_2PI;
			fx6(w, 100, e->pos, e->angle, e->ownerId, 0, e->team);
			aos_spawn_hitbox(w, ID, 0xc3, e->pos, e->angle2, e->ownerId, 0, e->team);
			if (e->f70 >= 20.0f && !aos_in_arena(e->pos, -10.0f))
				chakram_burst(w, e);
		} else {
			e->scale[2] -= 0.02f;
			e->angle = e->scale[2] * e->scale[0] * 0.5f + e->angle;
			if (e->scale[2] <= 0.0f)
				return false;
		}
		break;
	case 200: /* smoke ball / cloud */
		if (e->f84 == 0) {
			e->scale[0] += 0.2f;
			if (e->scale[0] > 1.0f)
				e->scale[0] = 1.0f;
			if (e->timer2 > 5)
				e->f70 -= 0.25f;
			e->pos[0] += aos_cos(e->angle2) * e->f70;
			e->pos[1] += aos_sin(e->angle2) * e->f70;
			e->angle = e->scale[0] * 0.25f + e->angle;
			if (e->angle > AOS_PI)
				e->angle -= AOS_2PI;
			if (e->f70 <= 0.0f || !aos_in_arena(e->pos, 10.0f))
				enemy_on_killed(w, e); /* turns into a cloud; return value unused */
		} else {
			if (e->timer2 % 5 == 0) {
				float a = aos_rand_real(w, -AOS_PI, AOS_PI);
				float d = aos_rand_real(w, 0, 60.0f);
				float pos[3] = {aos_cos(a) * d + e->pos[0], aos_sin(a) * d + e->pos[1], 0};
				float r = aos_rand_real(w, -AOS_PI, AOS_PI);
				fx6(w, 200, pos, r, e->ownerId, 0, e->team);
			}
			aos_player *o = &w->p[e->team == 1 ? 1 : 0]; /* FUN_00780480(team) */
			float dx = o->x - e->pos[0], dy = o->y - e->pos[1];
			if (aos_sqrt(dx * dx + dy * dy) < 100.0f)
				aos_status(w, o, AOS_STATUS_SLOW, 0x10);
			if (e->timer2 > 300)
				return false;
		}
		break;
	case 300: /* Quick Slash afterimage */
		e->scale[0] -= 0.1f;
		e->scale[1] += 0.2f;
		if (e->scale[0] <= 0.0f)
			return false;
		break;
	case 400: /* Divine Speed Slash clone */
		if (e->f84 == 0) {
			e->scale[0] += 0.1f;
			e->scale[1] -= 0.1f;
			if (e->scale[0] >= 1.0f) {
				e->scale[0] = 1.0f;
				e->scale[1] = 1.0f;
				e->timer2 = 0;
				e->f84 = 100;
			}
		} else {
			if (e->timer2 == 5) {
				e->f70 = 10.0f;
				aos_player *o = &w->p[e->team == 1 ? 1 : 0];
				e->angle2 = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
				e->angle = e->angle2;
			}
			if (e->timer2 == 10)
				aos_spawn_hitbox(w, ID, 0x19a, e->pos, e->angle, e->ownerId, 0, e->team);
			if (e->timer2 > 0x14)
				e->f70 *= 0.85f;
			e->pos[0] += aos_cos(e->angle2) * e->f70;
			e->pos[1] += aos_sin(e->angle2) * e->f70;
			if (e->timer2 > 0x28)
				e->scale[2] -= 0.1f;
			if (e->scale[2] <= 0.0f)
				return false;
		}
		break;
	default:
		break;
	}
	return true;
}

static void enemy_draw(aos_world *w, aos_enemy *e) /* FUN_005257b0 — switches on +0x10 (spawn id) */
{
	int bank = owner_bank(w, e->ownerId);
	switch (e->f10) {
	case 100:
	case 0xb4:
	case 0xb9:
	case 0xbe: { /* sprite 0xa2 (mr_chakram, S=128), colour (a=r=g=b=+0x68), scale +0x60 */
		int f = e->age / 50;
		if (f > 2)
			f = 2;
		if (e->f10 == 0xb4 && e->f84 == 900) {
			f -= e->timer2 / 20;
			if (f < 0)
				f = 0;
		}
		float a = e->scale[2];
		quad(aos_texf("mr_chakram0%d", f), bank, e->pos, 64.0f, e->scale[0], e->scale[0], e->angle, a, a, a, a,
		     AOS_BLEND_ALPHA);
		break;
	}
	case 200: /* sprite 0xa5 (mr_smkball, S=48), not drawn as a cloud (+0x84 == 100) */
		if (e->f84 != 100) {
			float k = aos_sin(((float)(e->age % 10) / 10.0f) * AOS_2PI) * 0.15f + 1.0f;
			float v = e->scale[2];
			quad(aos_texf("mr_smkball0%d", (e->age / 3) % 6), bank, e->pos, 24.0f, e->scale[0] * k,
			     e->scale[0] * k, e->angle, 1, v, v, v, AOS_BLEND_ALPHA);
		}
		break;
	case 300: { /* Quick Slash afterimage: sprite 0x80 frame min(+0x80/2,4), black, AIM transform */
		int f = e->timer2 / 2;
		if (f > 4)
			f = 4;
		aos_dcmd *c = quad(aos_texf("mr_sld0%d", f), bank, e->pos, 48.0f, e->scale[0], e->scale[1], e->angle,
		                   1, 0, 0, 0, AOS_BLEND_ALPHA);
		c->flipX = 1;
		if (1.5707964f <= aos_fabs(e->angle))
			c->sy = -c->sy; /* RotX(π) approximated as y-scale cos(π) */
		break;
	}
	case 400:
	case 0x191:
	case 0x192: { /* Divine clones */
		const char *pat = "mr_sld0%d";
		int f;
		if (e->f84 == 0) {
			f = e->timer2 / 2;
			if (f > 4)
				f = 4;
		} else {
			static const char *pats[3] = {"mr_infghta0%d", "mr_infghtb0%d", "mr_infghtc0%d"};
			static const int cap[3] = {3, 4, 5};
			int k = e->f10 - 400;
			pat = pats[k];
			f = e->timer2 < 5 ? 0 : (e->timer2 - 5) / 4;
			if (f > cap[k])
				f = cap[k];
		}
		float rgb = 1.0f <= e->scale[0] ? 1.0f : 0.0f;
		aos_dcmd *c = quad(aos_texf(pat, f), bank, e->pos, 48.0f, e->scale[0], e->scale[1], e->angle,
		                   e->scale[2], rgb, rgb, rgb, AOS_BLEND_ALPHA);
		c->flipX = 1;
		if (1.5707964f <= aos_fabs(e->angle))
			c->sy = -c->sy;
		break;
	}
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Mira effects (type 6) FUN_004fdfa0 / FUN_004fed10 / FUN_004fe220 — cosmetic, no RNG */

static int effect_spawn(aos_world *w, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team)
{
	if (id != 100 && id != 200 && id != 300 && id != 400 && id != 500)
		return -1;
	int i = aos_effect_new(w, ID, id, variant, pos, angle, owner, age0, team);
	if (i < 0)
		return -1;
	aos_effect *f = &w->obj.effect[i];
	f->drawLayer = 1;
	f->scale = 1.0f;
	f->alpha = 1.0f;
	f->aux = 0;
	if (id == 300) {
		f->scale = 2.0f;
		f->alpha = 0;
	} else if (id == 200) {
		f->drawLayer = 0;
		f->aux = 1.0f;
	}
	return i;
}

static bool effect_update(aos_world *w, aos_effect *f)
{
	(void)w;
	switch (f->id) {
	case 300:
		if (f->age < 100) {
			f->alpha += 0.1f;
			f->scale -= 0.1f;
			if (f->scale <= 1.0f) {
				f->alpha = 1.0f;
				f->scale = 1.0f;
				f->age = 100;
				f->drawLayer = 0;
			}
		} else if (f->age > 0x77) {
			f->alpha -= 0.05f;
			f->scale -= 0.05f;
			if (f->scale <= 0.0f)
				return false;
		}
		break;
	case 100:
	case 400:
		f->alpha -= 0.1f;
		if (f->alpha <= 0.0f)
			return false;
		break;
	case 200:
		f->alpha -= 0.025f;
		f->scale += 0.1f;
		if (f->alpha <= 0.0f)
			return false;
		break;
	case 500:
		f->alpha -= 0.1f;
		f->scale += 0.1f;
		if (f->alpha <= 0.0f)
			return false;
		break;
	}
	return true;
}

static void effect_draw(aos_world *w, aos_effect *f) /* FUN_004fe220 */
{
	int bank = owner_bank(w, f->ownerId); /* DAT_00885a00[team] palette bank */
	switch (f->id) {
	case 100: /* chakram trail: sprite 0xa2 frame 2, half 64*scale (DAT_008140f8), alpha 200a, blend 2 */
		blit(aos_texf("mr_chakram0%d", 2), bank, f->pos, 64.0f * f->scale, 64.0f * f->scale, 0, 0, 127, 127,
		     f->rotation, a255(200.0f * f->alpha), AOS_BLEND_ADD_ALPHA);
		break;
	case 200: { /* smoke puff: sprite 0xa4 frame min(age/12,3), half 32*scale (DAT_008140dc) */
		int fr = f->age / 12;
		if (fr > 3)
			fr = 3;
		int tex = aos_texf("mr_smk0%d", fr);
		float h = 32.0f * f->scale;
		blit(tex, bank, f->pos, h, h, 0, 0, 127, 127, f->rotation, a255(255.0f * f->alpha), AOS_BLEND_ALPHA);
		aos_dcmd *c = blit(tex, bank, f->pos, h, h, 0, 0, 127, 127, f->rotation, a255(255.0f * f->alpha),
		                   AOS_BLEND_ADD_ALPHA);
		int g = (int)(128.0f * f->alpha) & 0xff; /* DAT_00814134 */
		c->rgba = (uint32_t)g << 24 | (uint32_t)g << 16 | (uint32_t)g << 8 | (c->rgba & 0xffu);
		break;
	}
	case 300: { /* unused by Mira's code: common explosion atlas DAT_0087fccc[variant/8] */
		int v8 = f->variant % 8;
		int row = v8 % 4, col = v8 / 4;
		int tex = aos_tex(f->variant / 8 == 0 ? "explosion0" : "explosion1");
		float h = 180.0f * f->scale; /* DAT_00815258 */
		/* rotation passed without the sign flip: world rotation = -(+0x38) */
		blit(tex, AOS_BANK_COMMON, f->pos, h, h, (float)(col * 256), (float)(row * 128),
		     (float)(col * 256 + 127), (float)(row * 128 + 127), -f->rotation, a255(128.0f * f->alpha),
		     AOS_BLEND_ADD_ALPHA);
		h = 200.0f * f->scale; /* DAT_008147cc */
		if (f->age % 2 == 0)
			h *= 0.85f;
		blit(tex, AOS_BANK_COMMON, f->pos, h, h, (float)(col * 256 + 128), (float)(row * 128),
		     (float)(col * 256 + 255), (float)(row * 128 + 127), -f->rotation, a255(255.0f * f->alpha),
		     AOS_BLEND_ADD_ALPHA);
		break;
	}
	case 400: /* ninja-star trail: sprite 0x9e frame 0, half 32*scale, alpha 200a, blend 2 */
		blit(aos_texf("mr_blade0%d", 0), bank, f->pos, 32.0f * f->scale, 32.0f * f->scale, 0, 0, 127, 127,
		     f->rotation, a255(200.0f * f->alpha), AOS_BLEND_ADD_ALPHA);
		break;
	case 500: /* smoke cloud: sprite 0xa5 frame (age/3)%6, half 24*scale (DAT_00853b8c), blend 1 */
		blit(aos_texf("mr_smkball0%d", (f->age / 3) % 6), bank, f->pos, 24.0f * f->scale, 24.0f * f->scale, 0, 0,
		     127, 127, f->rotation, a255(255.0f * f->alpha), AOS_BLEND_ALPHA);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------------------ */
/* input dispatch FUN_006fbb60 (scheme A) / FUN_006fc090 (scheme B) */

static bool dispatch(aos_world *w, aos_player *p, bool schemeB)
{
	int ch = chakram_find(w, p->side);
	if (aos_may_hyper(w, p)) {
		bool hS, hB, hD, hH;
		if (!schemeB) {
			if (p->S > 1 && p->B > 1 && p->H == 3 && aos_spend_hyper(p, 3.0f)) {
				aos_set_state(w, p, 0x9d);
				return true;
			}
			hS = p->S > 1 && p->H == 3;
			hB = p->B > 1 && p->H == 3;
			hD = p->D > 1 && p->H == 3;
			hH = p->H == 3;
		} else {
			if (p->H > 1 && p->Shield == 3 && aos_spend_hyper(p, 3.0f)) {
				aos_set_state(w, p, 0x9d);
				return true;
			}
			hS = p->H > 1 && p->S == 3;
			hB = p->H > 1 && p->B == 3;
			hD = p->H > 1 && p->D == 3;
			hH = p->H > 1 && p->A == 3;
		}
		if (hS && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, ch == -1 ? 0x9a : 0x93);
			return true;
		}
		if (hB && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, ch == -1 ? 0x8f : 0x92);
			return true;
		}
		if (hD && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x8e);
			return true;
		}
		if (hH && aos_spend_hyper(p, 1.0f)) {
			aos_set_state(w, p, 0x94);
			return true;
		}
	}
	if (p->state == AOS_ST_NEUTRAL || p->state == AOS_ST_DASH) {
		if (p->D == 3) {
			aos_dash(w, p, 0);
		} else if (p->Shield == 3 && aos_spend_hyper(p, 0.5f) && aos_may_shield(w, p)) {
			aos_shield(w, p);
		} else {
			if (p->S > 1 && p->B == 3) {
				aos_set_state(w, p, ch == -1 ? 0x8d : 0x90);
				return true;
			}
			if (p->S > 1 && p->A == 3) {
				aos_set_state(w, p, ch == -1 ? 0x95 : 0x91);
				return true;
			}
			if (p->B == 3) {
				aos_set_state(w, p, 0x8c);
				return true;
			}
			if (p->D > 1 && p->A == 3) {
				aos_set_state(w, p, 0x88);
				return true;
			}
			if (p->A == 3) {
				aos_set_state(w, p, 0x86);
				return true;
			}
		}
	}
	return aos_tab.T2[p->state] ? aos_tab.T2[p->state](w, p) : true;
}

static bool input_a(aos_world *w, aos_player *p) { return dispatch(w, p, false); } /* FUN_006fbb60 */
static bool input_b(aos_world *w, aos_player *p) { return dispatch(w, p, true); }  /* FUN_006fc090 */

/* ------------------------------------------------------------------ */
/* states (T2)                                                         */

static void dash_or_end(aos_world *w, aos_player *p, int cancelFrom, int endAfter)
{
	if (p->timer < cancelFrom || p->D != 3) {
		if (p->timer > endAfter)
			p->state = AOS_ST_NEUTRAL;
	} else {
		aos_dash(w, p, 1);
	}
}

static bool st_neutral(aos_world *w, aos_player *p) /* 0x7c T2 FUN_006f69a0 */
{
	(void)w;
	float s = aos_tab.stats[p->id].walk;
	if (p->up > 1) p->y += s;
	if (p->down > 1) p->y -= s;
	if (p->left > 1) p->x -= s;
	if (p->right > 1) p->x += s;
	p->faceAngle = p->facingRight == 1 ? 0.0f : AOS_PI;
	return true;
}

static bool st_stub(aos_world *w, aos_player *p) { (void)w; (void)p; return true; } /* 0x7d/0x7f/0x80 */

static bool st_ko_hook(aos_world *w, aos_player *p) /* 0x7e T2 FUN_006f6ac0 */
{
	if (p->timer == 0) {
		for (int i = 0; i < w->obj.nenemy; i++) {
			aos_enemy *e = &w->obj.enemy[i];
			if (e->ownerId != p->objId)
				continue;
			if (e->f7c == 100 || e->f7c == 0xb4 || e->f7c == 0xb9 || e->f7c == 0xbe) {
				e->f7c = 0xb4;
				e->f84 = 900;
				e->timer2 = 0;
				e->active = 0;
			}
		}
	}
	return true;
}

static void spawn_star_pair(aos_world *w, aos_player *p, float s)
{
	aos_spawn_beam(w, ID, 200, &p->x, p->faceAngle - s, p->objId, 0, p->side);
	aos_spawn_beam(w, ID, 0xc9, &p->x, p->faceAngle + s, p->objId, 0, p->side);
}

static bool st_ninja_stars(aos_world *w, aos_player *p) /* 0x86 T2 FUN_006f6be0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->faceAngle;
	}
	if (p->timer > 9 && p->timer < 0x16) {
		float s = aos_tab.stats[p->id].walk * 0.75f;
		if (p->up > 1) p->y += s;
		if (p->down > 1) p->y -= s;
		if (p->left > 1) p->x -= s;
		if (p->right > 1) p->x += s;
	}
	if (p->timer == 0xc && aos_use_gauge(p, 0x86)) {
		p->faceAngle = aos_aim(w, p);
		/* third branch (s = 2.0) re-tests the same condition — dead code, 0x6f6d70..0x6f6e4e */
		spawn_star_pair(w, p, input_toward(p, p->faceAngle) ? 1.0f : 1.5f);
		aos_se(w, 0xd);
	}
	if (p->timer == 0x14 && p->A > 1)
		p->timer = 10;
	else
		dash_or_end(w, p, 0x14, 0x23);
	return true;
}

static bool st_caltrops(aos_world *w, aos_player *p) /* 0x88 T2 FUN_006f7020 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->faceAngle;
	}
	if (p->timer >= 10 && p->timer <= 0x15 && aos_use_gauge(p, 0x88)) {
		p->faceAngle = aos_aim(w, p);
		static const float off[12] = {-0.54f, 0.54f, -0.33f, 0.33f, -0.12f, 0.12f,
		                              0.09f, -0.09f, 0.3f, -0.3f, 0.51f, -0.51f};
		aos_spawn_beam(w, ID, 300, &p->x, p->faceAngle + off[p->timer - 10], p->objId, 0, p->side);
		aos_se_cd(w, 0xf, 4);
	}
	if (p->timer < 0x19 || p->D != 3) {
		if (p->timer > 0x28)
			p->state = AOS_ST_NEUTRAL;
		p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static void kunai3(aos_world *w, aos_player *p)
{
	for (int k = -1; k < 2; k++) {
		int m = aos_spawn_missile(w, ID, 100, &p->x, (float)k * 0.15f + p->faceAngle, p->objId, 0, p->side, p->color);
		if (m >= 0)
			whip_spawn(w, 0, p->color, w->obj.missile[m].serial);
	}
	aos_se(w, 0x1f);
}

static bool st_kunai1(aos_world *w, aos_player *p) /* 0x89 T2 FUN_006f73a0 */
{
	if (p->timer == 0xc && aos_use_gauge(p, 0x89))
		kunai3(w, p);
	if (p->timer >= 0xc && p->B == 3 && aos_use_gauge(p, 0x8a))
		aos_set_state(w, p, 0x8a);
	else
		dash_or_end(w, p, 0x19, 0x28);
	return true;
}

static void kunai_entry(aos_world *w, aos_player *p)
{
	if (p->timer == 0) {
		float a = aos_dir_angle(p);
		if (a < AOS_2PI) {
			p->moveAngle = a;
			p->speed = 16.0f;
			p->heat += 0.2f; /* v>0x5a0 */
			aos_se(w, 8);
		}
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->faceAngle;
	}
	if (p->speed >= 3.0f)
		sparks(w, p, -0.3f, 0.3f, p->moveAngle);
	if (p->timer == 0xc) {
		p->faceAngle = aos_aim(w, p);
		kunai3(w, p);
	}
}

static bool st_kunai2(aos_world *w, aos_player *p) /* 0x8a T2 FUN_006f7560 */
{
	kunai_entry(w, p);
	if (p->timer >= 0xc && p->B == 3 && aos_use_gauge(p, 0x8b))
		aos_set_state(w, p, 0x8b);
	else
		dash_or_end(w, p, 0x1e, 0x28);
	return true;
}

static bool st_kunai3(aos_world *w, aos_player *p) /* 0x8b T2 FUN_006f7890 */
{
	kunai_entry(w, p);
	dash_or_end(w, p, 0x14, 0x1e);
	return true;
}

static bool st_kiri(aos_world *w, aos_player *p) /* 0x8c T2 FUN_006f7b50 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer < 0xd && p->B < 1) {
		p->faceAngle = aos_aim(w, p);
		p->state = 0x89;
		if (p->timer > 10)
			p->timer = 10;
		aos_tab.T2[0x89](w, p);
		return true;
	}
	for (int k = 0; k < 4; k++) {
		if (p->timer == k * 2 + 0xf && aos_use_gauge(p, 0x8c)) {
			int m = aos_spawn_missile(w, ID, 200, &p->x, p->faceAngle + 1.4f + (float)k * 0.2f, p->objId, 0, p->side, p->color);
			if (m >= 0)
				whip_spawn(w, 1, p->color, w->obj.missile[m].serial);
			m = aos_spawn_missile(w, ID, 200, &p->x, (p->faceAngle - 1.4f) - (float)k * 0.2f, p->objId, 0, p->side, p->color);
			if (m >= 0)
				whip_spawn(w, 1, p->color, w->obj.missile[m].serial);
			aos_se_cd(w, 0x23, 4);
		}
	}
	dash_or_end(w, p, 0x19, 0x23);
	return true;
}

static int smoke_ball(aos_world *w, aos_player *p)
{
	int i = aos_spawn_enemy(w, ID, 200, p->objId, p->side, p->color);
	if (i >= 0) {
		aos_enemy *e = &w->obj.enemy[i];
		e->pos[0] = p->x;
		e->pos[1] = p->y;
		e->pos[2] = p->z;
	}
	return i;
}

static bool st_binding_smoke(aos_world *w, aos_player *p) /* 0x8d T2 FUN_006f8ab0 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 10 && aos_use_gauge(p, 0x8d)) {
		int i = smoke_ball(w, p);
		if (i >= 0) {
			w->obj.enemy[i].angle2 = p->faceAngle;
			w->obj.enemy[i].f70 = 10.0f;
		}
		aos_se(w, 0x1f);
	}
	dash_or_end(w, p, 0x14, 0x23);
	return true;
}

/* FUN_006b1b40(p,10), SE 6, dark(100,.75), [speedline(100,pos,slA,slS)], blur(20,.5,2), [effect 0xfa] */
static void hyper_start(aos_world *w, aos_player *p, bool flash, bool sl, float slA, float slS)
{
	aos_barrier(w, p, 10);
	aos_hyper_flourish_ex(w, p, 6, 100, 0.75f, sl ? 100 : 0, slA, slS, 0x14, 0.5f, 2, flash);
}

static bool st_smokescreen(aos_world *w, aos_player *p) /* 0x8e T2 FUN_006f8c80 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, true, true, 0.0f, 0.0f);
	}
	for (int k = 0; k < 8; k++) {
		if (p->timer == k * 3 + 10) {
			int i = smoke_ball(w, p);
			float base = ((float)(k % 4) / 4.0f) * AOS_2PI;
			float r = aos_rand_real(w, -0.75f, 0.75f); /* always drawn, unused in v>0x5a0 */
			(void)r;
			if (i >= 0) {
				aos_enemy *e = &w->obj.enemy[i];
				e->angle2 = p->timer < 0x14 ? base : base + 1.0f;
				e->f70 = 10.0f;
			}
		}
	}
	dash_or_end(w, p, 0x28, 0x3c);
	return true;
}

static bool st_circling(aos_world *w, aos_player *p) /* 0x8f T2 FUN_006f7de0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, true, true, 0.0f, 0.0f);
	}
	if (p->timer == 5) {
		for (int k = 0; k < 4; k++) {
			int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
			if (i < 0)
				continue;
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
			e->angle2 = ((float)k / 4.0f) * AOS_2PI;
			e->f70 = 0;
			set_e74(e, 0);
		}
	}
	dash_or_end(w, p, 0x28, 0x32);
	return true;
}

static void launch_at_opp(aos_world *w, aos_player *p, aos_enemy *e, int32_t beh, int32_t sub)
{
	aos_player *o = aos_opponent(w, p);
	e->f10 = 0xb4;
	e->f7c = beh;
	e->timer2 = 0;
	e->f84 = sub;
	e->angle2 = aos_atan2(o->y - e->pos[1], o->x - e->pos[0]);
	e->f70 = -16.0f;
}

static bool st_soaring(aos_world *w, aos_player *p) /* 0x90 T2 FUN_006f8010 */
{
	if (p->timer == 0)
		p->faceAngle = aos_aim(w, p);
	if (p->timer == 0xc) {
		int ch = chakram_find(w, p->side);
		if (ch == -1 || !aos_use_gauge(p, 0x90)) {
			p->timer = 0xf;
		} else {
			launch_at_opp(w, p, &w->obj.enemy[ch], 0xb4, 0);
			aos_se(w, 0x20);
		}
	}
	if (p->timer == 0xe && p->B > 1)
		p->timer = 6;
	else
		dash_or_end(w, p, 0xf, 0x1e);
	return true;
}

static bool st_swift(aos_world *w, aos_player *p) /* 0x91 T2 FUN_006f8240 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		set_p90(p, 0);
		p->speed = 0;
	}
	if (p->timer == 8) {
		set_p90(p, 20.0f);
		p->heat += 0.5f;
		aos_se(w, 0x20);
	}
	p->x = aos_cos(p->faceAngle) * p90(p) + p->x;
	p->y = aos_sin(p->faceAngle) * p90(p) + p->y;
	set_p90(p, p90(p) * 0.95f);
	dash_or_end(w, p, 0x28, 0x32);
	return true;
}

static bool st_sky_soaring(aos_world *w, aos_player *p) /* 0x92 T2 FUN_006f83d0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, false, true, 0.0f, 0.0f);
	}
	if (p->timer == 1) {
		int ch = chakram_find(w, p->side);
		while (ch != -1) {
			aos_enemy *e = &w->obj.enemy[ch];
			e->f10 = 0xbe;
			e->f7c = 0xbe;
			e->timer2 = 0;
			e->f84 = 0;
			e->angle2 = aos_atan2(e->pos[1] - p->y, e->pos[0] - p->x);
			e->f70 = -16.0f;
			ch = chakram_find(w, p->side);
		}
	}
	dash_or_end(w, p, 0x28, 0x32);
	return true;
}

static bool st_baiting(aos_world *w, aos_player *p) /* 0x93 T2 FUN_006f85f0 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, false, true, p->faceAngle, 0.7853982f); /* DAT_0081fa90 */
	}
	if (p->timer > 9 && p->timer < 0x24 && p->timer % 5 == 0) {
		int i = aos_spawn_enemy(w, ID, 100, p->objId, p->side, p->color);
		float R = aos_rand_real(w, 100.0f, 150.0f);
		float ph = aos_rand_real(w, -0.5f, 0.5f) + 1.5707964f;
		R = 125.0f; /* v>0x5a0: both draws discarded */
		ph = 1.5707964f;
		if (p->timer % 10 == 0)
			ph = ph * -1.0f;
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = aos_cos(ph) * R + p->x;
			e->pos[1] = aos_sin(ph) * R + p->y;
			e->f10 = 0xb4;
			e->f7c = 0xb9;
			e->f84 = 0;
			e->angle2 = p->faceAngle;
			e->f70 = -16.0f;
			set_e74(e, 0.5f);
		}
	}
	if (p->timer == 0x28) {
		int ch = chakram_find(w, p->side);
		if (ch != -1 && aos_use_gauge(p, 0x90)) {
			aos_enemy *e = &w->obj.enemy[ch];
			launch_at_opp(w, p, e, 0xb9, 100);
			set_e74(e, 0.5f);
			aos_se(w, 0x20);
			p->timer = 0x23;
		}
	}
	dash_or_end(w, p, 0x32, 0x50);
	return true;
}

static float tornado_angle(aos_world *w, aos_player *p)
{
	float a = aos_dir_angle(p);
	if (AOS_2PI < a) {
		aos_player *o = aos_opponent(w, p);
		a = aos_atan2(o->y - p->y, o->x - p->x);
	}
	return a;
}

static bool st_tornado(aos_world *w, aos_player *p) /* 0x94 T2 FUN_006f8f90 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, true, true, 0.0f, 0.0f);
	}
	if (p->timer == 0xf) {
		float a = tornado_angle(w, p), o[3];
		tornado_origin(w, p, a, o);
		aos_spawn_laser(w, ID, 100, o, a + 1.5707964f, p->objId, p->side, p->color);
	}
	dash_or_end(w, p, 0x46, 100);
	return true;
}

static bool st_qs_approach(aos_world *w, aos_player *p) /* 0x95 T2 FUN_006f9540 */
{
	mira_global *g = glob(w);
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		g->lunge = aos_dist(w, p) / 12.0f;
		if (14.0f <= g->lunge)
			g->lunge = 14.0f;
		p->dashAngle = p->faceAngle;
		p->moveAngle = p->faceAngle;
		p->speed = g->lunge;
	}
	if (p->timer < 0xb) {
		p->moveAngle = p->faceAngle;
		p->speed = g->lunge;
		p->hurtShape[0] = p->hurtShape[1] = p->hurtShape[2] = 0;
	} else {
		aos_set_state(w, p, 0x97);
	}
	return true;
}

static bool st_qs_through(aos_world *w, aos_player *p) /* 0x96 T2 FUN_006f96a0 */
{
	mira_global *g = glob(w);
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
		g->lunge = aos_dist(w, p) / 10.0f;
		int i = aos_spawn_enemy(w, ID, 300, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->angle = p->faceAngle;
			e->angle2 = p->faceAngle;
			e->pos[0] = p->x;
			e->pos[1] = p->y;
			e->pos[2] = p->z;
		}
		aos_se(w, 0x2c);
	}
	if (p->timer < 0xb) {
		p->moveAngle = p->faceAngle;
		p->speed = 5.0f + g->lunge;
	} else {
		aos_set_state(w, p, 0x98);
	}
	return true;
}

static void slash_box(aos_world *w, aos_player *p, int32_t id, float off)
{
	float pos[3] = {aos_cos(p->faceAngle) * off + p->x, aos_sin(p->faceAngle) * off + p->y, p->z};
	/* z of the original local vector is uninitialised stack; hitbox z is never read (FUN_005f1c90 is 2-D) */
	aos_spawn_hitbox(w, ID, id, pos, p->faceAngle, p->objId, 0, p->side);
}

static bool st_qs1(aos_world *w, aos_player *p) /* 0x97 T2 FUN_006f9850 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 1)
		aos_se(w, 0x1f);
	if (p->timer == 2 && aos_use_gauge(p, 0x97))
		slash_box(w, p, 300, 20.0f);
	if (p->timer >= 8 && p->hitFlag == 1 && p->A == 3) {
		freeze_opp(w, p);
		aos_set_state(w, p, 0x96);
	} else {
		dash_or_end(w, p, 0x1f, 0x24);
	}
	return true;
}

static bool st_qs2(aos_world *w, aos_player *p) /* 0x98 T2 FUN_006f9d20 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		float l = aos_dist(w, p) / 15.0f;
		if (10.0f <= l)
			l = 10.0f;
		p->dashAngle = p->faceAngle;
		p->moveAngle = p->faceAngle;
		p->speed = l;
	}
	if (p->timer == 1)
		aos_se(w, 0x1f);
	if (p->timer == 4 && aos_use_gauge(p, 0x98))
		slash_box(w, p, 0x136, 20.0f);
	if (p->timer >= 8 && p->hitFlag == 1 && p->A == 3)
		aos_set_state(w, p, 0x99);
	else
		dash_or_end(w, p, 0x1a, 0x24);
	return true;
}

static bool st_qs3(aos_world *w, aos_player *p) /* 0x99 T2 FUN_006fa190 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 0;
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 16.0f;
		aos_se(w, 0x20);
	}
	if (p->timer == 10 && aos_use_gauge(p, 0x99))
		slash_box(w, p, 0x140, 10.0f);
	if (p->timer > 0x13 && p->D == 3) {
		aos_dash(w, p, 1);
		return true;
	}
	if (p->timer > 0x23)
		p->state = AOS_ST_NEUTRAL;
	p->friction *= 0.5f;
	return true;
}

static bool st_divine(aos_world *w, aos_player *p) /* 0x9a T2 FUN_006fa520 */
{
	if (p->timer == 0) {
		p->faceAngle = aos_aim(w, p);
		hyper_start(w, p, true, false, 0.0f, 0.0f); /* no FUN_007bec20 in this state */
	}
	if (p->timer == 10) {
		p->faceAngle = aos_aim(w, p);
		float l = aos_dist(w, p) / 12.0f;
		if (16.0f <= l)
			l = 16.0f;
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = l;
	}
	if (p->timer == 0xe)
		aos_spawn_hitbox(w, ID, 400, &p->x, p->faceAngle, p->objId, 0, p->side);
	if (p->timer > 0xe && p->timer < 0x15 && p->hitFlag == 0)
		aos_spawn_hitbox(w, ID, 400, &p->x, p->faceAngle, p->objId, 0, p->side);
	if (p->speed >= 3.0f)
		sparks(w, p, -0.5f, 0.5f, p->faceAngle);
	if (p->timer == 8)
		aos_se(w, 0x1f);
	if (p->timer == 0x14 && p->hitFlag == 1) {
		freeze_opp(w, p);
		aos_player *o = aos_opponent(w, p); /* FUN_00780480(team) */
		aos_camera_focus(w, 0x50, o->objId, &o->x, 0.4f); /* DAT_00813f30 */
		aos_speedline(w, 100, &p->x, p->faceAngle, 0.0f);
		p->state = 0x9b;
		p->timer = 0;
		p->hitFlag = 0;
		aos_se(w, 0x2b);
		aos_tab.T2[0x9b](w, p);
	}
	dash_or_end(w, p, 0x28, 0x3c);
	return true;
}

static bool st_divine_clones(aos_world *w, aos_player *p) /* 0x9b T2 FUN_006faa10 */
{
	if (p->timer > 9 && p->timer < 0x29 && p->timer % 5 == 0) {
		float r = aos_rand_real(w, -AOS_PI, AOS_PI);
		aos_player *o = aos_opponent(w, p);
		freeze_opp(w, p);
		int i = aos_spawn_enemy(w, ID, ((p->timer - 10) / 5) % 3 + 400, p->objId, p->side, p->color);
		if (i >= 0) {
			aos_enemy *e = &w->obj.enemy[i];
			e->pos[0] = aos_cos(r) * 80.0f + o->x;
			e->pos[1] = aos_sin(r) * 80.0f + o->y;
			e->angle = (o->x <= e->pos[0]) ? AOS_PI : 0.0f;
			e->angle2 = e->angle;
		}
		aos_se_cd(w, 0x2f, 4);
	}
	if (p->timer > 0x3c) {
		p->state = 0x9c;
		p->timer = 0;
		p->hitFlag = 0;
		aos_barrier(w, p, 10);
		aos_se(w, 0x2c);
		aos_tab.T2[0x9c](w, p);
	}
	p->hittable = 0;
	return true;
}

static bool st_divine_finish(aos_world *w, aos_player *p) /* 0x9c T2 FUN_006facc0 */
{
	if (p->timer == 0) {
		aos_player *o = aos_opponent(w, p);
		float a = aos_atan2(-o->y, -o->x);
		p->x = o->x - aos_cos(a) * 60.0f;
		p->y = o->y - aos_sin(a) * 60.0f;
		p->faceAngle = (o->x <= p->x) ? AOS_PI : 0.0f;
		p->dashAngle = p->faceAngle;
		freeze_opp(w, p);
	}
	if (p->timer == 0xf) {
		p->faceAngle = aos_aim(w, p);
		p->dashAngle = p->moveAngle = p->faceAngle;
		p->speed = 16.0f;
		aos_se(w, 0x20);
		aos_spawn_hitbox(w, ID, 0x1a4, &p->x, p->faceAngle, p->objId, 0, p->side);
	}
	if (p->speed >= 8.0f)
		sparks(w, p, -0.5f, 0.5f, p->faceAngle);
	if (p->timer < 0x1c || p->D != 3) {
		if (p->timer > 0x2a)
			p->state = AOS_ST_NEUTRAL;
		p->friction *= 0.5f;
	} else {
		aos_dash(w, p, 1);
	}
	return true;
}

static bool st_twin_dragon(aos_world *w, aos_player *p) /* 0x9d T2 FUN_006fb0e0 */
{
	if (p->timer == 0) {
		face_x(w, p);
		aos_barrier(w, p, 0x14);
		aos_se(w, 7);
		aos_screen_dark(w, 0x96, 0.75f); /* DAT_00813f3c */
		aos_speedline(w, 100, &p->x, p->faceAngle, 0.0f);
		aos_screen_blur(w, 0x96, 0.75f, 2);
		fx0(w, 0xfa, 0, &p->x, 0, p->objId, p->side);
	}
	if (p->timer == 0xf) {
		float a = tornado_angle(w, p), o[3];
		tornado_origin(w, p, a, o);
		aos_spawn_laser(w, ID, 0x96, o, a + 1.5707964f, p->objId, p->side, p->color);
		a = a + 1.5707964f;
		tornado_origin(w, p, a, o);
		aos_spawn_laser(w, ID, 0x96, o, a + 1.5707964f, p->objId, p->side, p->color);
	}
	dash_or_end(w, p, 0x5a, 100);
	return true;
}

/* ------------------------------------------------------------------ */
/* draw handlers (T1)                                                  */

/* pp(n) = (t/n)&3 with 3 -> 1 */
static int pp(int t, int n)
{
	int f = (t / n) & 3;
	return f == 3 ? 1 : f;
}

/* cyc(n) = n<2 ? n : ((n-2)&3)+2, 5 -> 3 */
static int cyc(int n)
{
	if (n < 2)
		return n;
	int f = ((n - 2) & 3) + 2;
	return f == 5 ? 3 : f;
}

static int clampi(int v, int hi) { return v > hi ? hi : v; }

/* AIM transform (suguri.md §4.2): rotZ(aim) [+rotX(π) if |aim| >= π/2] + rotY(π). */
static aos_dcmd *draw_aim(aos_world *w, aos_player *p, const char *pat, int frame)
{
	aos_dcmd *c = aos_draw(aos_texf(pat, frame), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->rotZ = aos_rad2bam(p->faceAngle);
	c->flipX = 1;
	if (aos_fabs(p->faceAngle) >= 1.5707964f)
		c->sy = -c->sy; /* X-rotation by π approximated as y-scale × cos(π) */
	return c;
	/* cosmetic: FUN_004e8f50 global tint (white in play) */
}

/* FUN_004d7e00(model, 1, p, rot) after an AIM draw: rot = {RotX, RotY(π), RotZ(aim)}. */
static void after_aim(aos_world *w, aos_player *p, const aos_dcmd *c)
{
	int32_t rot[3] = {aos_fabs(p->faceAngle) >= 1.5707964f ? aos_deg2bam(0xb4) : 0, aos_deg2bam(0xb4),
	                  aos_rad2bam(p->faceAngle)};
	aos_afterimage(w, c, 1, p, rot);
}

static void dr_neutral(aos_world *w, aos_player *p) /* 0x7c T1 FUN_006fd4d0 */
{
	const char *pat = "mr_std0%d";
	if (p->up > 1) pat = "mr_fwd0%d";
	if (p->down > 1) pat = "mr_bck0%d";
	if (!p->facingRight && p->left > 1) pat = "mr_fwd0%d";
	if (p->facingRight == 1 && p->left > 1) pat = "mr_bck0%d";
	if (!p->facingRight && p->right > 1) pat = "mr_bck0%d";
	if (p->facingRight == 1 && p->right > 1) pat = "mr_fwd0%d";
	aos_dcmd *c = aos_draw(aos_texf(pat, pp(p->timer, 5)), aos_pidx(w, p), p->x, p->y, 0);
	c->halfW = c->halfH = 48.0f;
	c->flipX = p->facingRight != 0; /* RotY(π) when +0x8c != 0 */
	int32_t rot[3] = {0, p->facingRight != 0 ? aos_rad2bam(AOS_PI) : 0, 0};
	aos_afterimage(w, c, 1, p, rot); /* FUN_004d7e00 */
}

static void dr_dash(aos_world *w, aos_player *p) /* 0x7d T1 FUN_006fd790 */
{
	int f = pp(p->timer, 3);
	float aa = aos_fabs(p->faceAngle);
	if ((p->facingRight == 1 && aa > 1.5707964f) || (p->facingRight == 0 && aa < 1.5707964f))
		f += 3;
	after_aim(w, p, draw_aim(w, p, "mr_dsh0%d", f));
}

static void dr_ko(aos_world *w, aos_player *p) /* 0x7e T1 FUN_006fe140 */
{
	if (p->timer >= 100)
		return;
	draw_aim(w, p, "mr_los0%d", pp(p->timer, 3) + (w->frame % 2 == 0 ? 3 : 0));
}

static void dr_hit(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_los0%d", pp(p->timer, 3)); }      /* 0x7f T1 FUN_006fdb90 */
static void dr_shield(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_sld0%d", cyc(p->timer / 5)); } /* 0x80 T1 FUN_006fde60 */
static void dr_atcka(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_atcka0%d", clampi(p->timer / 4, 4)); } /* 0x86/0x88/0x89/0x8c */
static void dr_atckb(aos_world *w, aos_player *p) { after_aim(w, p, draw_aim(w, p, "mr_atckb0%d", clampi(p->timer / 4, 3))); } /* 0x8a FUN_006fec80 */
static void dr_atckc(aos_world *w, aos_player *p) { after_aim(w, p, draw_aim(w, p, "mr_atckc0%d", clampi(p->timer / 4, 4))); } /* 0x8b FUN_006ff000 */
static void dr_smoke(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_sld0%d", clampi(p->timer / 5, 4)); }   /* 0x8d FUN_007005b0 */
static void dr_smokescreen(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_sld0%d", 0); }                  /* 0x8e FUN_00700870 */

static void dr_circling(aos_world *w, aos_player *p) /* 0x8f T1 FUN_006ff640 */
{
	draw_aim(w, p, "mr_sld0%d", p->timer < 5 ? 0 : clampi((p->timer - 5) / 5, 4));
}

static void dr_soaring(aos_world *w, aos_player *p) /* 0x90 T1 FUN_006ff920 */
{
	draw_aim(w, p, "mr_atcka0%d", p->timer < 6 ? 0 : clampi((p->timer - 6) / 4, 4));
}

static void dr_swift(aos_world *w, aos_player *p) /* 0x91 T1 FUN_006ffc00 */
{
	if (p->timer < 8)
		after_aim(w, p, draw_aim(w, p, "mr_sld0%d", 0));
	else
		after_aim(w, p, draw_aim(w, p, "mr_dsh0%d", pp(p->timer, 3)));
}

static void dr_sky(aos_world *w, aos_player *p) /* 0x92/0x93 T1 FUN_006fffb0 / FUN_007002b0 */
{
	draw_aim(w, p, "mr_sld0%d", p->timer < 10 ? 0 : cyc((p->timer - 10) / 5));
}

static void dr_tornado(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_sld0%d", cyc(p->timer / 5)); } /* 0x94/0x9d */

static void dr_qs_approach(aos_world *w, aos_player *p) /* 0x95 T1 FUN_00700df0 */
{
	aos_dcmd *c = draw_aim(w, p, "mr_bck0%d", pp(p->timer, 2));
	c->rgba = aos_rgba(0, 0, 0, 1); /* +0xdc(a=1, 0,0,0): black silhouette */
	after_aim(w, p, c);
}

static void dr_none(aos_world *w, aos_player *p) { (void)w; (void)p; } /* 0x96 T1 FUN_007011d0 (empty) */
static void dr_qs1(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_infghta0%d", clampi(p->timer / 5, 3)); } /* 0x97/0x9a */
static void dr_qs2(aos_world *w, aos_player *p) { draw_aim(w, p, "mr_infghtb0%d", clampi(p->timer / 5, 3)); } /* 0x98 */

static void dr_qs3(aos_world *w, aos_player *p) /* 0x99 T1 FUN_00701760 */
{
	draw_aim(w, p, "mr_infghtc0%d", p->timer < 10 ? 0 : clampi((p->timer - 10) / 4 + 1, 5));
}

static void dr_clones(aos_world *w, aos_player *p) /* 0x9b T1 FUN_00701d00 */
{
	if (p->timer >= 0x14)
		return;
	aos_dcmd *c = draw_aim(w, p, "mr_sld0%d", clampi(p->timer / 4, 4));
	c->rgba = aos_rgba(0, 0, 0, 1);
	c->sx *= 1.0f - (float)p->timer / 20.0f;
	c->sy *= (float)p->timer / 10.0f;
}

static void dr_finish(aos_world *w, aos_player *p) /* 0x9c T1 FUN_007020f0 */
{
	aos_dcmd *c;
	if (p->timer < 10) {
		c = draw_aim(w, p, "mr_sld0%d", clampi(p->timer / 4, 4));
		c->rgba = aos_rgba(0, 0, 0, 1);
		c->sx *= (float)p->timer / 10.0f;
		c->sy *= 2.0f - (float)p->timer / 10.0f;
	} else if (p->timer < 0xf) {
		c = draw_aim(w, p, "mr_infghtc0%d", 0);
	} else {
		c = draw_aim(w, p, "mr_infghtc0%d", clampi((p->timer - 0xf) / 4, 5));
	}
	after_aim(w, p, c);
}

/* ------------------------------------------------------------------ */

void aos_register_mira(void)
{
	aos_tab.inputA[ID] = input_a;
	aos_tab.inputB[ID] = input_b;
	/* CPU AI FUN_006fce20 / FUN_006fc5c0 not ported (not reversed) */

	aos_tab.T2[0x7c] = st_neutral;
	aos_tab.T2[0x7d] = st_stub;
	aos_tab.T2[0x7e] = st_ko_hook;
	aos_tab.T2[0x7f] = st_stub;
	aos_tab.T2[0x80] = st_stub;
	aos_tab.T2[0x86] = st_ninja_stars;
	aos_tab.T2[0x88] = st_caltrops;
	aos_tab.T2[0x89] = st_kunai1;
	aos_tab.T2[0x8a] = st_kunai2;
	aos_tab.T2[0x8b] = st_kunai3;
	aos_tab.T2[0x8c] = st_kiri;
	aos_tab.T2[0x8d] = st_binding_smoke;
	aos_tab.T2[0x8e] = st_smokescreen;
	aos_tab.T2[0x8f] = st_circling;
	aos_tab.T2[0x90] = st_soaring;
	aos_tab.T2[0x91] = st_swift;
	aos_tab.T2[0x92] = st_sky_soaring;
	aos_tab.T2[0x93] = st_baiting;
	aos_tab.T2[0x94] = st_tornado;
	aos_tab.T2[0x95] = st_qs_approach;
	aos_tab.T2[0x96] = st_qs_through;
	aos_tab.T2[0x97] = st_qs1;
	aos_tab.T2[0x98] = st_qs2;
	aos_tab.T2[0x99] = st_qs3;
	aos_tab.T2[0x9a] = st_divine;
	aos_tab.T2[0x9b] = st_divine_clones;
	aos_tab.T2[0x9c] = st_divine_finish;
	aos_tab.T2[0x9d] = st_twin_dragon;

	aos_tab.T1[0x7c] = dr_neutral;
	aos_tab.T1[0x7d] = dr_dash;
	aos_tab.T1[0x7e] = dr_ko;
	aos_tab.T1[0x7f] = dr_hit;
	aos_tab.T1[0x80] = dr_shield;
	aos_tab.T1[0x86] = dr_atcka;
	aos_tab.T1[0x88] = dr_atcka;
	aos_tab.T1[0x89] = dr_atcka;
	aos_tab.T1[0x8a] = dr_atckb;
	aos_tab.T1[0x8b] = dr_atckc;
	aos_tab.T1[0x8c] = dr_atcka;
	aos_tab.T1[0x8d] = dr_smoke;
	aos_tab.T1[0x8e] = dr_smokescreen;
	aos_tab.T1[0x8f] = dr_circling;
	aos_tab.T1[0x90] = dr_soaring;
	aos_tab.T1[0x91] = dr_swift;
	aos_tab.T1[0x92] = dr_sky;
	aos_tab.T1[0x93] = dr_sky;
	aos_tab.T1[0x94] = dr_tornado;
	aos_tab.T1[0x95] = dr_qs_approach;
	aos_tab.T1[0x96] = dr_none;
	aos_tab.T1[0x97] = dr_qs1;
	aos_tab.T1[0x98] = dr_qs2;
	aos_tab.T1[0x99] = dr_qs3;
	aos_tab.T1[0x9a] = dr_qs1;
	aos_tab.T1[0x9b] = dr_clones;
	aos_tab.T1[0x9c] = dr_finish;
	aos_tab.T1[0x9d] = dr_tornado;

	aos_otab.beamSpawn[ID] = beam_spawn;
	aos_otab.beamUpdate[ID] = beam_update;
	aos_otab.beamDraw[ID] = beam_draw;
	aos_otab.beamOnHit[ID] = beam_on_hit;
	aos_otab.missileSpawn[ID] = missile_spawn;
	aos_otab.missileUpdate[ID] = missile_update;
	aos_otab.missileDraw[ID] = missile_draw;
	aos_otab.missileOnHit[ID] = missile_on_hit;
	aos_otab.missileOnKilled[ID] = missile_on_killed;
	aos_otab.missileOnAbsorb[ID] = NULL; /* 0x881b60 = 0 */
	aos_otab.hitboxSpawn[ID] = hitbox_spawn;
	aos_otab.hitboxOnHit[ID] = hitbox_on_hit;
	aos_otab.laserSpawn[ID] = laser_spawn;
	aos_otab.laserUpdate[ID] = laser_update;
	aos_otab.laserDraw[ID] = laser_draw;
	aos_otab.laserOnHit[ID] = laser_on_hit;
	aos_otab.enemySpawn[ID] = enemy_spawn;
	aos_otab.enemyUpdate[ID] = enemy_update;
	aos_otab.enemyDraw[ID] = enemy_draw;
	aos_otab.enemyOnKilled[ID] = enemy_on_killed;
	aos_otab.effectSpawn[ID] = effect_spawn;
	aos_otab.effectUpdate[ID] = effect_update;
	aos_otab.effectDraw[ID] = effect_draw;
	aos_otab.whipSpawn[ID] = whip_spawn;
	aos_otab.whipUpdate[ID] = whip_update;
	aos_otab.whipDraw[ID] = whip_draw;
}
