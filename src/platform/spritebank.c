#include "spritebank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tex3ds.h>

#define MAX_FOLDERS 24
#define MAX_PAGES 32
#define NAME_LEN 32

typedef struct {
	char name[NAME_LEN];
	aos_frame f;
} entry;

/* One atlas page. Folders that stay loaded for the whole session (the common
 * effect/system/beam/... atlases, spritebank_load_resident) go to VRAM while
 * it has room: that keeps ~4.5 MB out of the 32 MB linear heap, which every
 * character atlas, stage texture and SE buffer competes for, and VRAM is also
 * the faster place for the GPU to sample from. Everything else is in linear
 * memory as before (C2D_SpriteSheetLoad did the same import). */
typedef struct {
	Tex3DS_Texture t3x;
	C3D_Tex tex;
	bool vram;
} page_t;

#define VRAM_RESERVE (256 * 1024) /* left free for later users (nothing in a match allocates VRAM after the atlases) */
#ifndef MIP_LOD_BIAS
#define MIP_LOD_BIAS -0.5f /* sharper: switch to the next mip level only past ~1.4x minification */
#endif

static page_t *page_load(const char *path, bool try_vram)
{
	page_t *pg = calloc(1, sizeof *pg);
	if (!pg)
		return NULL;
	for (int pass = try_vram ? 0 : 1; pass < 2; pass++) {
		FILE *f = fopen(path, "rb");
		if (!f)
			break;
		bool vram = pass == 0;
		if (vram) { /* the t3x header gives the size: only try if it fits with the reserve */
			uint8_t h[6];
			size_t sz = 0;
			if (fread(h, 1, sizeof h, f) == sizeof h)
				sz = (size_t)(8u << (h[2] & 7)) * (8u << ((h[2] >> 3) & 7)) * (h[3] == GPU_ETC1A4 || h[3] == GPU_L8 || h[3] == GPU_A8 || h[3] == GPU_LA4 ? 2 : h[3] == GPU_ETC1 ? 1 : 4) / 2;
			if (sz && h[4]) /* mipmap chain */
				sz = sz * 4 / 3;
			fseek(f, 0, SEEK_SET);
			if (!sz || vramSpaceFree() < sz + VRAM_RESERVE) {
				fclose(f);
				continue;
			}
		}
		pg->t3x = Tex3DS_TextureImportStdio(f, &pg->tex, NULL, vram);
		fclose(f);
		if (pg->t3x) {
			pg->vram = vram;
			/* what C2D_SpriteSheetLoad does after the import: clamp to a 0 border */
			C3D_TexSetWrap(&pg->tex, GPU_CLAMP_TO_BORDER, GPU_CLAMP_TO_BORDER);
			pg->tex.border = 0;
			/* Bilinear: the import leaves both filters at NEAREST, which made
			 * rotated sprites (missiles, beams) shimmer between pixel patterns
			 * (64-texel missiles are ~20 px on screen) instead of turning smoothly;
			 * character atlases also carry mipmaps (aos2pack mipmapped()), which is
			 * what anti-aliases those minified sprites. Draws clamp their texture
			 * coordinates to the frame's texel centres (spritebank_clamp_sub) so
			 * atlas neighbours don't bleed in. Filtering is free on the PICA200. */
			C3D_TexSetFilter(&pg->tex, GPU_LINEAR, GPU_LINEAR);
			if (pg->tex.maxLevel > 0) {
				C3D_TexSetFilterMipmap(&pg->tex, GPU_LINEAR);
				C3D_TexSetLodBias(&pg->tex, MIP_LOD_BIAS);
			}
			return pg;
		}
	}
	free(pg);
	return NULL;
}

static void page_free(page_t *pg)
{
	if (!pg)
		return;
	Tex3DS_TextureFree(pg->t3x);
	C3D_TexDelete(&pg->tex);
	free(pg);
}

static size_t page_count(const page_t *pg) { return Tex3DS_GetNumSubTextures(pg->t3x); }

static C2D_Image page_image(page_t *pg, size_t i)
{
	C2D_Image img = {&pg->tex, Tex3DS_GetSubTexture(pg->t3x, i)};
	return img;
}

typedef struct {
	char folder[NAME_LEN];
	page_t *pages[MAX_PAGES];
	int npages;
	entry *entries; /* sorted by name (the .idx is written sorted) */
	int nentries;
} folder_t;

static folder_t folders[MAX_FOLDERS];

static folder_t *find_folder(const char *name)
{
	for (int i = 0; i < MAX_FOLDERS; i++)
		if (folders[i].entries && !strcmp(folders[i].folder, name))
			return &folders[i];
	return NULL;
}

static void free_folder(folder_t *fo)
{
	for (int p = 0; p < fo->npages; p++)
		page_free(fo->pages[p]);
	free(fo->entries);
	memset(fo, 0, sizeof *fo);
}

static bool load_folder(const char *folder, bool try_vram);

/* Character atlases also try VRAM: with the reduced common atlases (docs/PERF.md, texture memory plan) a
 * character's pages fit there; otherwise they land in linear memory as before. */
bool spritebank_load(const char *folder) { return load_folder(folder, true); }
bool spritebank_load_resident(const char *folder) { return load_folder(folder, true); }

static bool load_folder(const char *folder, bool try_vram)
{
	if (find_folder(folder))
		return true;
	folder_t *fo = NULL;
	for (int i = 0; i < MAX_FOLDERS && !fo; i++)
		if (!folders[i].entries)
			fo = &folders[i];
	if (!fo || strlen(folder) >= NAME_LEN)
		return false;

	char path[96];
	snprintf(path, sizeof path, "romfs:/gfx/%s.idx", folder);
	FILE *f = fopen(path, "r");
	if (!f)
		return false;
	int cap = 128, n = 0;
	entry *es = malloc(sizeof *es * (size_t)cap);
	char name[NAME_LEN], line[128];
	int page, idx, w, h, ow, oh, premul, maxpage = -1;
	bool ok = es != NULL;
	while (ok && fgets(line, sizeof line, f)) {
		premul = 0;
		if (sscanf(line, "%31s %d %d %d %d %d %d %d", name, &page, &idx, &w, &h, &ow, &oh, &premul) < 7)
			continue;
		if (page < 0 || page >= MAX_PAGES || idx < 0) {
			ok = false;
			break;
		}
		if (n == cap) {
			cap *= 2;
			entry *ne = realloc(es, sizeof *es * (size_t)cap);
			if (!ne) {
				ok = false;
				break;
			}
			es = ne;
		}
		memset(&es[n], 0, sizeof es[n]);
		strcpy(es[n].name, name);
		es[n].f.w = (u16)w;
		es[n].f.h = (u16)h;
		es[n].f.ow = (u16)ow;
		es[n].f.oh = (u16)oh;
		es[n].f.premul = (u8)(premul != 0);
		/* stash page/idx in img fields until the sheets are loaded */
		es[n].f.img.tex = NULL;
		es[n].f.img.subtex = (const Tex3DS_SubTexture *)(uintptr_t)((page << 16) | idx);
		if (page > maxpage)
			maxpage = page;
		n++;
	}
	fclose(f);
	if (!ok || n == 0) {
		free(es);
		return false;
	}
	strcpy(fo->folder, folder);
	fo->entries = es;
	fo->nentries = n;
	fo->npages = maxpage + 1;
	for (int p = 0; p < fo->npages; p++) {
		snprintf(path, sizeof path, "romfs:/gfx/%s_%d.t3x", folder, p);
		fo->pages[p] = page_load(path, try_vram);
		if (!fo->pages[p]) {
			free_folder(fo);
			return false;
		}
	}
	for (int i = 0; i < n; i++) {
		uintptr_t pi = (uintptr_t)es[i].f.img.subtex;
		int pg = (int)(pi >> 16), ix = (int)(pi & 0xffff);
		if ((size_t)ix >= page_count(fo->pages[pg])) {
			free_folder(fo);
			return false;
		}
		es[i].f.img = page_image(fo->pages[pg], (size_t)ix);
	}
	return true;
}

void spritebank_unload(const char *folder)
{
	folder_t *fo = find_folder(folder);
	if (fo)
		free_folder(fo);
}

static int cmp_entry(const void *key, const void *e)
{
	return strcmp((const char *)key, ((const entry *)e)->name);
}

const aos_frame *spritebank_get(const char *name)
{
	for (int i = 0; i < MAX_FOLDERS; i++) {
		if (!folders[i].entries)
			continue;
		const entry *e = bsearch(name, folders[i].entries, (size_t)folders[i].nentries, sizeof(entry), cmp_entry);
		if (e)
			return &e->f;
	}
	return NULL;
}

size_t spritebank_bytes(bool vram)
{
	size_t n = 0;
	for (int i = 0; i < MAX_FOLDERS; i++)
		for (int p = 0; folders[i].entries && p < folders[i].npages; p++)
			if (folders[i].pages[p] && folders[i].pages[p]->vram == vram)
				n += folders[i].pages[p]->tex.size;
	return n;
}

void spritebank_exit(void)
{
	for (int i = 0; i < MAX_FOLDERS; i++)
		if (folders[i].entries)
			free_folder(&folders[i]);
}

const aos_frame *spritebank_get_in(const char *folder, const char *name)
{
	for (int i = 0; i < MAX_FOLDERS; i++) {
		if (!folders[i].entries || strcmp(folders[i].folder, folder))
			continue;
		const entry *e = bsearch(name, folders[i].entries, (size_t)folders[i].nentries, sizeof(entry), cmp_entry);
		return e ? &e->f : NULL;
	}
	return NULL;
}
