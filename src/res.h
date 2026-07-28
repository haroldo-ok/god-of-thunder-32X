/* God of Thunder 32X - ROM-embedded resource access. */
#ifndef GOT_RES_H
#define GOT_RES_H

#include "mars.h"

#define RESK_RAW   0
#define RESK_TILES 1
#define RESK_PIC   2
#define RESK_ACTOR 3
#define RESK_PAL   4

typedef struct {
	char name[12];
	u32  offset;
	u32  size;
	u32  flags;
} res_entry_t;

/* Initialise from the archive linked into the ROM. Returns 0 on success. */
int          res_init(void);

/* Look a resource up by name. Returns a pointer straight into ROM (the data
   is already in its final, linear form) and stores the size. NULL if absent. */
const u8    *res_get(const char *name, u32 *size);

/* Convenience: name built from a prefix plus a number, eg. "ACTOR" + 12. */
const u8    *res_getn(const char *prefix, int num, u32 *size);

int          res_count(void);
const res_entry_t *res_entry(int idx);

#endif /* GOT_RES_H */
