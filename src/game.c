/*
 * God of Thunder 32X - game state and main loop.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

THOR_INFO thor_info;
SETUP     setup;

int  new_level_flag;
u8   area = 1;
int  game_over;
int  boss_active;
int  boss_dead;
int  exit_flag;
int  shield_on;
int  hourglass_flag;
int  bomb_flag;
int  thunder_flag;
int  lightning_used;
int  tornado_used;
int  warp_flag;
int  slow_mode;
int  switch_flag;

static u32 s_frame;
static int s_ready;


/* Thor's starting position, from 1_INIT.C: screen 23, grid cell (8, 6). */
#define START_SCREEN 23
#define START_ICON   ((6 * GRID_W) + 8)

void game_init(void)
{
	const u8 *pal;
	const u8 *txt;
	u32 size;

	gfx_init();

	if (res_init() != 0) {
		/* No archive: leave a visible red screen rather than a black
		   one so the failure is obvious on hardware and in the tests. */
		mars_set_color(0, 63, 0, 0);
		gfx_clear_page(PAGE_GAME, 0);
		gfx_present();
		return;
	}

	/* Font first, so any later message can be drawn. */
	txt = res_get("TEXT", &size);
	if (txt)
		gfx_font_init(txt, size);

	area = 1;
	setup.area = 1;
	setup.skill = 1;
	setup.music = 1;
	setup.dig_sound = 1;

	got_memset(&thor_info, 0, sizeof(thor_info));
	thor_info.magic       = 100;
	thor_info.jewels      = 0;
	thor_info.keys        = 0;
	thor_info.score       = 0;
	thor_info.last_health = 150;
	thor_info.last_screen = START_SCREEN;
	thor_info.last_icon   = START_ICON;
	thor_info.armor       = 0;
	thor_info.item        = 0;
	thor_info.inventory   = 0;

	level_set_area(area);

	pal = res_get("PALETTE", &size);
	if (pal && size >= 768)
		gfx_set_palette(pal);

	actors_init();
	thor->health = 150;
	thor->x = (s16)((thor_info.last_icon % GRID_W) * 16);
	thor->y = (s16)(((thor_info.last_icon / GRID_W) * 16) - 1);
	if (thor->x < 1) thor->x = 1;
	if (thor->y < 0) thor->y = 0;
	thor->dir = thor_info.last_dir;
	thor->last_dir = thor_info.last_dir;
	thor->speed_count = 6;
	thor->used = 1;

	current_level = -1;
	sptile_reset_informs();
	level_show(START_SCREEN);

	panel_init();
	script_init();
	snd_init();
	music_play(level_type, 1);

	gfx_present();
	s_ready = 1;
}

/*
 * Thor's movement. The full DOS engine ran check_thor_move() against the
 * tile table plus the actor list; this is the tile-collision half, which is
 * what makes the game playable and testable end to end.
 */

/*
 * check_move0() tests four probe points around Thor's feet rather than his
 * centre, which is what lets him walk with his head overlapping scenery:
 *
 *   x1 = x + 1        y1 = y + 8      (top-left of the foot box)
 *   x2 = x + 12       y2 = y + 15     (bottom-right)
 */

/*
 * Can a non-player actor stand at (x,y)? Used when shoving a block.
 * Mirrors check_special_move1()'s tile test.
 */
static int actor_tile_blocked(ACTOR *act, int x, int y)
{
	const int solid = act->flying ? TILE_SOLID : TILE_FLY;
	int x1, x2, y1, y2;
	int i1, i2, i3, i4;

	if (x < 0 || x > 304 || y < 0 || y > 176)
		return 1;

	x1 = x >> 4;
	y1 = y >> 4;
	x2 = (x + 15) >> 4;
	y2 = (y + 15) >> 4;

	i1 = level_bgtile(x1, y1);
	i2 = level_bgtile(x1, y2);
	i3 = level_bgtile(x2, y1);
	i4 = level_bgtile(x2, y2);

	if (i1 < solid || i2 < solid || i3 < solid || i4 < solid)
		return 1;

	if (i1 > TILE_SPECIAL && !special_tile_actor(act, y1, x1, i1)) return 1;
	if (i2 > TILE_SPECIAL && !special_tile_actor(act, y2, x1, i2)) return 1;
	if (i3 > TILE_SPECIAL && !special_tile_actor(act, y1, x2, i3)) return 1;
	if (i4 > TILE_SPECIAL && !special_tile_actor(act, y2, x2, i4)) return 1;

	return 0;
}

/* Would `act` at (x,y) land on top of another actor? */
static int actor_overlaps_any(ACTOR *act, int x, int y)
{
	const int x2 = x + 15;
	const int y2 = y + 15;
	int i;

	for (i = 3; i < MAX_ACTORS; i++) {
		ACTOR *o = &actor[i];
		int ox, oy, ox2, oy2;

		if (o == act || !o->used || o->type == 3)
			continue;
		ox = o->x;
		if (ox - x > 16 || x - ox > 16)
			continue;
		oy = o->y;
		if (oy - y > 16 || y - oy > 16)
			continue;
		ox2 = ox + o->size_x;
		oy2 = oy + o->size_y;
		if (x > ox2 || x2 < ox || y > oy2 || y2 < oy)
			continue;
		return 1;
	}
	return 0;
}

/*
 * Tile pass of check_move0().
 *
 * Probes the four corners of Thor's foot box. A corner on a tile below
 * TILE_FLY is solid scenery and blocks outright; a corner above
 * TILE_SPECIAL runs the special-tile handler, which may open a door, charge
 * jewels, drop Thor down a hole, or refuse the move.
 *
 * The original re-reads the remaining corners after each special handler,
 * because opening a door mutates the grid underneath us.
 */
static int tile_blocked(int x, int y)
{
	int x1, x2, y1, y2;
	int i1, i2, i3, i4;

	if (x < 0 || y < 0 || x > 306 || y > 175)
		return 1;

	x1 = (x + 1) >> 4;
	y1 = (y + 8) >> 4;
	x2 = (thor->dir > 1) ? ((x + 12) >> 4) : ((x + 10) >> 4);
	y2 = (y + 15) >> 4;

	i1 = level_bgtile(x1, y1);
	i2 = level_bgtile(x1, y2);
	i3 = level_bgtile(x2, y1);
	i4 = level_bgtile(x2, y2);

	if (i1 < TILE_FLY || i2 < TILE_FLY || i3 < TILE_FLY || i4 < TILE_FLY)
		return 1;

	if (i1 > TILE_SPECIAL) {
		if (!special_tile_thor(y1, x1, i1))
			return 1;
		i2 = level_bgtile(x1, y2);
		i3 = level_bgtile(x2, y1);
		i4 = level_bgtile(x2, y2);
	}
	if (i2 > TILE_SPECIAL) {
		if (!special_tile_thor(y2, x1, i2))
			return 1;
		i3 = level_bgtile(x2, y1);
		i4 = level_bgtile(x2, y2);
	}
	if (i3 > TILE_SPECIAL) {
		if (!special_tile_thor(y1, x2, i3))
			return 1;
		i4 = level_bgtile(x2, y2);
	}
	if (i4 > TILE_SPECIAL) {
		if (!special_tile_thor(y2, x2, i4))
			return 1;
	}

	return 0;
}

/* Flying actors (the hammer, shots) only stop at TILE_SOLID scenery. */
static int tile_blocked_flying(int x, int y)
{
	int x1, x2, y1, y2;

	if (x < 0 || y < 0 || x > 304 || y > 176)
		return 1;

	x1 = x >> 4;
	y1 = y >> 4;
	x2 = (x + 15) >> 4;
	y2 = (y + 15) >> 4;

	if (level_bgtile(x1, y1) < TILE_SOLID ||
	    level_bgtile(x1, y2) < TILE_SOLID ||
	    level_bgtile(x2, y1) < TILE_SOLID ||
	    level_bgtile(x2, y2) < TILE_SOLID)
		return 1;

	return 0;
}

/* Does a 16x16 box at (x,y) overlap a damageable enemy? */
static int actor_hit_at(int x, int y, ACTOR **hit)
{
	const int x2 = x + 13;
	const int y2 = y + 13;
	int i;

	*hit = 0;

	for (i = 3; i < MAX_ACTORS; i++) {
		ACTOR *act = &actor[i];
		int ax, ay, ax2, ay2;

		if (!act->used || act->type == 3)
			continue;

		ax = act->x;
		if (ax - x > 16 || x - ax > 16)
			continue;
		ay = act->y;
		if (ay - y > 16 || y - ay > 16)
			continue;

		ax2 = ax + act->size_x - 1;
		ay2 = ay + act->size_y - 1;

		if (x > ax2 || x2 < ax || y > ay2 || y2 < ay)
			continue;

		*hit = act;
		return 1;
	}
	return 0;
}

/* actor_damaged() from 1_MOVE.C, minus the scoring hooks. */
void actor_damaged(ACTOR *act, int damage)
{
	if (act->vunerable > 0 || act->type == 3)
		return;
	if ((act->solid & 0x7F) == 2)
		return;

	if (setup.skill == 0)
		damage <<= 1;
	else if (setup.skill == 2)
		damage >>= 1;
	if (damage < 1)
		damage = 1;

	act->vunerable = STAMINA;

	if (damage >= act->health) {
		act->used = 0;
		act->dead = 1;
		thor_info.score += act->init_health * 10;
		panel_score();
		snd_play(SND_EXPLODE);
	} else {
		act->show = 10;
		act->health -= (s16)damage;
		act->speed_count += 8;
		snd_play(SND_HIT);
	}
}

/*
 * Screen-edge transitions. check_move0() in the original treats walking off
 * an edge as a *successful* move that also swaps the screen, using a world
 * laid out as a 10-wide grid of 120 screens, and re-entering on the opposite
 * side at the coordinates below.
 */
static int thor_edge_transition(int nx, int ny)
{
	if (nx < 0) {
		if (current_level > 0) {
			level_show(current_level - 1);
			thor->x = 304;
			return 1;
		}
		return 0;
	}
	if (nx > 306) {
		if (current_level < 119) {
			level_show(current_level + 1);
			thor->x = 0;
			return 1;
		}
		return 0;
	}
	if (ny < 0) {
		if (current_level > 9) {
			level_show(current_level - 10);
			thor->y = 175;
			return 1;
		}
		return 0;
	}
	if (ny > 175) {
		if (current_level < 110) {
			level_show(current_level + 10);
			thor->y = 0;
			return 1;
		}
		return 0;
	}
	return 0;
}

/*
 * Actor collision. check_move0() walks actors 3..MAX_ACTORS and rejects the
 * move if Thor's foot box overlaps a solid actor; actors whose func_num is
 * set are "special" (pickups, NPCs) and trigger an effect instead of
 * blocking.
 *
 * The cheap |dx|>16 / |dy|>16 rejects come straight from the original and
 * keep this to a couple of compares for the vast majority of actors.
 */
/*
 * Special-actor handlers, from special_movement_func[] in 1_MOVPAT.C.
 *
 * An actor whose func_num is non-zero is not a plain enemy: it is a
 * pushable block, a talking peasant, a crystal ball, a switch, a boulder,
 * and so on. Bumping into one dispatches here instead of dealing damage.
 * Returning 1 lets Thor through, 0 blocks him.
 *
 * This is the piece that was missing: without it every one of these actors
 * was silently walked through.
 */

/* 1 - pushable block: shove it one step in Thor's facing. */
static int spec_block(ACTOR *act)
{
	int nx = act->x;
	int ny = act->y;
	const int sd = act->last_dir;

	act->last_dir = thor->dir;

	switch (thor->dir) {
	case DIR_UP:    ny -= 2; break;
	case DIR_DOWN:  ny += 2; break;
	case DIR_LEFT:  nx -= 2; break;
	default:        nx += 2; break;
	}

	/* The block may not be pushed into scenery or another actor. */
	if (actor_tile_blocked(act, nx, ny) || actor_overlaps_any(act, nx, ny)) {
		act->last_dir = (u8)sd;
		return 0;
	}

	act->x = (s16)nx;
	act->y = (s16)ny;
	return 0;   /* Thor still does not share the square this frame. */
}

/*
 * 3 - yellow globe / crystal ball.
 *
 * Runs the script keyed by level*1000 + actor_num. Depending on the script
 * that either shows text or mutates the world (PLACETILE/SETFLAG), which is
 * why some balls talk and others open walls.
 */
static int spec_globe(ACTOR *act)
{
	if (thunder_flag)
		return 0;
	actor_speaks(act);
	return 0;
}

/* 4, 7 - peg switches. */
static int spec_switch1(ACTOR *act)
{
	if (act->shot_cnt != 0)
		return 0;
	act->shot_cnt = 30;
	switch_flag = 1;
	return 0;
}

static int spec_switch2(ACTOR *act)
{
	if (act->shot_cnt != 0)
		return 0;
	act->shot_cnt = 30;
	switch_flag = 2;
	return 0;
}

/* 6 - simply hurts Thor on contact. */
static int spec_hurt(ACTOR *act)
{
	thor_damaged(act);
	return 0;
}

/* 8, 9 - boulders: start rolling when pushed along their axis. */
static int spec_roll_h(ACTOR *act)
{
	if (thor->dir < 2)
		return 0;
	act->last_dir = thor->dir;
	act->move = 14;
	return 0;
}

static int spec_roll_v(ACTOR *act)
{
	if (thor->dir > 1)
		return 0;
	act->last_dir = thor->dir;
	act->move = 14;
	return 0;
}

/* 2, 5, 11 - angle blocks, boulder-roll variants and timed actors. */
static int spec_solid(ACTOR *act)
{
	(void)act;
	return 0;
}

/*
 * 10 - villagers, merchants, hags and signs. Talking to one runs its
 * script, which is what makes NPCs speak. The temp6 cooldown stops the
 * dialogue re-firing every frame while Thor leans on them.
 */
static int spec_talk(ACTOR *act)
{
	if (act->temp6) {
		act->temp6--;
		return 0;
	}
	act->temp6 = 60;
	actor_speaks(act);
	return 0;
}

typedef int (*spec_fn)(ACTOR *);

static const spec_fn s_special_func[12] = {
	0,              /*  0 - not special      */
	spec_block,     /*  1 - pushable block   */
	spec_solid,     /*  2 - angle block      */
	spec_globe,     /*  3 - crystal ball     */
	spec_switch1,   /*  4 - peg switch       */
	spec_solid,     /*  5 - boulder roll     */
	spec_hurt,      /*  6 - contact damage   */
	spec_switch2,   /*  7 - peg switch 2     */
	spec_roll_h,    /*  8 - roll horizontal  */
	spec_roll_v,    /*  9 - roll vertical    */
	spec_talk,      /* 10 - villager / sign  */
	spec_solid,     /* 11 - (ScummVM extra)  */
};

/*
 * Actor pass of check_move0().
 *
 * Returns 1 if the move is blocked. Special actors dispatch through
 * s_special_func[]; plain enemies damage Thor and take a hit back, which is
 * how the original lets you kill things by walking into them.
 */
static int actor_blocked(int x, int y)
{
	const int x1 = x + 1;
	const int y1 = y + 8;
	const int x2 = x + 12;
	const int y2 = y + 15;
	int i;

	for (i = 3; i < MAX_ACTORS; i++) {
		ACTOR *act = &actor[i];
		int x3, y3, x4, y4;

		if (!act->used)
			continue;
		if (act->solid & 128)       /* explicitly non-colliding */
			continue;

		x3 = act->x + 1;
		if (x3 - x1 > 16 || x1 - x3 > 16)
			continue;
		y3 = act->y + 1;
		if (y3 - y1 > 16 || y1 - y3 > 16)
			continue;

		x4 = act->x + act->size_x - 1;
		y4 = act->y + act->size_y - 1;

		if (x1 > x4 || x2 < x3 || y1 > y4 || y2 < y3)
			continue;

		if (act->func_num > 0) {
			if (act->func_num == 255)   /* explosion */
				return 1;
			act->temp1 = (s16)x;
			act->temp2 = (s16)y;
			if (act->func_num < 12 && s_special_func[act->func_num])
				return !s_special_func[act->func_num](act);
			return 1;
		}

		/* Plain enemy: trade damage. */
		thor_damaged(act);
		if (act->solid < 2) {
			if (!act->vunerable && !(act->type & 1))
				snd_play(SND_PUNCH1);
			actor_damaged(act, thor->strength);
		}
		return 1;
	}
	return 0;
}

/* Thor took a hit: apply damage scaled by skill and start the blink. */
void thor_damaged(ACTOR *src)
{
	int damage;

	if (thor->vunerable > 0)
		return;

	damage = src->strength;
	if (damage != 255) {
		if (setup.skill == 0)
			damage >>= 1;
		else if (setup.skill == 2)
			damage <<= 1;
	}

	thor->health -= (s16)damage;
	if (thor->health <= 0) {
		thor->health = 0;
		game_over = 1;
	}
	thor->vunerable = STAMINA;
	thor->show = 10;
	panel_health();
	snd_play(SND_THOR_HURT);
}

/*
 * Throw the hammer. Mirrors thor_shoots() in 1_MOVE.C: only one hammer may
 * be in flight, and it inherits Thor's facing.
 */
static void thor_shoots(void)
{
	if (hammer->used || hammer->dead || thor->shot_cnt > 0)
		return;

	thor->shot_cnt = 20;
	hammer->used = 1;
	hammer->dir = thor->dir;
	hammer->last_dir = thor->dir;
	hammer->x = thor->x;
	hammer->y = (s16)(thor->y + 2);
	hammer->move = 2;          /* 2 = outbound flight */
	hammer->next = 0;
	hammer->move_counter = 0;
	snd_play(SND_SWISH);
}

/*
 * Hammer flight, from movement_two()/movement_five(). Pattern 2 flies out
 * in a straight line until it hits something; pattern 5 homes back to Thor
 * and is caught.
 */
static void hammer_update(void)
{
	int x1, y1;
	ACTOR *hitactor;

	if (!hammer->used)
		return;

	x1 = hammer->x;
	y1 = hammer->y;

	if (hammer->move == 2) {
		switch (hammer->last_dir) {
		case DIR_UP:    y1 -= 4; break;
		case DIR_DOWN:  y1 += 4; break;
		case DIR_LEFT:  x1 -= 4; break;
		default:        x1 += 4; break;
		}

		/*
		 * Damage the first enemy in the way, using the hammer's own
		 * strength (10/13/17 depending on Thor's armour) rather than
		 * a nominal value - check_move1() does actor_damaged(act,
		 * actr->strength).
		 */
		if (actor_hit_at(x1, y1, &hitactor) && hitactor) {
			actor_damaged(hitactor, hammer->strength);
			hammer->move = 5;
		} else if (x1 < 0 || x1 > 304 || y1 < 0 || y1 > 176 ||
		           tile_blocked_flying(x1, y1)) {
			hammer->move = 5;   /* bounce back */
		} else {
			hammer->x = (s16)x1;
			hammer->y = (s16)y1;
		}

		if (++hammer->frame_count >= 2) {
			hammer->frame_count = 0;
			hammer->next = (u8)((hammer->next + 1) & 3);
		}
		return;
	}

	/* move == 5: return to Thor. */
	{
		const int tx = thor->x;
		const int ty = thor->y + 2;
		int dx = 0, dy = 0;

		if (x1 > tx + 1)      dx = -4;
		else if (x1 < tx - 1) dx = 4;
		if (y1 > ty + 1)      dy = -4;
		else if (y1 < ty - 1) dy = 4;

		hammer->x = (s16)(x1 + dx);
		hammer->y = (s16)(y1 + dy);

		if (++hammer->frame_count >= 2) {
			hammer->frame_count = 0;
			hammer->next = (u8)((hammer->next + 1) & 3);
		}

		/* Caught. */
		if (dx == 0 && dy == 0) {
			hammer->used = 0;
			hammer->move = 0;
		}
	}
}

static void thor_move(void)
{
	const u16 held = g_input.held;
	const int x = thor->x;
	const int y = thor->y;
	int nx = x;
	int ny = y;
	int dir = thor->dir;
	int moved = 0;
	/*
	 * Direction priority follows movement_zero(): diagonals are checked
	 * first, and on a diagonal the actor faces LEFT/RIGHT.
	 */
	if (held & KEY_LEFT) {
		nx = x - THOR_STEP;
		dir = DIR_LEFT;
		moved = 1;
	} else if (held & KEY_RIGHT) {
		nx = x + THOR_STEP;
		dir = DIR_RIGHT;
		moved = 1;
	}

	if (held & KEY_UP) {
		ny = y - THOR_STEP;
		if (!moved)
			dir = DIR_UP;
		moved = 1;
	} else if (held & KEY_DOWN) {
		ny = y + THOR_STEP;
		if (!moved)
			dir = DIR_DOWN;
		moved = 1;
	}

	thor->dir = (u8)dir;
	thor->last_dir = (u8)dir;

	if (!moved) {
		thor->next = 0;
		return;
	}

	/* Leaving the screen takes priority over tile collision. */
	if (nx < 0 || nx > 306 || ny < 0 || ny > 175) {
		if (thor_edge_transition(nx, ny)) {
			thor->center_x = (u8)((thor->x + 8) >> 4);
			thor->center_y = (u8)((thor->y + 8) >> 4);
			return;
		}
	}

	/* Axes resolved independently so Thor slides along walls. */
	if (nx != x && !tile_blocked(nx, y) && !actor_blocked(nx, y))
		thor->x = (s16)nx;

	/* A hole/cave may have fired during the tile pass. */
	if (warp_pending)
		return;

	if (ny != y && !tile_blocked(thor->x, ny) && !actor_blocked(thor->x, ny))
		thor->y = (s16)ny;

	if (warp_pending)
		return;

	thor->center_x = (u8)((thor->x + 8) >> 4);
	thor->center_y = (u8)((thor->y + 8) >> 4);

	/* Walk cycle - next_frame() in 1_MOVE.C. */
	if (--thor->frame_count <= 0) {
		thor->frame_count = thor->frame_speed;
		thor->next = (u8)((thor->next + 1) & 3);
	}
}

void game_run_frame(void)
{
	input_poll();

	if (!s_ready) {
		s_frame++;
		return;
	}

	/* Fire button: throw the hammer (edge-triggered, like the original's
	   shot_cnt gate). */
	if (g_input.held & KEY_FIRE)
		thor_shoots();

	if (thor->shot_cnt > 0)
		thor->shot_cnt--;
	if (thor->vunerable > 0) {
		thor->vunerable--;
		thor->show++;
	}

	thor_move();

	/*
	 * A hole or cave mouth sets warp_pending during the tile pass; the
	 * screen change happens here, outside the collision code, exactly as
	 * the original defers it to the main loop.
	 */
	if (warp_pending) {
		warp_pending = 0;
		if (warp_new_level >= 0 && warp_new_level < 120) {
			level_show(warp_new_level);
			if (warp_scroll) {
				switch (thor->dir) {
				case DIR_UP:    thor->y = 175; break;
				case DIR_DOWN:  thor->y = 0;   break;
				case DIR_LEFT:  thor->x = 304; break;
				default:        thor->x = 0;   break;
				}
			} else {
				thor->x = (s16)((warp_new_tile % GRID_W) << 4);
				thor->y = (s16)(((warp_new_tile / GRID_W) << 4) - 2);
			}
			if (thor->x < 0) thor->x = 0;
			if (thor->y < 0) thor->y = 0;
		}
	}

	hammer_update();
	actors_move();
	objects_check_pickup();

	/* Redraw: background, objects, then everything alive on top. */
	level_build_screen(PAGE_GAME);
	objects_draw(PAGE_GAME);
	actors_draw(PAGE_GAME);

	snd_update();
	gfx_present();

	s_frame++;
	/*
	 * Heartbeat for the automated tests, plus a visible tick so the
	 * harness can measure the real logic frame rate: the pixel at (0,0)
	 * of the panel toggles every logic frame.
	 */
	mars_heartbeat((u16)s_frame);
	gfx_page(PAGE_PANEL)[0] = (u8)((s_frame & 1) ? 255 : 0);
}

int game_ready(void)
{
	return s_ready;
}

u32 game_frame(void)
{
	return s_frame;
}
