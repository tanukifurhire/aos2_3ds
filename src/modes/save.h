/*
 * Persistent progress: unlocks, clear flags, lifetime wins (PC game.sys, 0xa4 bytes at
 * DAT_0087b728) and the arcade ranking (PC game.rkg, 0xca8 bytes at DAT_0087b7d0).
 * Spec: docs/re/modes.md §D.2.3, §D.5. Pure C (host-testable); the only I/O is
 * aos_save_load_file / aos_save_write_file (stdio).
 *
 * 3DS file: AOS_SAVE_PATH. Format (all little endian), version AOS_SAVE_FORMAT:
 *   0x000 "A2SV"            magic
 *   0x004 u16 format         AOS_SAVE_FORMAT (1)
 *   0x006 u16 game version   0x5a2
 *   0x008 u32 payload bytes  (0xa4 + 0xca8 + 4 = 0xd50 in format 1)
 *   0x00c u32 crc32          (IEEE, of the payload)
 *   0x010 payload:
 *         0x0a4 bytes  game.sys image, byte layout of DAT_0087b728 (see aos_save fields)
 *         0xca8 bytes  game.rkg image, 3 difficulties x 10 x {i32 score, i32 name[7], i32 char}
 *         u32          achievements bitmask (bit = Steam achievement index, §D.5; port extra)
 * A later format may append fields after the payload's known part; a reader takes the
 * prefix it knows and defaults the rest. Bad magic / crc / short file -> defaults.
 */
#ifndef AOS_MODES_SAVE_H
#define AOS_MODES_SAVE_H

#include <stddef.h>
#include <stdint.h>

#define AOS_SAVE_DIR "sdmc:/3ds/aos2_3ds"
#define AOS_SAVE_PATH AOS_SAVE_DIR "/progress.sav"
#define AOS_SAVE_FORMAT 1
#define AOS_SAVE_SYS_BYTES 0xa4
#define AOS_SAVE_RKG_BYTES 0xca8
#define AOS_SAVE_FILE_BYTES (16 + AOS_SAVE_SYS_BYTES + AOS_SAVE_RKG_BYTES + 4)

#define AOS_SAVE_CHARS 0x13  /* per-character arrays (index = engine char id) */
#define AOS_SAVE_STAGES 0x14
#define AOS_SAVE_BGMS 0x19
#define AOS_RANK_ENTRIES 10
#define AOS_RANK_NAME 7

typedef struct {
	int32_t score;               /* +0x00 */
	int32_t name[AOS_RANK_NAME]; /* +0x04 one letter index 0..45 per slot (FUN_00659480); new entries 45 = blank */
	int32_t character;           /* +0x20 engine id */
} aos_rank_entry;

typedef struct {
	/* game.sys (offsets from DAT_0087b728) */
	int32_t version;                          /* +0x00 0x5a2 (FUN_007c4450) */
	uint8_t charOpen[AOS_SAVE_CHARS];         /* +0x04 DAT_0087b72c + char */
	uint8_t stageOpen[AOS_SAVE_STAGES];       /* +0x17 DAT_0087b73f + stage */
	uint8_t bgmOpen[AOS_SAVE_BGMS];           /* +0x2b DAT_0087b753 + bgm */
	int32_t wins;                             /* +0x44 DAT_0087b76c fights won in arcade + story */
	int32_t arcadeClears[3];                  /* +0x48 DAT_0087b770[diff] no-continue arcade clears */
	int32_t storyClears;                      /* +0x54 DAT_0087b77c */
	uint8_t arcadeClear[3][AOS_SAVE_CHARS];   /* +0x58 DAT_0087b780 + diff*0x13 + char */
	uint8_t storyClear[AOS_SAVE_CHARS];       /* +0x91 DAT_0087b7b9 + char */
	/* game.rkg */
	aos_rank_entry rank[3][AOS_RANK_ENTRIES]; /* DAT_0087b7d0 [diff][pos] */
	/* port */
	uint32_t achievements;                    /* bit k = FUN_007ca890(k) was called */
	int dirty;                                /* set where the PC writes game.sys/game.rkg; app clears after writing */
} aos_save;

/* Fresh save: FUN_007c4480 (game.sys defaults) + FUN_00792a30 (ranking defaults). */
void aos_save_defaults(aos_save *s);
/* Ranking defaults only (FUN_00792a30 when game.rkg is missing). */
void aos_save_rank_defaults(aos_save *s);

/* Serialise to AOS_SAVE_FILE_BYTES bytes; returns bytes written (0 if cap too small). */
size_t aos_save_encode(const aos_save *s, uint8_t *buf, size_t cap);
/* Parse; on any error fills defaults and returns 0, else 1. */
int aos_save_decode(aos_save *s, const uint8_t *buf, size_t len);

/* File helpers (stdio; on 3DS paths start with sdmc:/). load: 1 ok, 0 missing/corrupt
 * (defaults filled; the PC then writes the defaults at once). write: creates the
 * directory, writes <path>.tmp and renames it over <path>; returns 1 on success. */
int aos_save_load_file(aos_save *s, const char *path);
int aos_save_write_file(const aos_save *s, const char *dir, const char *path);

void aos_save_achieve(aos_save *s, int id); /* FUN_007ca890 (port: bitmask only) */

#endif
