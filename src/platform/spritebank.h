/* Sprite bank: loads the per-folder atlases produced by tools/aos2pack
 * (romfs:/gfx/<folder>.idx + <folder>_<page>.t3x) and looks frames up by
 * their original texture name (e.g. "sg_std00"). */
#ifndef AOS_SPRITEBANK_H
#define AOS_SPRITEBANK_H

#include <citro2d.h>
#include <stdbool.h>

typedef struct {
	C2D_Image img;
	u16 w, h;   /* atlas (scaled) size */
	u16 ow, oh; /* original PC texture size — draw at ow/w scale to get PC units */
} aos_frame;

/* Loads a folder (idempotent). Returns false if its index can't be read. */
bool spritebank_load(const char *folder);
/* Same, for folders kept for the whole session: pages go to VRAM while it has room. */
bool spritebank_load_resident(const char *folder);
/* Frees a folder's textures. */
void spritebank_unload(const char *folder);
/* Finds a frame by name in any loaded folder; NULL if not found. */
const aos_frame *spritebank_get(const char *name);
/* Find a frame by name in one specific loaded folder. */
const aos_frame *spritebank_get_in(const char *folder, const char *name);
void spritebank_exit(void);

/* Texture bytes held by the loaded folders in VRAM (true) or linear memory (false). */
size_t spritebank_bytes(bool vram);

#endif
