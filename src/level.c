/*
 * God of Thunder 32X - level / screen handling.
 *
 * The world is a grid of 512-byte LEVEL records held in the SDATn resource
 * (120 screens per episode). The DOS game kept the whole 60 KB block in a
 * far heap allocation and memcpy'd the current screen into `scrn`.
 *
 * We keep the same model, but the master copy stays in ROM and only the
 * mutable working set (the screens the player has changed - opened doors,
 * collected items) lives in SDRAM.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

#define NUM_SCREENS 120

LEVEL scrn;

static const u8 *s_sdat;          /* ROM master copy for the current area */
static const u8 *s_bgpics;        /* 16x16 background tiles               */
static const u8 *s_objects;       /* 32 object tiles                      */

/* Screens the player has modified, kept in RAM. A 120-entry table of
   512-byte records would be 60 KB; instead we track a small LRU of dirty
   screens, which is plenty for normal play. */
#define DIRTY_MAX 24
static LEVEL s_dirty[DIRTY_MAX];
static s16   s_dirty_id[DIRTY_MAX];
static int   s_dirty_n;

int current_level;
u8  level_type;

/* Cached composite of the current screen's tile grid (see below). */
static u8  s_bgcache[GAME_W * GAME_H];
static int s_bgcache_valid;

void level_invalidate_bg(void)
{
	s_bgcache_valid = 0;
}

void level_set_area(int a)
{
	u32 size;

	s_sdat    = res_getn("SDAT",  a, &size);
	s_bgpics  = res_getn("BPICS", a, &size);
	if (!s_objects)
		s_objects = res_get("OBJECTS", &size);
	s_dirty_n = 0;
}

const u8 *level_tile(int idx)
{
	if (!s_bgpics || idx < 0 || idx > 255)
		return 0;
	return s_bgpics + idx * (16 * 16);
}

const u8 *level_object_tile(int idx)
{
	if (!s_objects || idx < 0 || idx >= NUM_OBJECTS)
		return 0;
	return s_objects + idx * (16 * 16);
}

static LEVEL *find_dirty(int num)
{
	int i;

	for (i = 0; i < s_dirty_n; i++)
		if (s_dirty_id[i] == num)
			return &s_dirty[i];
	return 0;
}

/* Copy the current screen back out so changes persist while we're away. */
static void level_stash(int num)
{
	LEVEL *d = find_dirty(num);

	if (!d) {
		if (s_dirty_n >= DIRTY_MAX)
			return;
		d = &s_dirty[s_dirty_n];
		s_dirty_id[s_dirty_n] = (s16)num;
		s_dirty_n++;
	}
	got_memcpy(d, &scrn, sizeof(LEVEL));
}

void level_load(int num)
{
	const LEVEL *src;
	LEVEL *d;

	if (num < 0 || num >= NUM_SCREENS)
		return;

	d = find_dirty(num);
	if (d) {
		got_memcpy(&scrn, d, sizeof(LEVEL));
	} else if (s_sdat) {
		src = (const LEVEL *)(s_sdat + num * 512);
		got_memcpy(&scrn, src, sizeof(LEVEL));
	} else {
		got_memset(&scrn, 0, sizeof(LEVEL));
	}

	current_level = num;
	level_type = scrn.type;
	s_bgcache_valid = 0;
}

int level_bgtile(int x, int y)
{
	if (x < 0 || y < 0 || x >= GRID_W || y >= GRID_H)
		return 0;
	return scrn.icon[y][x];
}

void level_place_tile(int x, int y, int tile)
{
	if (x < 0 || y < 0 || x >= GRID_W || y >= GRID_H)
		return;
	scrn.icon[y][x] = (u8)tile;
	s_bgcache_valid = 0;
	level_stash(current_level);
}

/*
 * Paint the whole 320x192 play area. Every cell draws the screen's base
 * tile first and then its own icon on top, matching build_screen() in
 * 1_BACK.C - that is how the game gets transparent decorations over grass.
 */
/*
 * Composite the 12x20 tile grid.
 *
 * This used to run every frame: 240 tiles, each drawn twice (base + icon),
 * plus a full 61 KB page clear - about 123 K byte-writes per frame before a
 * single sprite was touched, which is what pinned the game at single-digit
 * frames per second.
 *
 * The tile grid only changes on a screen transition or an explicit
 * place_tile(), so it is rendered once into a cache page and then restored
 * with a straight linear copy. The per-frame cost drops to one memcpy of
 * the play area.
 */
static void level_render_grid(u8 *dst)
{
	const u8 *base = level_tile(scrn.bg_color);
	int x, y;

	/* Rebuild straight into the cache page. */
	got_memset(dst, 0, GAME_W * GAME_H);

	for (y = 0; y < GRID_H; y++) {
		const u8 *row = scrn.icon[y];
		const int py = y << 4;

		for (x = 0; x < GRID_W; x++) {
			const int icon = row[x];
			const u8 *t;
			const int px = x << 4;

			if (icon == 0)
				continue;
			if (base)
				gfx_tile_to(dst, px, py, base);
			t = level_tile(icon);
			if (t)
				gfx_tile_masked_to(dst, px, py, t);
		}
	}
}

void level_build_screen(int page)
{
	u8 *dst = gfx_page(page);

	if (!s_bgcache_valid) {
		level_render_grid(s_bgcache);
		s_bgcache_valid = 1;
	}

	got_memcpy(dst, s_bgcache, GAME_W * GAME_H);
}

void level_show(int num)
{
	if (current_level >= 0)
		level_stash(current_level);

	level_load(num);
	level_build_screen(PAGE_GAME);
	objects_build_map();
	objects_draw(PAGE_GAME);
	actors_show_enemies();
	actors_draw(PAGE_GAME);
}
