/*
 * God of Thunder 32X - actor movement.
 *
 * Port of the parts of 1_MOVE.C / 1_MOVPAT.C that drive non-player actors:
 *
 *   move_actor()   - the per-actor scheduler: counts down speed_count and
 *                    calls the actor's movement pattern when it fires
 *   check_move2()  - "can this enemy stand here?": scenery, special tiles
 *                    and other actors, plus damaging Thor on contact
 *   movement_*()   - the individual patterns
 *
 * Episode 1 uses patterns 1, 3, 4, 7, 9, 10, 11, 15, 18, 20, 27, 29, 31,
 * 37, 38 and 39. Those are implemented here; anything else falls back to
 * "animate in place", which is what pattern 1 does anyway.
 */

#include "got.h"
#include "gfx.h"

/* Thor's collision box, recomputed once per frame (set_thor_vars). */
int thor_x1, thor_y1, thor_x2, thor_y2, thor_real_y1;

void set_thor_vars(void)
{
	thor_x1 = thor->x + 1;
	thor_y1 = thor->y + 8;
	thor_real_y1 = thor->y;
	thor_x2 = thor->x + 12;
	thor_y2 = thor->y + 15;
}

static int overlap(int x1, int y1, int x2, int y2,
                   int x3, int y3, int x4, int y4)
{
	if (x1 > x4 || x2 < x3 || y1 > y4 || y2 < y3)
		return 0;
	return 1;
}

/* next_frame() from 1_MOVE.C */
void next_frame(ACTOR *a)
{
	if (--a->frame_count <= 0) {
		a->next++;
		if (a->next > 3)
			a->next = 0;
		a->frame_count = a->frame_speed;
	}
}

int reverse_direction(ACTOR *a)
{
	switch (a->last_dir) {
	case DIR_UP:    return DIR_DOWN;
	case DIR_DOWN:  return DIR_UP;
	case DIR_LEFT:  return DIR_RIGHT;
	default:        return DIR_LEFT;
	}
}

/*
 * check_move2(): can `act` occupy (x,y)?
 *
 * Returns 1 and commits the position on success, 0 if blocked. Walking into
 * Thor damages him and blocks - that is how enemies hurt the player.
 */
int check_move2(int x, int y, ACTOR *act)
{
	const int solid = act->flying ? TILE_SOLID : TILE_FLY;
	int x1, x2, y1, y2;
	int i1, i2, i3, i4;
	int i;

	if (x < 0 || x > (319 - act->size_x) || y < 0 || y > 175)
		return 0;

	x1 = (x + 1) >> 4;
	y1 = act->func_num ? ((y + 1) >> 4) : ((y + (act->size_y >> 1)) >> 4);
	x2 = ((x + act->size_x) - 1) >> 4;
	y2 = ((y + act->size_y) - 1) >> 4;

	i1 = level_bgtile(x1, y1);
	i2 = level_bgtile(x1, y2);
	i3 = level_bgtile(x2, y1);
	i4 = level_bgtile(x2, y2);

	if (i1 < solid || i2 < solid || i3 < solid || i4 < solid)
		return 0;

	if (i1 > TILE_SPECIAL && !special_tile_actor(act, y1, x1, i1)) return 0;
	if (i2 > TILE_SPECIAL && !special_tile_actor(act, y2, x1, i2)) return 0;
	if (i3 > TILE_SPECIAL && !special_tile_actor(act, y1, x2, i3)) return 0;
	if (i4 > TILE_SPECIAL && !special_tile_actor(act, y2, x2, i4)) return 0;

	x1 = x + 1;
	y1 = y + 1;
	x2 = (x + act->size_x) - 1;
	y2 = (y + act->size_y) - 1;

	for (i = 0; i < MAX_ACTORS; i++) {
		ACTOR *o = &actor[i];
		int x3, y3, x4, y4;

		if (o == act || o->actor_num == 1 || !o->used || o->type == 3)
			continue;

		if (i == 0) {
			if (overlap(x1, y1, x2, y2,
			            thor_x1, thor_y1, thor_x2, thor_y2)) {
				thor_damaged(act);
				return 0;
			}
			continue;
		}

		x3 = o->x;
		if (x3 - x1 > 16 || x1 - x3 > 16)
			continue;
		y3 = o->y;
		if (y3 - y1 > 16 || y1 - y3 > 16)
			continue;
		x4 = o->x + o->size_x;
		y4 = o->y + o->size_y;

		if (overlap(x1, y1, x2, y2, x3, y3, x4, y4)) {
			/* Pattern 38 (blue hair) trips switches by touch. */
			if (act->move == 38) {
				if (o->func_num == 4)
					switch_flag = 1;
				else if (o->func_num == 7)
					switch_flag = 2;
			}
			return 0;
		}
	}

	act->x = (s16)x;
	act->y = (s16)y;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Movement patterns                                                   */

/* 1 - stand still, cycle frames. Villagers, signs and trees use this. */
static int mv_one(ACTOR *a)
{
	next_frame(a);
	return a->dir;
}

/* 15 - completely static: no movement, no animation. */
static int mv_fifteen(ACTOR *a)
{
	return a->dir;
}

/* 3 - walk until blocked, then pick a new random direction. */
static int mv_three(ACTOR *a)
{
	int d = a->last_dir;
	int x1 = a->x;
	int y1 = a->y;

	switch (d) {
	case DIR_UP:    y1 -= 2; break;
	case DIR_DOWN:  y1 += 2; break;
	case DIR_LEFT:  x1 -= 2; break;
	default:        x1 += 2; break;
	}
	if (!check_move2(x1, y1, a))
		d = got_rnd(4);

	next_frame(a);
	a->last_dir = (u8)d;
	if (a->directions == 1)
		return 0;
	return d;
}

/* 4 - simple tracking: close on Thor horizontally, then vertically. */
static int mv_four(ACTOR *a)
{
	int d = a->last_dir;
	int x1 = a->x;
	int y1 = a->y;
	int f = 0;

	if (x1 > thor_x1 - 1)      { x1 -= 2; d = DIR_LEFT;  f = 1; }
	else if (x1 < thor_x1 - 1) { x1 += 2; d = DIR_RIGHT; f = 1; }

	if (f)
		f = check_move2(x1, y1, a);

	if (!f) {
		y1 = a->y;
		if (y1 < thor_real_y1) {
			int step = thor_real_y1 - y1;
			if (step > 2) step = 2;
			y1 += step;
			d = DIR_DOWN;
			f = 1;
		} else if (y1 > thor_real_y1) {
			int step = y1 - thor_real_y1;
			if (step > 2) step = 2;
			y1 -= step;
			d = DIR_UP;
			f = 1;
		}
		if (f)
			f = check_move2(a->x, y1, a);
		if (!f)
			check_move2(a->x, a->y, a);
	}

	next_frame(a);
	a->last_dir = (u8)d;
	if (a->directions == 1)
		return 0;
	return d;
}

/* 7 - like 3 but pauses between runs. */
static int mv_seven(ACTOR *a)
{
	if (a->next == 0 && a->frame_count == a->frame_speed) {
		a->speed_count = 12;
		a->last_dir = (u8)got_rnd(4);
	}
	return mv_three(a);
}

/* 9 / 37 - travel in a straight line for a random distance, then turn. */
static int mv_random_run(ACTOR *a)
{
	int d = a->last_dir;
	int x1 = a->x;
	int y1 = a->y;
	int blocked = 0;

	if (a->counter) {
		a->counter--;
		switch (d) {
		case DIR_UP:    y1 -= 2; break;
		case DIR_DOWN:  y1 += 2; break;
		case DIR_LEFT:  x1 -= 2; break;
		default:        x1 += 2; break;
		}
		if (!check_move2(x1, y1, a))
			blocked = 1;
	} else {
		blocked = 1;
	}

	if (blocked) {
		a->counter = (s16)(got_rnd(60) + 10);
		a->last_dir = (u8)got_rnd(4);
		d = a->last_dir;
	}

	next_frame(a);
	if (a->directions == 1)
		return 0;
	return d;
}

/* 10 - vertical-only patrol. */
static int mv_ten(ACTOR *a)
{
	int d = a->last_dir;
	int y1 = a->y;
	int blocked = 0;

	if (a->counter) {
		if (a->pass_value != 1)
			a->counter--;
		if (d == DIR_UP || d == DIR_LEFT)
			y1 -= 2;
		else
			y1 += 2;
		if (!check_move2(a->x, y1, a))
			blocked = 1;
	} else {
		blocked = 1;
	}

	if (blocked) {
		a->counter = (s16)(got_rnd(50) + 10);
		a->last_dir = (u8)((d == DIR_UP) ? DIR_DOWN : DIR_UP);
		d = a->last_dir;
	}

	next_frame(a);
	if (a->directions == 1)
		return 0;
	return d;
}

/* 11 - bats: drift horizontally, bouncing diagonally. */
static int mv_eleven(ACTOR *a)
{
	int d = a->last_dir;

	switch (d) {
	case 0:
		if (check_move2(a->x - 2, a->y - 2, a)) break;
		d = 1;
		if (check_move2(a->x - 2, a->y + 2, a)) break;
		d = 2;
		break;
	case 1:
		if (check_move2(a->x - 2, a->y + 2, a)) break;
		d = 0;
		if (check_move2(a->x - 2, a->y - 2, a)) break;
		d = 3;
		break;
	case 2:
		if (check_move2(a->x + 2, a->y - 2, a)) break;
		d = 3;
		if (check_move2(a->x + 2, a->y + 2, a)) break;
		d = 0;
		break;
	default:
		if (check_move2(a->x + 2, a->y + 2, a)) break;
		d = 2;
		if (check_move2(a->x + 2, a->y - 2, a)) break;
		d = 1;
		break;
	}

	next_frame(a);
	a->last_dir = (u8)d;
	if (a->directions == 1)
		return 0;
	return d;
}

/* 18 - rats: dart about randomly with pauses. */
static int mv_eighteen(ACTOR *a)
{
	if (a->temp5) {
		a->temp5--;
		if (!a->temp5)
			a->num_moves = 1;
	}
	return mv_random_run(a);
}

/* 29 - patrol horizontally or vertically depending on pass_value. */
static int mv_twentynine(ACTOR *a)
{
	int d = a->last_dir;
	int x1 = a->x;
	int y1 = a->y;

	if (a->pass_value == 0) {
		if (d == DIR_LEFT)  x1 -= 2; else { x1 += 2; d = DIR_RIGHT; }
		if (!check_move2(x1, y1, a))
			d = (d == DIR_LEFT) ? DIR_RIGHT : DIR_LEFT;
	} else {
		if (d == DIR_UP) y1 -= 2; else { y1 += 2; d = DIR_DOWN; }
		if (!check_move2(x1, y1, a))
			d = (d == DIR_UP) ? DIR_DOWN : DIR_UP;
	}

	next_frame(a);
	a->last_dir = (u8)d;
	if (a->directions == 1)
		return 0;
	return d;
}

/* 39 / 40 - trolls; without the boss logic they simply track Thor. */
static int mv_track(ACTOR *a)
{
	return mv_four(a);
}

typedef int (*move_fn)(ACTOR *);

static const move_fn s_move_func[41] = {
	0,             /*  0 player - handled in game.c */
	mv_one,        /*  1 */
	mv_one,        /*  2 hammer - handled in game.c */
	mv_three,      /*  3 */
	mv_four,       /*  4 */
	mv_one,        /*  5 */
	mv_one,        /*  6 */
	mv_seven,      /*  7 */
	mv_one,        /*  8 */
	mv_random_run, /*  9 */
	mv_ten,        /* 10 */
	mv_eleven,     /* 11 */
	mv_three,      /* 12 */
	mv_four,       /* 13 */
	mv_one,        /* 14 boulder roll */
	mv_fifteen,    /* 15 */
	mv_one,        /* 16 */
	mv_one,        /* 17 */
	mv_eighteen,   /* 18 */
	mv_one,        /* 19 */
	mv_random_run, /* 20 */
	mv_one,        /* 21 */
	mv_one,        /* 22 */
	mv_one,        /* 23 */
	mv_one,        /* 24 */
	mv_one,        /* 25 */
	mv_one,        /* 26 */
	mv_random_run, /* 27 */
	mv_one,        /* 28 */
	mv_twentynine, /* 29 */
	mv_ten,        /* 30 */
	mv_fifteen,    /* 31 stalactite */
	mv_one,        /* 32 */
	mv_one,        /* 33 */
	mv_one,        /* 34 */
	mv_one,        /* 35 */
	mv_one,        /* 36 */
	mv_random_run, /* 37 */
	mv_three,      /* 38 */
	mv_track,      /* 39 */
	mv_track,      /* 40 */
};

/*
 * move_actor(): the scheduler from 1_MOVE.C.
 *
 * speed_count gates how often the pattern runs, so a "slow" actor simply
 * has a larger speed value. num_moves lets fast actors step more than once
 * per activation.
 */
void move_actor(ACTOR *a)
{
	int i;
	int moves;

	if (a->vunerable != 0) a->vunerable--;
	if (a->shot_cnt != 0)  a->shot_cnt--;
	if (a->show != 0)      a->show--;

	a->speed_count--;
	if (a->speed_count > 0)
		return;

	a->speed_count = a->move_counter ? (s16)(a->speed << 1) : a->speed;
	if (a->speed_count <= 0)
		a->speed_count = 1;

	moves = a->num_moves ? a->num_moves : 1;
	if (moves > 4)
		moves = 4;

	while (moves--) {
		const move_fn fn = (a->move < 41) ? s_move_func[a->move] : mv_one;

		if (!fn)
			break;
		i = fn(a);
		if (a->directions == 2)
			i &= 1;
		a->dir = (u8)i;
	}

	/* The original keeps actors on even X so the Mode X blits stay
	   aligned; harmless here but preserved for identical behaviour. */
	a->x &= ~1;
}
