/*
 * God of Thunder 32X - resource access.
 *
 * All game assets are converted at build time (tools/mkassets.py) and linked
 * into the ROM as one flat, big-endian archive. Because the SH2 can execute
 * and read directly out of the cartridge address space, resources are used
 * in place: nothing is copied into the 32X's 256 KB of SDRAM unless the game
 * actually needs to mutate it (only the level/screen data does).
 */

#include "res.h"

/* Provided by the linker via the assets object (see Makefile). */
extern const u8 gotres_start[];

typedef struct {
	u32 magic;
	u32 count;
} res_hdr_t;

static const res_hdr_t   *s_hdr;
static const res_entry_t *s_dir;
static const u8          *s_base;

static int name_eq(const char *a, const char *b)
{
	int i;

	for (i = 0; i < 12; i++) {
		char ca = a[i];
		char cb = b[i];

		if (ca >= 'a' && ca <= 'z')
			ca = (char)(ca - 'a' + 'A');
		if (cb >= 'a' && cb <= 'z')
			cb = (char)(cb - 'a' + 'A');
		if (ca != cb)
			return 0;
		if (ca == 0)
			return 1;
	}
	return 1;
}

int res_init(void)
{
	s_base = gotres_start;
	s_hdr = (const res_hdr_t *)s_base;

	if (s_hdr->magic != 0x474F5433u)
		return -1;

	s_dir = (const res_entry_t *)(s_base + sizeof(res_hdr_t));
	return 0;
}

int res_count(void)
{
	return s_hdr ? (int)s_hdr->count : 0;
}

const res_entry_t *res_entry(int idx)
{
	if (!s_hdr || idx < 0 || idx >= (int)s_hdr->count)
		return 0;
	return &s_dir[idx];
}

const u8 *res_get(const char *name, u32 *size)
{
	u32 i;

	if (!s_hdr)
		return 0;

	for (i = 0; i < s_hdr->count; i++) {
		if (name_eq(s_dir[i].name, name)) {
			if (size)
				*size = s_dir[i].size;
			return s_base + s_dir[i].offset;
		}
	}
	if (size)
		*size = 0;
	return 0;
}

const u8 *res_getn(const char *prefix, int num, u32 *size)
{
	char buf[12];
	int i = 0;
	int d;

	while (prefix[i] && i < 8) {
		buf[i] = prefix[i];
		i++;
	}

	if (num == 0) {
		buf[i++] = '0';
	} else {
		char tmp[8];
		int n = 0;

		d = num;
		while (d > 0 && n < 7) {
			tmp[n++] = (char)('0' + (d % 10));
			d /= 10;
		}
		while (n > 0)
			buf[i++] = tmp[--n];
	}
	buf[i] = 0;

	return res_get(buf, size);
}
