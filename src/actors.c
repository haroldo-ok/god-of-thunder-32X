/*
 * God of Thunder 32X - actor loading and rendering.
 *
 * In the DOS original every actor frame had to be "made into a mask": each
 * 16x16 sprite was copied into VGA latch memory in four different plane
 * alignments, alongside a nibble-per-pixel mask for the Sequencer. That is
 * what make_mask()/xcreatmaskimage() in 1_IMAGE.C did, and it is why the
 * game had a hard cap on actor frames (it ran out of VGA memory).
 *
 * Here the frames are already linear 16x16 chunky blocks sitting in ROM, so
 * "loading" an actor is just pointing at them. There is no frame budget and
 * no per-screen upload.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

#define ACTOR_FRAME_BYTES  (16 * 16)
#define ACTOR_FRAMES       16
#define ACTOR_PIX_BYTES    (ACTOR_FRAME_BYTES * ACTOR_FRAMES)  /* 4096 */

/* Cache of resolved ACTORn resources. The archive stores, per actor,
   16 linear frames followed by the 40-byte info block. */
#define ACTOR_CACHE 128

typedef struct {
	s16       num;
	const u8 *pix;
	const ACTOR_NFO *nfo;
} actor_res_t;

static actor_res_t s_cache[ACTOR_CACHE];
static int         s_cache_n;

ACTOR  actor[MAX_ACTORS];
ACTOR *thor;
ACTOR *hammer;

static const actor_res_t *find_actor(int num)
{
	int i;
	u32 size;
	const u8 *p;

	for (i = 0; i < s_cache_n; i++) {
		if (s_cache[i].num == num)
			return &s_cache[i];
	}
	if (s_cache_n >= ACTOR_CACHE)
		return 0;

	p = res_getn("ACTOR", num, &size);
	if (!p || size < ACTOR_PIX_BYTES + 40)
		return 0;

	s_cache[s_cache_n].num = (s16)num;
	s_cache[s_cache_n].pix = p;
	s_cache[s_cache_n].nfo = (const ACTOR_NFO *)(p + ACTOR_PIX_BYTES);
	return &s_cache[s_cache_n++];
}

const ACTOR_NFO *actor_info(int num)
{
	const actor_res_t *r = find_actor(num);

	return r ? r->nfo : 0;
}

const u8 *actor_frame(int actor_num, int dir, int frame)
{
	const actor_res_t *r = find_actor(actor_num);
	int idx;

	if (!r)
		return 0;
	idx = (dir & 3) * 4 + (frame & 3);
	return r->pix + idx * ACTOR_FRAME_BYTES;
}

/* Populate the runtime actor from its static info block. */
static void apply_info(ACTOR *a, const ACTOR_NFO *n)
{
	int i;

	a->move          = n->move;
	a->width         = n->width;
	a->height        = n->height;
	a->directions    = n->directions;
	a->frames        = n->frames;
	a->frame_speed   = n->frame_speed;
	for (i = 0; i < 4; i++)
		a->frame_sequence[i] = n->frame_sequence[i];
	a->speed         = n->speed;
	a->size_x        = n->size_x;
	a->size_y        = n->size_y;
	a->strength      = n->strength;
	a->health        = n->health;
	a->num_moves     = n->num_moves;
	a->shot_type     = n->shot_type;
	a->shot_pattern  = n->shot_pattern;
	a->shots_allowed = n->shots_allowed;
	a->solid         = n->solid;
	a->flying        = n->flying;
	a->rating        = n->rating;
	a->type          = n->type;
	for (i = 0; i < 9; i++)
		a->name[i] = n->name[i];
	a->func_num      = n->func_num;
	a->func_pass     = n->func_pass;
}

/* Bind the 4x4 frame table for an actor to the ROM data. */
static void bind_frames(ACTOR *a, int num)
{
	int d, f;

	for (d = 0; d < 4; d++)
		for (f = 0; f < 4; f++)
			a->pic[d][f] = actor_frame(num, d, f);
}

int actor_load(int num)
{
	return find_actor(num) ? 1 : 0;
}

void actor_setup(ACTOR *a, int num, int dir, int x, int y)
{
	a->next = 0;
	a->frame_count = a->frame_speed;
	a->dir = (u8)dir;
	a->last_dir = (u8)dir;

	if (a->directions == 1)
		a->dir = 0;
	if (a->directions == 2)
		a->dir &= 1;
	if (a->directions == 4 && a->frames == 1) {
		a->dir = 0;
		a->next = (u8)dir;
	}

	a->x = (s16)x;
	a->y = (s16)y;
	a->width = 16;
	a->height = 16;
	a->center = 0;
	a->last_x[0] = (s16)x;
	a->last_x[1] = (s16)x;
	a->last_y[0] = (s16)y;
	a->last_y[1] = (s16)y;
	a->used = 1;
	a->speed_count = 8;
	a->vunerable = 0;
	a->shot_cnt = 20;
	a->num_shots = 0;
	a->creator = 0;
	a->pause = 0;
	a->show = 0;
	a->actor_num = (u8)num;
	a->counter = 0;
	a->move_counter = 0;
	a->edge_counter = 20;
	a->hit_thor = 0;
	a->rand = (s16)got_rnd(100);
	a->temp1 = 0;
	a->init_health = a->health;
	a->dead = 0;
	a->center_x = (u8)((x + 8) / 16);
	a->center_y = (u8)((y + 8) / 16);
}

/*
 * Load Thor, his hammer and the shared effect actors. Actor numbers follow
 * the original: 100..102 = Thor (by armour level), 103..105 = hammer,
 * 106 = sparkle, 107 = explosion.
 */
void actors_init(void)
{
	const actor_res_t *r;
	int i;

	got_memset(actor, 0, sizeof(actor));

	/* Thor */
	r = find_actor(100 + thor_info.armor);
	if (!r)
		r = find_actor(100);
	if (r) {
		apply_info(&actor[0], r->nfo);
		bind_frames(&actor[0], r->num);
		actor_setup(&actor[0], 0, 0, 100, 100);
	}
	thor = &actor[0];

	/* Hammer */
	r = find_actor(103 + thor_info.armor);
	if (!r)
		r = find_actor(103);
	if (r) {
		apply_info(&actor[1], r->nfo);
		bind_frames(&actor[1], r->num);
		actor_setup(&actor[1], 1, 0, 100, 100);
	}
	actor[1].used = 0;
	hammer = &actor[1];

	for (i = 2; i < MAX_ACTORS; i++)
		actor[i].used = 0;
}

/* Spawn this screen's enemies from the level data. */
void actors_show_enemies(void)
{
	int i;

	for (i = 3; i < MAX_ACTORS; i++)
		actor[i].used = 0;

	for (i = 0; i < MAX_ENEMIES; i++) {
		int type = scrn.actor_type[i];
		const actor_res_t *r;
		ACTOR *a;

		if (type == 0)
			continue;

		r = find_actor(type);
		if (!r)
			continue;

		a = &actor[i + 3];
		got_memset(a, 0, sizeof(*a));
		apply_info(a, r->nfo);
		bind_frames(a, type);
		actor_setup(a, i + 3,
		            scrn.actor_dir[i],
		            (scrn.actor_loc[i] % GRID_W) * 16,
		            (scrn.actor_loc[i] / GRID_W) * 16);
		a->init_dir = scrn.actor_dir[i];
		a->pass_value = scrn.actor_value[i];
		a->etype = (u8)type;
	}
}

/*
 * Draw every live actor into the given page.
 *
 * Iterating backwards keeps the original's draw order (Thor on top). The
 * frame pointer is resolved with two masked index ops rather than the
 * multiply the compiler would emit for pic[d][f] on a 4x4 array of
 * pointers, and the per-actor early-outs are ordered cheapest-first.
 */
void actors_draw(int page)
{
	int i;

	for (i = MAX_ACTORS - 1; i >= 0; i--) {
		ACTOR *a = &actor[i];
		const u8 *fr;

		if (!a->used)
			continue;

		/* Blink while briefly invulnerable after a hit. Thor is never
		   hidden this way, so the player always has a reference. */
		if (i != 0 && a->vunerable > 0 && (a->show & 1))
			continue;

		fr = a->pic[a->dir & 3][a->next & 3];
		if (!fr)
			fr = a->pic[0][0];
		if (fr)
			gfx_tile_masked(page, a->x, a->y, fr);
	}
}

/*
 * Per-frame update for the NPC actors.
 *
 * Actors 0 (Thor) and 1 (the hammer) are driven explicitly from game.c -
 * movement pattern 0 and 2/5 respectively - so they are skipped here.
 * Everything else runs its movement pattern through move_actor(), which
 * handles the speed gating, the pattern dispatch and collision.
 */
void actors_move(void)
{
	int i;

	set_thor_vars();

	for (i = 2; i < MAX_ACTORS; i++) {
		ACTOR *a = &actor[i];

		if (!a->used)
			continue;
		move_actor(a);
	}
}
