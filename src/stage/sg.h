/*
 * sg: "stage graphics" — a small, portable re-implementation of the parts of
 * the AoS2 3D engine (CModel / CMesh / CCamera / CPerticle, D3D9 render
 * states, the stage vertex shaders) that the stage backgrounds use.
 * docs/re/stages.md §3 documents every function mirrored here.
 *
 * Stage code (src/stage/st_*.c) is a literal port of the decompiled stage
 * functions written against this API. sg records every render call into a
 * command list (sg_cmds) that a backend executes:
 *   - 3DS: src/platform/stage.c (citro3d)
 *   - host: tests/stagepreview.c (software rasterizer -> PNG)
 *
 * Conventions (= D3D9/D3DX): row vectors, v' = v * M, left-handed, matrices
 * stored row-major m[row][col]. Angles passed to the CModel/CCamera rotate
 * methods are 16-bit binary angles (0x10000 = 2*pi, FUN_0045a0c0 / FUN_0045a0f0).
 */
#ifndef SG_H
#define SG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { float m[4][4]; } sg_mat;
typedef struct { float x, y, z; } sg_vec3;

/* ---------------------------------------------------------------- math */
void sg_mat_identity(sg_mat *o);
void sg_mat_mul(sg_mat *o, const sg_mat *a, const sg_mat *b); /* o = a*b (a applied first) */
bool sg_mat_inverse(sg_mat *o, const sg_mat *a);
uint16_t sg_rad2bam(float rad);  /* FUN_0045a0c0: (int)(rad*65536/2pi) & 0xffff */
uint16_t sg_deg2bam(int deg);    /* FUN_0045a0f0: deg*0x10000/360 */
float sg_bam_sin(uint32_t bam);  /* sine table DAT_00ddc0f0 */
float sg_bam_cos(uint32_t bam);

/* ---------------------------------------------------------------- textures */
/* FUN_0045a6f0(FUN_005df500(folder, name)): folder 2 = "/bgMdl/", 3 = "/bgMdlAdd/".
 * Returns a handle > 0, 0 on failure. Textures are refcounted per name. */
int sg_tex_load(int folder, const char *name);
void sg_tex_free(int tex);       /* CTexture release (vtable +4) */

/* ---------------------------------------------------------------- models */
/* CMesh+0x18c: vertex-colour fragment of the runtime-assembled vertex shader
 * (FUN_004928b0; fragment texts at 0x850c20.., stages.md §3.3). */
enum {
	SG_LIGHT_WHITE = 0x100,   /* oD0 = 1 */
	SG_LIGHT_VCOLOR = 0x200,  /* oD0 = vertex colour (unused by stages; treated as white) */
	SG_LIGHT_NONE = 0x400,    /* oD0 = material diffuse */
	SG_LIGHT_DIR = 0x800,     /* oD0 = min((N.L0 + ambient) * light colour * material, 1) (4 lights, only 0 used) */
	SG_LIGHT_HEMI = 0x1000,   /* directional * hemisphere (unused by stages; treated as SG_LIGHT_DIR) */
};
/* CMesh+0x194: fog fragment (FUN_00492a50). */
enum {
	SG_FOG_NONE = 0x10000000,
	SG_FOG_VIEW = 0x20000000, /* f = (end - z_view) / (end - start) */
	SG_FOG_WORLD = 0x40000000,/* f = (end - |p_world|^2) / (end - start) (literal: squared length) */
};

typedef struct {
	float *vtx;          /* nv * 8 floats: pos[3] normal[3] uv[2] */
	uint16_t *idx;       /* triangle list */
	int nv, ni;
	float bmin[3], bmax[3];
	void *gpu;           /* backend data */
} sg_geom;

typedef struct {
	sg_geom geom;
	int tex[2];          /* texture stage 0 (modulate) and 1 (add), 0 = none */
	float color[4];      /* material diffuse r,g,b,a (CModel +0xdc sets it) */
} sg_subset;

typedef struct {
	sg_subset *sub;      /* materials (CMesh +0x54 list) */
	int nsub;
	int lightType;       /* +0x18c (SG_LIGHT_*) */
	int fogType;         /* +0x194 (SG_FOG_*) */
	float uvOff[2];      /* +0x180 / +0x184 (added to every texcoord) */
} sg_mesh;

typedef struct sg_model {
	sg_mesh *mesh;       /* CModel+0xb4 list */
	int nmesh;
	float rot[3][3];     /* rows = model +0x84, +0x78, +0x6c */
	float pos[3];        /* +0x90 */
	float scale[3];      /* +0x9c */
	int billboard;       /* model +0xd8 (unused by stages) */
} sg_model;

/* Constructors (all return a model with lightType 0x800, fogType 0x10000000,
 * colour (1,1,1,1), no texture, identity transform — like FUN_00470aa0). */
sg_model *sg_sphere(float rx, float ry, float rz, int stacks, int slices); /* FUN_00468d80 */
sg_model *sg_plane(float w, float h);                                  /* FUN_004678c0 */
sg_model *sg_grid_xz(float w, float d, int nx, int nz, float uvScale);  /* FUN_004681f0 */
sg_model *sg_grid_xy(float w, float h, int nx, int ny, float uvScale);  /* FUN_00467d00 */
sg_model *sg_box(float w, float h, float d);                           /* FUN_004686e0 */
/* FUN_004742b0(name) + FUN_00469450(surface, split, sx, sz, height, uvRepeat):
 * height field from a /bgMdl/ grey-scale map (red channel / 255 * height). */
sg_model *sg_heightfield(const char *map, int split, float sx, float sz, float height, int uvRepeat);
sg_model *sg_xfile(const char *name);  /* FUN_00469e50("mdl\\<name>.x") */
void sg_model_free(sg_model *m);       /* vtable +4 */
/* Stage-specific geometry: allocates mesh[0].sub[0].geom with nv vertices
 * (8 floats each: pos[3], normal[3], uv[2]) and ni u16 indices; fill
 * geom.vtx / geom.idx, then call sg_custom_done (bounds + GPU upload). */
sg_model *sg_custom(int nv, int ni);
void sg_custom_done(sg_model *m);

/* CModel methods (vtable offset). mesh/mat index -1 = all. */
void sg_reset(sg_model *m);                        /* +0x38 */
void sg_rot_x(sg_model *m, uint32_t bam);          /* +0x50 */
void sg_rot_y(sg_model *m, uint32_t bam);          /* +0x54 */
void sg_rot_z(sg_model *m, uint32_t bam);          /* +0x58 */
void sg_pos(sg_model *m, float x, float y, float z);   /* +0x5c */
void sg_scale(sg_model *m, float x, float y, float z); /* +0x60 */
void sg_render(sg_model *m);                       /* +0x40 */
void sg_render_mesh(sg_model *m, int mesh);        /* (only mesh `mesh`) */
void sg_render_end(sg_model *m);                   /* +0x9c (no-op: clears D3D texture stages) */
void sg_render_end_parent(sg_model *m);            /* +0xa0 (see stages.md §3.2) */
void sg_set_tex(sg_model *m, int stage, int tex, int mesh, int mat); /* +0xb0 */
void sg_uv_u(sg_model *m, float u, int mesh);      /* +0xc0 */
void sg_uv_v(sg_model *m, float v, int mesh);      /* +0xc4 */
void sg_light_type(sg_model *m, int type, int mesh); /* +0xc8 */
void sg_fog_type(sg_model *m, int type, int mesh); /* +0xd0 */
void sg_color(sg_model *m, float a, float r, float g, float b, int mesh); /* +0xdc (a,r,g,b) */

/* ---------------------------------------------------------------- camera */
typedef struct {
	sg_mat view, proj;
	float right[3], up[3], fwd[3], eye[3]; /* +0x128, +0x11c, +0x110, +0x134 */
	float fovRad, nearZ, farZ, aspect;
} sg_camera;

void sg_cam_init(sg_camera *c);                     /* FUN_00465c30 */
/* +0x0c: fov in degrees (the stages pass (k*zoom+c)*0.64), near, far, aspect
 * (the stages pass DAT_009b1010 = window W/H; the 3DS backend substitutes its
 * own aspect — see stages.md §3.4). */
void sg_cam_persp(sg_camera *c, float fovDeg, float nearZ, float farZ, float aspect);
void sg_cam_reset(sg_camera *c);                    /* +0x20 */
void sg_cam_build(sg_camera *c);                    /* +0x24 (normalize axes, view matrix) */
void sg_cam_rot_y(sg_camera *c, uint32_t bam);      /* +0x28 */
void sg_cam_rot_x(sg_camera *c, uint32_t bam);      /* +0x2c */
void sg_cam_rot_z(sg_camera *c, uint32_t bam);      /* +0x30 */
void sg_cam_lookat(sg_camera *c, const float eye[3], const float at[3], const float *up); /* +0x44 */
void sg_cam_apply(sg_camera *c);                    /* +0x64 (SetTransform view/proj) */

/* ---------------------------------------------------------------- render state */
void sg_blend(int mode);              /* FUN_004637d0 (modes as in render.md) */
void sg_rs(int state, int value);     /* FUN_004636e0: D3DRS_* (7 Z, 0xe ZWRITE, 0x16 CULL, 0x1c FOG,
                                         0x1b ALPHABLEND, 0x13 SRCBLEND, 0x14 DESTBLEND) */
void sg_fog(float start, float end, uint32_t rgb); /* FUN_00463750 */
void sg_ambient(float a);             /* FUN_004676e0 */
void sg_light_color(int i, float r, float g, float b); /* FUN_00467700 */
void sg_light_dir(int i, const float d[3]);           /* FUN_00467750 (normalizes) */
void sg_light_enable(int i, int on);  /* FUN_004676b0 */
void sg_restore_2d(void);             /* FUN_004bfdd0: blend 1, cull none, fog off */

/* ---------------------------------------------------------------- particles (CPerticle) */
typedef struct sg_particles sg_particles;
sg_particles *sg_particles_new(int max);       /* FUN_00475660(max, kind) */
void sg_particles_free(sg_particles *p);
void sg_particles_tex(sg_particles *p, int tex);  /* +0x1c */
void sg_particles_begin(sg_particles *p);          /* +0x28 */
/* +0x20: camera-facing quad w*h at (x,y,z), source rect in texels (l,t,r,b), ARGB colour. */
void sg_particles_add(sg_particles *p, float x, float y, float z, float w, float h,
                      const int rect[4], uint32_t argb);
void sg_particles_end(sg_particles *p);            /* +0x2c (draw) */

/* ---------------------------------------------------------------- command list */
typedef struct {
	int clear;                   /* 0 = draw; 1 = colour clear (FUN_004639d0, colour in fogColor);
	                                2 = depth clear (FUN_00463a00) */
	const sg_geom *geom;         /* NULL for particle batches */
	const float *pvtx;           /* particles: n*6 vertices of pos[3] uv[2] rgba(u32) */
	int npv;
	sg_mat world;
	const sg_mat *view, *proj;   /* camera snapshot (sg_cmds.cam), shared by commands */
	float lightObj[3];           /* -(light0 dir in object space) */
	float lightColor[3], ambient;
	float color[4];              /* material */
	float uvOff[2];
	int tex[2];
	int lightType, fogType;
	int blend;                   /* FUN_004637d0 mode */
	int zenable, zwrite, cull;   /* D3DRS_ZENABLE / ZWRITEENABLE / CULLMODE (1 none, 2 cw, 3 ccw) */
	float fogStart, fogEnd;
	uint32_t fogColor;           /* 0xRRGGBB */
} sg_cmd;

#define SG_MAX_CMDS 1024
#define SG_MAX_CAMS 32
typedef struct {
	sg_cmd cmd[SG_MAX_CMDS];
	int n;
	struct { sg_mat view, proj; } cam[SG_MAX_CAMS]; /* one per sg_cam_apply this frame */
	int ncam;
} sg_cmdlist;
extern sg_cmdlist sg_cmds;
void sg_frame_begin(void);   /* reset the list (and per-frame state) */
void sg_clear(uint32_t argb);/* FUN_004639d0: clear the colour buffer */
void sg_clear_z(void);       /* FUN_00463a00(1.0): clear the depth buffer */

/* ---------------------------------------------------------------- backend hooks */
typedef struct {
	/* read a file below the stage asset root ("mdl/city.bin", "hf/dr_sand.bin");
	 * returns malloc'd buffer or NULL */
	void *(*read_file)(const char *rel, int *len);
	/* load texture "<folder>/<name>" (folder "bgMdl", "bgMdlAdd" or "3d"), return backend id > 0 */
	int (*tex_load)(const char *folder, const char *name);
	void (*tex_free)(int id);
	void (*geom_upload)(sg_geom *g);
	void (*geom_free)(sg_geom *g);
} sg_backend;
extern sg_backend sg_be;

/* All allocations of the stage (models, textures, particles) — freed by sg_free_all. */
void sg_free_all(void);

#endif
