/*
 * God of Thunder 32X - special tiles.
 *
 * Port of 1_SPTILE.C. Any background tile with an index above TILE_SPECIAL
 * (200) is not just scenery: stepping onto it runs a handler that can open a
 * door, charge jewels, teleport Thor down a hole or into a cave, or simply
 * refuse the move.
 *
 * The handlers return 1 to allow the move and 0 to block it, exactly as in
 * the original, and they are called from tile_blocked() once per probe
 * corner.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

/* Set by a hole/cave tile to request a screen change after the move. */
int  warp_pending;
int  warp_new_level;
int  warp_new_tile;
int  warp_scroll;
int  end_tile;

/* "You need a key" / "you need N jewels" are only said once per session. */
static u8 s_door_inform;
static u8 s_cash1_inform;
static u8 s_cash2_inform;

static void erase_door(int gx, int gy)
{
	snd_play(SND_DOOR);
	scrn.icon[gy][gx] = scrn.bg_color;
	/* The cached background must be rebuilt now the grid changed. */
	level_invalidate_bg();
}

static int open_door1(int gy, int gx)
{
	if (thor_info.keys > 0) {
		erase_door(gx, gy);
		thor_info.keys--;
		panel_keys();
		return 1;
	}
	if (!s_door_inform)
		s_door_inform = 1;
	return 0;
}

static int cash_door1(int gy, int gx, int amount)
{
	if (thor_info.jewels >= amount) {
		erase_door(gx, gy);
		thor_info.jewels -= (s16)amount;
		panel_jewels();
		return 1;
	}
	if (amount == 10 && !s_cash1_inform)
		s_cash1_inform = 1;
	if (amount == 100 && !s_cash2_inform)
		s_cash2_inform = 1;
	return 0;
}

/*
 * special_tile_thor(): gy/gx are grid coordinates.
 *
 * Tile meanings (from 1_SPTILE.C):
 *   201        locked door        - costs a key
 *   202        ending bridge
 *   203, 204   always solid
 *   205..208   one-way tiles: passable only travelling in one direction
 *   209        money gate, 10 jewels
 *   210        money gate, 100 jewels
 *   211        end-of-level trigger
 *   212, 213   always solid
 *   214..217   teleport tiles (solid to Thor)
 *   218..229   holes and cave mouths - move to another screen
 */
int special_tile_thor(int gy, int gx, int icon)
{
	int f = 0;

	switch (icon) {
	case 201:
		return open_door1(gy, gx);
	case 202:
		if (thor->x > 300)
			end_tile = 1;
		return 1;
	case 203:
	case 204:
		return 0;

	/* One-way tiles. dir: 0=up 1=down 2=left 3=right. */
	case 205:
		if (thor->dir != DIR_DOWN)  return 1;
		break;
	case 206:
		if (thor->dir != DIR_UP)    return 1;
		break;
	case 207:
		if (thor->dir != DIR_RIGHT) return 1;
		break;
	case 208:
		if (thor->dir != DIR_LEFT)  return 1;
		break;

	case 209:
		return cash_door1(gy, gx, 10);
	case 210:
		return cash_door1(gy, gx, 100);

	case 211:
		level_place_tile(gx, gy, 79);
		exit_flag = 2;
		return 1;

	case 212:
	case 213:
	case 214:
	case 215:
	case 216:
	case 217:
		return 0;

	/* 218/219 index the second half of the new_level table. */
	case 218:
	case 219:
		f = 1;
		/* fall through */
	case 220: case 221: case 222: case 223:
	case 224: case 225: case 226: case 227:
	case 228: case 229: {
		/*
		 * Only trigger when Thor's centre is actually on the tile -
		 * otherwise brushing a corner would teleport him.
		 */
		const int cx = (thor->x + 1 + 7) >> 4;
		const int cy = (thor->y + 8) >> 4;
		int idx;

		if (cx < 0 || cx >= GRID_W || cy < 0 || cy >= GRID_H)
			return 1;
		if (scrn.icon[cy][cx] != icon)
			return 1;

		thor->vunerable = STAMINA;
		if (icon < 224 && icon > 219)
			snd_play(SND_FALL);

		idx = icon - 220 + (f * 6);
		if (idx < 0 || idx > 9)
			return 1;

		warp_new_level = scrn.new_level[idx];
		warp_scroll = 0;
		if (warp_new_level > 119) {
			warp_scroll = 1;
			warp_new_level -= 128;
		}
		warp_new_tile = scrn.new_level_loc[idx];
		warp_pending = 1;
		return 0;
	}
	default:
		break;
	}
	return 0;
}

/*
 * special_tile(): the same tiles as seen by a non-player actor (enemies,
 * the hammer, shots). Most are simply passable; money gates and teleports
 * are not.
 */
int special_tile_actor(ACTOR *act, int gy, int gx, int icon)
{
	(void)gy;
	(void)gx;

	switch (icon) {
	case 201:
	case 202:
	case 203:
	case 204:
		break;
	case 205:
	case 206:
	case 207:
	case 208:
		return 1;
	case 209:
	case 210:
	case 214:
	case 215:
	case 216:
	case 217:
		return 0;
	case 224:
	case 225:
	case 226:
	case 227:
		return act->flying ? 1 : 0;
	default:
		return 1;
	}
	return 0;
}

void sptile_reset_informs(void)
{
	s_door_inform = 0;
	s_cash1_inform = 0;
	s_cash2_inform = 0;
}
