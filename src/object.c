/*
 * God of Thunder 32X - static objects (pickups).
 *
 * Port of 1_OBJECT.C. Each screen carries up to 30 static objects in
 * scrn.static_obj[], with grid coordinates in static_x/static_y. The engine
 * flattens them into a 20x12 lookup (object_map) so the main loop can test
 * "is there an object under Thor?" with a single array read.
 *
 * Two details that are easy to get wrong, and which were wrong here:
 *   - static_x/static_y are GRID cells, so they must be scaled by 16 when
 *     drawing (the original does xfput(static_x*16, static_y*16, ...));
 *   - the tile index is objects[static_obj - 1], i.e. one-based.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

u8 object_map[GRID_W * GRID_H];
u8 object_index[GRID_W * GRID_H];

void objects_build_map(void)
{
	int i;

	got_memset(object_map, 0, sizeof(object_map));
	got_memset(object_index, 0, sizeof(object_index));

	for (i = 0; i < 30; i++) {
		int gx, gy, p;

		if (!scrn.static_obj[i])
			continue;

		gx = scrn.static_x[i];
		gy = scrn.static_y[i];
		if (gx < 0 || gx >= GRID_W || gy < 0 || gy >= GRID_H)
			continue;

		p = gx + gy * GRID_W;
		object_index[p] = (u8)i;
		object_map[p] = scrn.static_obj[i];
	}
}

void objects_draw(int page)
{
	int i;

	for (i = 0; i < 30; i++) {
		const int o = scrn.static_obj[i];
		const u8 *t;

		if (o == 0)
			continue;

		/* objects[] is one-based in the level data. */
		t = level_object_tile(o - 1);
		if (t)
			gfx_tile_masked(page, scrn.static_x[i] << 4,
			                scrn.static_y[i] << 4, t);
	}
}

static void remove_object(int p)
{
	const int idx = object_index[p];

	object_map[p] = 0;
	if (idx >= 0 && idx < 30)
		scrn.static_obj[idx] = 0;
}

/*
 * pick_up_object(): p is a flattened grid index. Mirrors the original's
 * switch; the inventory/magic cases set the selected item and top up magic.
 */
void pick_up_object(int p)
{
	const int o = object_map[p];
	int consumed = 1;

	switch (o) {
	case 1:                     /* red jewel */
		if (thor_info.jewels >= 999)
			return;
		thor_info.jewels += 10;
		if (thor_info.jewels > 999)
			thor_info.jewels = 999;
		panel_jewels();
		break;
	case 2:                     /* blue jewel */
		if (thor_info.jewels >= 999)
			return;
		thor_info.jewels += 1;
		panel_jewels();
		break;
	case 3:                     /* red potion */
		if (thor_info.magic >= 150)
			return;
		thor_info.magic += 10;
		if (thor_info.magic > 150)
			thor_info.magic = 150;
		panel_magic();
		break;
	case 4:                     /* blue potion */
		if (thor_info.magic >= 150)
			return;
		thor_info.magic += 3;
		if (thor_info.magic > 150)
			thor_info.magic = 150;
		panel_magic();
		break;
	case 5:                     /* good apple */
		if (thor->health >= 150)
			return;
		snd_play(SND_GULP);
		thor->health += 5;
		if (thor->health > 150)
			thor->health = 150;
		panel_health();
		break;
	case 6:                     /* bad apple */
		snd_play(SND_OW);
		thor->health -= 10;
		if (thor->health < 0)
			thor->health = 0;
		panel_health();
		break;
	case 7:                     /* key */
		thor_info.keys++;
		panel_keys();
		break;
	case 8:                     /* treasure */
		if (thor_info.jewels >= 999)
			return;
		thor_info.jewels += 50;
		if (thor_info.jewels > 999)
			thor_info.jewels = 999;
		panel_jewels();
		break;
	case 9:                     /* trophy */
		thor_info.score += 100;
		panel_score();
		break;
	case 10:                    /* crown */
		thor_info.score += 1000;
		panel_score();
		break;

	case 12: case 13: case 14: case 15: case 16: case 17:
	case 18: case 19: case 20: case 21: case 22: case 23:
	case 24: case 25: case 26:  /* quest objects */
		thor->num_moves = 1;
		hammer->num_moves = 2;
		actor[2].used = 0;
		shield_on = 0;
		tornado_used = 0;
		thor_info.inventory |= 64;
		thor_info.item = 7;
		thor_info.object = (u8)(o - 11);
		break;

	case 27: case 28: case 29:
	case 30: case 31: case 32:  /* magic items */
		hourglass_flag = 0;
		thunder_flag = 0;
		shield_on = 0;
		lightning_used = 0;
		tornado_used = 0;
		hammer->num_moves = 2;
		thor->num_moves = 1;
		actor[2].used = 0;
		thor_info.inventory |= (s16)(1 << (o - 27));
		thor_info.item = (u8)(o - 26);
		thor_info.magic = 150;
		panel_magic();
		break;

	default:
		consumed = 0;
		break;
	}

	if (consumed) {
		remove_object(p);
		level_invalidate_bg();
	}
}

/* Called once per frame from the main loop. */
void objects_check_pickup(void)
{
	const int gx = (thor->x + 7) >> 4;
	const int gy = (thor->y + 8) >> 4;
	int p;

	if (gx < 0 || gx >= GRID_W || gy < 0 || gy >= GRID_H)
		return;

	p = gx + gy * GRID_W;
	if (object_map[p])
		pick_up_object(p);
}
