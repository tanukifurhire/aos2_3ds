/* Object families (docs/re/objects.md §1–2). Each family is an
 * order-preserving fixed array that mirrors the original std::vector:
 * push_back appends, erase shifts, and iteration re-reads the count so
 * objects spawned mid-walk run in the same frame. Field names follow
 * objects.md; original offsets in comments. Included by sim.h. */
#ifndef AOS_OBJECTS_H
#define AOS_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "fxstate.h"
#define AOS_HAVE_FXSTATE 1

typedef struct {
	float halfW, halfH, radius; /* rect if halfW > 0, circle if radius > 0 */
} aos_shape;

typedef struct {               /* 0x98 */
	int32_t type;              /* +0x00 owner char index */
	int32_t id;                /* +0x04 per-character bullet id */
	int32_t age;               /* +0x08 */
	int32_t ownerId;           /* +0x0c */
	int32_t hitType;           /* +0x10 */
	int32_t team;              /* +0x14 */
	int32_t f18;               /* +0x18 */
	int32_t serial;            /* +0x1c */
	int32_t link;              /* +0x20 */
	float pos[3];              /* +0x24 */
	float f30[9];              /* +0x30..+0x53 per-type scratch */
	uint8_t grazed;            /* +0x54 */
	uint8_t destroyOnHit;      /* +0x55 */
	float scale;               /* +0x58 */
	float alpha;               /* +0x5c */
	float moveAngle;           /* +0x60 */
	float angle;               /* +0x64 */
	float speed;               /* +0x68 */
	float f6c;                 /* +0x6c */
	uint8_t active;            /* +0x70 */
	aos_shape shape;           /* +0x74 */
	float f80;                 /* +0x80 */
	int32_t damage;            /* +0x84 */
	float knockback;           /* +0x88 */
	float stun;                /* +0x8c */
	int32_t rehitTimer;        /* +0x90 */
	uint8_t rehitFlag;         /* +0x94 */
} aos_beam;

typedef struct {               /* 0xb8 */
	int32_t type, id, age, ownerId; /* +0x00 .. +0x0c */
	int32_t hitType;           /* +0x10 (2) */
	int32_t team;              /* +0x14 */
	int32_t f18;               /* +0x18 */
	int32_t p18;               /* +0x1c owner palette */
	int32_t serial;            /* +0x20 */
	int32_t link;              /* +0x24 */
	float pos[3];              /* +0x28 */
	float f34[9];              /* +0x34..+0x57 per-type scratch */
	uint8_t grazed;            /* +0x58 */
	uint8_t f59;               /* +0x59 */
	float growScale;           /* +0x5c */
	float f60;                 /* +0x60 */
	int32_t f64;               /* +0x64 */
	float moveAngle;           /* +0x68 */
	float angle;               /* +0x6c */
	float speed;               /* +0x70 */
	float f74;                 /* +0x74 */
	int32_t hp;                /* +0x78 */
	uint8_t active;            /* +0x7c */
	aos_shape shapeBody;       /* +0x80 */
	float f8c;
	aos_shape shapeProj;       /* +0x90 */
	float f9c;
	int32_t damage;            /* +0xa0 */
	float knockback;           /* +0xa4 */
	float stun;                /* +0xa8 */
	uint8_t absorbs;           /* +0xac */
	int32_t rehitTimer;        /* +0xb0 */
	uint8_t rehitFlag;         /* +0xb4 */
} aos_missile;

typedef struct {               /* 0x4c */
	uint8_t dealsDamage;       /* +0x00 */
	uint8_t oncePerFrame;      /* +0x01 */
	int32_t type, id, f0c, ownerId, hitType, team; /* +0x04 .. +0x18 */
	float pos[3];              /* +0x1c */
	float angle;               /* +0x28 */
	aos_shape shape;           /* +0x2c */
	float f38;
	int32_t damage;            /* +0x3c */
	float knockback;           /* +0x40 */
	float stun;                /* +0x44 */
	uint8_t rehitFlag;         /* +0x48 */
} aos_hitbox;

#define AOS_LASER_SEGS 64
typedef struct {
	aos_shape shape;
	float pad;
	float pos[3];
} aos_seg;

typedef struct {               /* 0x50 + segments */
	int32_t type, id;
	int32_t blockMode;         /* +0x08 (1 = not cut by bodies) */
	int32_t hitType;           /* +0x0c */
	int32_t team, p18, ownerId;/* +0x10 +0x14 +0x18 */
	int32_t f1c;
	int32_t age;               /* +0x20 */
	float origin[3];           /* +0x24 */
	float angle;               /* +0x30 */
	int32_t nseg;              /* +0x34 vector<Seg> */
	aos_seg seg[AOS_LASER_SEGS];
	int32_t damage;            /* +0x40 */
	float knockback;           /* +0x44 */
	int32_t f48;               /* +0x48 */
	uint8_t stun;              /* +0x4c */
	uint8_t f4d, f4e;
	float scratch[8];          /* port: per-type scratch */
} aos_laser;

typedef struct {               /* 0xec */
	int32_t id;                /* +0x00 unique */
	int32_t serial;            /* +0x04 */
	int32_t ownerId;           /* +0x08 */
	int32_t type;              /* +0x0c */
	int32_t f10;               /* +0x10 spawn id */
	int32_t team, p18;         /* +0x14 +0x18 */
	int32_t drawLayer;         /* +0x1c */
	uint8_t solid;             /* +0x20 */
	uint8_t invulnerable;      /* +0x21 */
	int32_t hp, maxHp;         /* +0x24 +0x28 */
	uint8_t active;            /* +0x2c */
	aos_shape shape;           /* +0x30 */
	float f3c;
	float pos[3];              /* +0x40 */
	float prevPos[3];          /* +0x4c */
	float angle, angle2;       /* +0x58 +0x5c */
	float scale[3];            /* +0x60 */
	uint8_t mirrorChildren;    /* +0x6c */
	float f70;                 /* +0x70 */
	union { int32_t f74; float f74f; }; /* +0x74: int or float depending on the character (f74f = float view) */
	int32_t age;               /* +0x78 */
	int32_t f7c;
	int32_t timer2;            /* +0x80 */
	int32_t f84;
	int32_t motionId;          /* +0x88 */
	int32_t motionFrame;       /* +0x8c */
	int32_t parentId;          /* +0x90 */
	int32_t childSlots[10];    /* +0x94 */
	float pivot[2];            /* +0xbc */
	float fc4;
	float offset[2];           /* +0xc8 */
	float fd0;
	float fd4[3];              /* +0xd4..+0xdc */
	float fe0;                 /* +0xe0 */
	int32_t rehitTimer;        /* +0xe4 */
	uint8_t fe8[4];
	float scratch[8];          /* port: per-type scratch */
} aos_enemy;

typedef struct {               /* effect record 0x44 */
	int32_t type, id, variant, ownerId, team, drawLayer; /* +0x00 .. +0x14 */
	float pos[3];              /* +0x18 */
	int32_t f24;
	int32_t age;               /* +0x28 */
	float scale, alpha;        /* +0x2c +0x30 */
	float moveDir;             /* +0x34 */
	float rotation;            /* +0x38 */
	float aux, aux2;           /* +0x3c +0x40 */
} aos_effect;

#define AOS_WHIP_NODES 20
typedef struct {               /* 0x244 */
	int32_t id, type, ownerId, age, f10, p3;
	float f18[3];
	float node[AOS_WHIP_NODES][7];
} aos_whip;

typedef struct {               /* particle 0x38 */
	int32_t kind, colour, age, owner;
	float alpha, spin[2];
	float pos[3];
	float dir, speed, growth, scale;
} aos_particle;

#define AOS_SPARK_PTS 48
typedef struct {
	int32_t kind, colour, maxLen, age, owner;
	float alpha;
	float heading, target, turn, speed;
	int32_t npts;
	float pts[AOS_SPARK_PTS][2];
} aos_spark;

typedef struct {               /* message popup 0x18 */
	int32_t kind;
	float pos[3];
	int32_t owner, timer;
} aos_popuprec;

#define AOS_MAX_BEAM 768 /* logical cap (the original's) */
#define AOS_BEAM_SLOTS (2 * AOS_MAX_BEAM) /* physical slots, see aos_beams_update */
#define AOS_MAX_MISSILE 192
#define AOS_MAX_HITBOX 512
#define AOS_MAX_LASER 64
#define AOS_MAX_ENEMY 256
#define AOS_MAX_EFFECT 2048 /* logical cap (the original's) */
#define AOS_EFFECT_SLOTS (2 * AOS_MAX_EFFECT) /* physical slots, see aos_effects_update */
#define AOS_MAX_WHIP 64
#define AOS_MAX_PARTICLE 768
#define AOS_MAX_SPARK 512
#define AOS_MAX_POPUP 32
#define AOS_MAX_HITREG 256

/* Dead pool slots (index >= count) are kept all-zero: every place that shrinks
 * a pool clears the slots it vacates. Nothing in the simulation reads them, and
 * it lets a rollback snapshot store only the live prefix of each pool and still
 * restore the world byte for byte (src/net/rollback.c). AOS_DEAD_FILL is a test
 * hook: tests/run_golden.sh poison fills vacated slots with garbage instead to
 * prove no code reads them. */
#ifndef AOS_DEAD_FILL
#define AOS_DEAD_FILL 0
#endif
#define AOS_CLEAR_DEAD(ptr, nbytes) memset((ptr), AOS_DEAD_FILL, (nbytes))

typedef struct {
	int32_t nbeam, nmissile, nhitbox, nlaser, nenemy, neffect, nwhip, nparticle, nspark, npopup;
	aos_beam beam[AOS_BEAM_SLOTS];
	aos_missile missile[AOS_MAX_MISSILE];
	aos_hitbox hitbox[AOS_MAX_HITBOX];
	aos_laser laser[AOS_MAX_LASER];
	aos_enemy enemy[AOS_MAX_ENEMY];
	aos_effect effect[AOS_EFFECT_SLOTS];
	aos_whip whip[AOS_MAX_WHIP];
	aos_particle particle[AOS_MAX_PARTICLE];
	aos_spark spark[AOS_MAX_SPARK];
	aos_popuprec popup[AOS_MAX_POPUP];
	int32_t hitreg[AOS_MAX_HITREG][2]; /* (id, ownerId) once-per-frame registry */
	int32_t nhitreg;
	int32_t beamSerial, missileSerial, enemyId, enemySerial; /* DAT_00885988/8c/80/84 */
	int32_t overflow;          /* port: count of dropped spawns (pool full) */
	aos_fxstate fx;            /* screen effects + camera focus list (fx.c) */
	/* camera (FUN_005f8b90) */
	float camX, camY, zoom;
	float zoomView;            /* zoom after the focus-list blend (display only; `zoom` carries over) */
} aos_objects;

struct aos_world;
struct aos_player;

/* Per-type (= owner char index) function tables. */
typedef int (*aos_beam_spawn_fn)(struct aos_world *, int32_t id, const float pos[3], float angle, int32_t ownerId, int32_t age0, int32_t team);
typedef bool (*aos_beam_update_fn)(struct aos_world *, aos_beam *);
typedef void (*aos_beam_draw_fn)(struct aos_world *, aos_beam *);
typedef void (*aos_beam_hit_fn)(struct aos_world *, aos_beam *);
typedef int (*aos_missile_spawn_fn)(struct aos_world *, int32_t id, const float pos[3], float angle, int32_t ownerId, int32_t age0, int32_t team, int32_t p18);
typedef bool (*aos_missile_update_fn)(struct aos_world *, aos_missile *);
typedef void (*aos_missile_draw_fn)(struct aos_world *, aos_missile *);
typedef bool (*aos_missile_cb_fn)(struct aos_world *, aos_missile *); /* onHit / onKilled: return keep */
typedef void (*aos_missile_absorb_fn)(struct aos_world *, aos_missile *, aos_beam *);
typedef int (*aos_hitbox_spawn_fn)(struct aos_world *, int32_t id, const float pos[3], float angle, int32_t ownerId, int32_t zero, int32_t team);
typedef void (*aos_hitbox_hit_fn)(struct aos_world *, aos_hitbox *, struct aos_player *);
typedef int (*aos_laser_spawn_fn)(struct aos_world *, int32_t id, const float pos[3], float angle, int32_t ownerId, int32_t team, int32_t p18);
typedef bool (*aos_laser_update_fn)(struct aos_world *, aos_laser *);
typedef void (*aos_laser_draw_fn)(struct aos_world *, aos_laser *);
typedef void (*aos_laser_hit_fn)(struct aos_world *, aos_laser *, int32_t seg, struct aos_player *);
typedef int (*aos_enemy_spawn_fn)(struct aos_world *, int32_t id, int32_t ownerId, int32_t team, int32_t p18);
typedef bool (*aos_enemy_update_fn)(struct aos_world *, aos_enemy *);
typedef void (*aos_enemy_draw_fn)(struct aos_world *, aos_enemy *);
typedef bool (*aos_enemy_killed_fn)(struct aos_world *, aos_enemy *);
typedef int (*aos_effect_spawn_fn)(struct aos_world *, int32_t id, int32_t variant, const float pos[3], float angle, int32_t ownerId, int32_t age0, int32_t team);
typedef bool (*aos_effect_update_fn)(struct aos_world *, aos_effect *);
typedef void (*aos_effect_draw_fn)(struct aos_world *, aos_effect *);
typedef int (*aos_whip_spawn_fn)(struct aos_world *, int32_t id, int32_t ownerId, int32_t p3);
typedef bool (*aos_whip_update_fn)(struct aos_world *, aos_whip *);
typedef void (*aos_whip_draw_fn)(struct aos_world *, aos_whip *);

#define AOS_NTYPES 19
typedef struct {
	aos_beam_spawn_fn beamSpawn[AOS_NTYPES];       /* 0x881878 */
	aos_beam_update_fn beamUpdate[AOS_NTYPES];     /* 0x8818c8 */
	aos_beam_draw_fn beamDraw[AOS_NTYPES];         /* 0x881918 */
	aos_beam_hit_fn beamOnHit[AOS_NTYPES];         /* 0x881968 */
	aos_missile_spawn_fn missileSpawn[AOS_NTYPES]; /* 0x8819b8 */
	aos_missile_update_fn missileUpdate[AOS_NTYPES];
	aos_missile_draw_fn missileDraw[AOS_NTYPES];
	aos_missile_cb_fn missileOnHit[AOS_NTYPES];    /* 0x881aa8 */
	aos_missile_cb_fn missileOnKilled[AOS_NTYPES]; /* 0x881af8 */
	aos_missile_absorb_fn missileOnAbsorb[AOS_NTYPES]; /* 0x881b48 */
	aos_hitbox_spawn_fn hitboxSpawn[AOS_NTYPES];   /* 0x881b98 */
	aos_hitbox_hit_fn hitboxOnHit[AOS_NTYPES];     /* 0x881be8 */
	aos_laser_spawn_fn laserSpawn[AOS_NTYPES];     /* 0x881c38 */
	aos_laser_update_fn laserUpdate[AOS_NTYPES];
	aos_laser_draw_fn laserDraw[AOS_NTYPES];
	aos_laser_hit_fn laserOnHit[AOS_NTYPES];       /* 0x881d28 */
	aos_enemy_spawn_fn enemySpawn[AOS_NTYPES];     /* 0x881558 */
	aos_enemy_update_fn enemyUpdate[AOS_NTYPES];   /* 0x8815f8 */
	aos_enemy_draw_fn enemyDraw[AOS_NTYPES];       /* 0x8815a8 */
	aos_enemy_killed_fn enemyOnKilled[AOS_NTYPES]; /* 0x881648 */
	aos_effect_spawn_fn effectSpawn[AOS_NTYPES];   /* 0x881698 (type 0 = common) */
	aos_effect_update_fn effectUpdate[AOS_NTYPES]; /* 0x8816e8 */
	aos_effect_draw_fn effectDraw[AOS_NTYPES];     /* 0x881738 */
	aos_whip_spawn_fn whipSpawn[AOS_NTYPES];       /* 0x881d78 */
	aos_whip_update_fn whipUpdate[AOS_NTYPES];
	aos_whip_draw_fn whipDraw[AOS_NTYPES];
} aos_obj_tables;

extern aos_obj_tables aos_otab;

/* ---- generic object helpers (objects.c) ---- */
bool aos_shape_hit(const float posA[3], const float posB[3], const aos_shape *a, const aos_shape *b,
                   float angleA, float angleB);               /* FUN_005f1c90 */
bool aos_in_arena(const float pos[3], float margin);          /* FUN_0061ac10 */
int32_t aos_enemy_group_next(struct aos_world *w); /* FUN_0055efc0: call once per summon spawn, store in e->serial */
bool aos_point_in_tri(const float *a, const float *b, const float *c, const float *p); /* FUN_005f1820 */

/* Allocate a zeroed slot at the end of a family; returns index or -1 if full.
 * The *_init helpers apply the original template initialisers. */
int aos_beam_new(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                 int32_t ownerId, int32_t age0, int32_t team);            /* + FUN_004ba490 */
int aos_missile_new(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                    int32_t ownerId, int32_t age0, int32_t team, int32_t p18); /* + FUN_00685bb0 */
int aos_hitbox_new(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                   int32_t ownerId, int32_t zero, int32_t team);
int aos_laser_new(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle,
                  int32_t ownerId, int32_t team, int32_t p18);             /* + FUN_006307d0 */
int aos_enemy_new(struct aos_world *w, int32_t type, int32_t ownerId, int32_t team, int32_t p18); /* + FUN_0055ee70 */
int aos_whip_new(struct aos_world *w, int32_t type, int32_t id, int32_t ownerId, int32_t p3); /* zeroed slot, -1 if full */
int aos_effect_new(struct aos_world *w, int32_t type, int32_t id, int32_t variant, const float pos[3],
                   float angle, int32_t ownerId, int32_t age0, int32_t team);
bool aos_laser_push_seg(aos_laser *l, const aos_shape *s, const float pos[3]);
aos_enemy *aos_enemy_by_id(struct aos_world *w, int32_t id);  /* FUN_0055f060 */
struct aos_player *aos_player_by_objid(struct aos_world *w, int32_t objId); /* FUN_007805f0 */

/* Spawn through the per-type tables (what player states call). */
int aos_spawn_beam(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team);
int aos_spawn_missile(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team, int32_t p18);
int aos_spawn_hitbox(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t zero, int32_t team);
int aos_spawn_laser(struct aos_world *w, int32_t type, int32_t id, const float pos[3], float angle, int32_t owner, int32_t team, int32_t p18);
int aos_spawn_enemy(struct aos_world *w, int32_t type, int32_t id, int32_t owner, int32_t team, int32_t p18);
int aos_spawn_effect(struct aos_world *w, int32_t type, int32_t id, int32_t variant, const float pos[3], float angle, int32_t owner, int32_t age0, int32_t team);

/* Per-frame managers in tick order. */
void aos_enemies_update(struct aos_world *w);   /* FUN_005231d0 */
void aos_beams_update(struct aos_world *w);     /* FUN_004aa460 */
void aos_missiles_update(struct aos_world *w);  /* FUN_0065e2f0 */
void aos_lasers_update(struct aos_world *w);    /* FUN_0061cbf0 */
void aos_effects_update(struct aos_world *w);   /* FUN_004fde70 */
void aos_whips_update(struct aos_world *w);     /* FUN_007c4fb0 */
void aos_collisions(struct aos_world *w);       /* passes 1..17 */
void aos_enemies_cleanup(struct aos_world *w);  /* FUN_00523370 */
void aos_rehit_timers(struct aos_world *w);     /* tick step 12 */
void aos_camera_update(struct aos_world *w);    /* FUN_005f8b90 */

#endif
