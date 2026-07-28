/*
 * God of Thunder 32X - core game types.
 *
 * These mirror the original Turbo C structures from 1_DEFINE.H, with two
 * important changes:
 *
 *  1. `int` was 16-bit under DOS but is 32-bit on the SH2. Every field that
 *     is serialised to/from the on-disc data (LEVEL, ACTOR_NFO, SETUP,
 *     THOR_INFO) therefore uses explicit s16/u16 so the layout still matches
 *     the bytes stored in GOTRES.DAT.
 *
 *  2. The MASK_IMAGE / ALIGNED_MASK_IMAGE machinery, which existed only to
 *     pre-shift sprites into VGA Mode X plane alignments, is gone. On a
 *     chunky framebuffer a sprite is just a pointer to pixels.
 */
#ifndef GOT_H
#define GOT_H

#include "mars.h"

#define MAX_ACTORS   35
#define MAX_ENEMIES  16
#define MAX_SHOTS    16
#define STAMINA      20
#define NUM_OBJECTS  32
#define TMP_SIZE     5800

#define TILE_W       16
#define TILE_H       16
#define GRID_W       20
#define GRID_H       12

#define BOSS_LEVEL1  59
#define BOSS_LEVEL2  60
#define BOSS_LEVEL3  95
#define ENDING_SCREEN 106

#define APPLE_MAGIC     1
#define LIGHTNING_MAGIC 2
#define BOOTS_MAGIC     4
#define WIND_MAGIC      8
#define SHIELD_MAGIC    16
#define THUNDER_MAGIC   32

/* ---------------------------------------------------------------- */
/* On-disc structures - byte layout must match GOTRES.DAT exactly.   */

/*
 * 512 bytes: one screen of the world map.
 *
 * NOTE: the offsets in the comments of the original 1_DEFINE.H were stale
 * (they claimed static_obj lived at 302). The real layout - confirmed both
 * by 1_BACK.C, which copies 130 bytes from sd_data + level*512 + 322, and by
 * ScummVM's serialiser - is the one asserted below.
 */
typedef struct {
	u8  icon[GRID_H][GRID_W];   /* 0   background tile grid          */
	u8  bg_color;               /* 240 base tile index               */
	u8  type;                   /* 241 music/level type              */
	u8  actor_type[MAX_ENEMIES];/* 242 enemy actor numbers           */
	u8  actor_loc[MAX_ENEMIES]; /* 258 spawn grid positions          */
	u8  actor_value[MAX_ENEMIES];/* 274                              */
	u8  pal_colors[3];          /* 290                               */
	u8  actor_invis[MAX_ENEMIES];/* 293                              */
	u8  extra[13];              /* 309                               */
	u8  static_obj[30];         /* 322 pickups on this screen        */
	s16 static_x[30];           /* 352                               */
	s16 static_y[30];           /* 412                               */
	u8  new_level[10];          /* 472 exit links                    */
	u8  new_level_loc[10];      /* 482                               */
	u8  area;                   /* 492                               */
	u8  actor_dir[MAX_ENEMIES]; /* 493                               */
	u8  future[3];              /* 509                               */
} LEVEL;

/* The on-disc layout is load-bearing: assert it at compile time. */
#define GOT_ASSERT(name, cond) \
	typedef char got_assert_##name[(cond) ? 1 : -1]
GOT_ASSERT(level_size,     sizeof(LEVEL) == 512);
GOT_ASSERT(level_bgcolor,  __builtin_offsetof(LEVEL, bg_color)      == 240);
GOT_ASSERT(level_atype,    __builtin_offsetof(LEVEL, actor_type)    == 242);
GOT_ASSERT(level_sobj,     __builtin_offsetof(LEVEL, static_obj)    == 322);
GOT_ASSERT(level_sx,       __builtin_offsetof(LEVEL, static_x)      == 352);
GOT_ASSERT(level_sy,       __builtin_offsetof(LEVEL, static_y)      == 412);
GOT_ASSERT(level_newlvl,   __builtin_offsetof(LEVEL, new_level)     == 472);
GOT_ASSERT(level_area,     __builtin_offsetof(LEVEL, area)          == 492);
GOT_ASSERT(level_adir,     __builtin_offsetof(LEVEL, actor_dir)     == 493);

/* 40 bytes: the static half of an actor, stored at +5120 in ACTORn. */
typedef struct {
	u8 move;
	u8 width;
	u8 height;
	u8 directions;
	u8 frames;
	u8 frame_speed;
	u8 frame_sequence[4];
	u8 speed;
	u8 size_x;
	u8 size_y;
	u8 strength;
	u8 health;
	u8 num_moves;
	u8 shot_type;
	u8 shot_pattern;
	u8 shots_allowed;
	u8 solid;
	u8 flying;
	u8 rating;
	u8 type;
	char name[9];
	u8 func_num;
	u8 func_pass;
	u8 future1[6];
} ACTOR_NFO;

GOT_ASSERT(actor_nfo_size,    sizeof(ACTOR_NFO) == 40);
/* func_num drives the special-interaction dispatch (blocks, globes, signs,
   villagers, boulders); solid/type gate damage. Pin them. */
GOT_ASSERT(actor_nfo_sizex,   __builtin_offsetof(ACTOR_NFO, size_x)   == 11);
GOT_ASSERT(actor_nfo_sizey,   __builtin_offsetof(ACTOR_NFO, size_y)   == 12);
GOT_ASSERT(actor_nfo_solid,   __builtin_offsetof(ACTOR_NFO, solid)    == 19);
GOT_ASSERT(actor_nfo_type,    __builtin_offsetof(ACTOR_NFO, type)     == 22);
GOT_ASSERT(actor_nfo_name,    __builtin_offsetof(ACTOR_NFO, name)     == 23);
GOT_ASSERT(actor_nfo_funcnum, __builtin_offsetof(ACTOR_NFO, func_num) == 32);

/* Runtime actor. Not serialised, so plain ints are fine here. */
typedef struct {
	/* --- copied verbatim from ACTOR_NFO --- */
	u8 move;
	u8 width;
	u8 height;
	u8 directions;
	u8 frames;
	u8 frame_speed;
	u8 frame_sequence[4];
	u8 speed;
	u8 size_x;
	u8 size_y;
	u8 strength;
	s16 health;
	u8 num_moves;
	u8 shot_type;
	u8 shot_pattern;
	u8 shots_allowed;
	u8 solid;
	u8 flying;
	u8 rating;
	u8 type;
	char name[9];
	u8 func_num;
	u8 func_pass;
	s16 magic_hurts;

	/* --- dynamic --- */
	const u8 *pic[4][4];    /* 16x16 chunky frames, straight from ROM */
	u8  frame_count;
	u8  dir;
	u8  last_dir;
	s16 x;
	s16 y;
	s16 center;
	s16 last_x[2];
	s16 last_y[2];
	u8  used;
	u8  next;
	s16 speed_count;
	s16 vunerable;
	s16 shot_cnt;
	u8  num_shots;
	u8  creator;
	u8  pause;
	u8  actor_num;
	u8  move_count;
	u8  dead;
	u8  toggle;
	u8  center_x;
	u8  center_y;
	u8  show;
	s16 temp1;
	s16 temp2;
	s16 counter;
	s16 move_counter;
	s16 edge_counter;
	s16 temp3, temp4, temp5;
	u8  hit_thor;
	s16 rand;
	u8  init_dir;
	u8  pass_value;
	u8  shot_actor;
	u8  magic_hit;
	s16 temp6;
	s16 i1, i2, i3, i4, i5, i6;
	s16 init_health;
	s16 talk_counter;
	u8  etype;
} ACTOR;

typedef struct {
	u8  magic;
	u8  keys;
	s16 jewels;
	u8  last_area;
	u8  last_screen;
	u8  last_icon;
	u8  last_dir;
	s16 inventory;
	u8  item;
	s16 last_health;
	u8  last_magic;
	s16 last_jewels;
	u8  last_keys;
	u8  last_item;
	s16 last_inventory;
	u8  level;
	s32 score;
	s32 last_score;
	u8  object;
	const char *object_name;
	u8  last_object;
	const char *last_object_name;
	u8  armor;
} THOR_INFO;

typedef struct {
	u8 flags[8];        /* the original 64 script bit-flags */
	u8 value[16];
	u8 game;
	u8 area;
	u8 pc_sound;
	u8 dig_sound;
	u8 music;
	u8 speed;
	u8 scroll_flag;
	u8 boss_dead[3];
	u8 skill;
	u8 game_over;
} SETUP;

/* ---------------------------------------------------------------- */
/* Input - the DOS game polled scancodes; we map the Genesis pad.    */

/*
 * Direction encoding. This is the original engine's, taken from
 * movement_zero()/movement_two() in 1_MOVPAT.C, and it also indexes the
 * first dimension of an actor's pic[dir][frame] table - so getting it wrong
 * makes the sprite face one way while walking the other.
 */
/* Thor walks 2px per frame in the original. */
#define THOR_STEP  2

/*
 * Sound effect indices into DIGSOUND, from the (commented-out) enum in
 * 1_SOUND.C. Audio is stubbed for now, but the call sites are wired up so
 * the PWM backend has somewhere to land.
 */
#define SND_OW        0
#define SND_GULP      1
#define SND_SWISH     2
#define SND_YAH       3
#define SND_ELECTRIC  4
#define SND_THUNDER   5
#define SND_DOOR      6
#define SND_FALL      7
#define SND_ANGEL     8
#define SND_WOOP      9
#define SND_BRAAPP   11
#define SND_WIND     12
#define SND_PUNCH1   13
#define SND_CLANG    14
#define SND_EXPLODE  15

#define SND_THOR_HURT SND_OW
#define SND_HIT       SND_CLANG

#define DIR_UP     0
#define DIR_DOWN   1
#define DIR_LEFT   2
#define DIR_RIGHT  3

#define KEY_UP     0x0001
#define KEY_DOWN   0x0002
#define KEY_LEFT   0x0004
#define KEY_RIGHT  0x0008
#define KEY_FIRE   0x0010   /* B - swing hammer          */
#define KEY_MAGIC  0x0020   /* C - use selected magic    */
#define KEY_SELECT 0x0040   /* A - cycle inventory       */
#define KEY_START  0x0080   /* Start - options menu      */

typedef struct {
	u16 held;
	u16 pressed;    /* edge-triggered this frame */
	u16 last;
} input_t;

/* ---------------------------------------------------------------- */

/* Globals shared across the engine (defined in game.c). */
extern LEVEL      scrn;
extern ACTOR      actor[MAX_ACTORS];
extern ACTOR     *thor;
extern ACTOR     *hammer;
extern THOR_INFO  thor_info;
extern SETUP      setup;
extern input_t    g_input;

extern int  current_level;
extern int  new_level_flag;
extern u8   area;
extern u8   level_type;
extern int  game_over;
extern int  boss_active;
extern int  boss_dead;
extern int  exit_flag;
extern int  shield_on;
extern int  hourglass_flag;
extern int  bomb_flag;
extern int  thunder_flag;
extern int  lightning_used;
extern int  tornado_used;
extern int  warp_flag;
extern int  slow_mode;

/* game.c */
void game_init(void);
void game_run_frame(void);
int  game_ready(void);
u32  game_frame(void);

/* level.c */
void level_load(int num);
void level_build_screen(int page);
void level_show(int new_level);
int  level_bgtile(int x, int y);
void level_place_tile(int x, int y, int tile);
/* script.c */
void script_init(void);
int  script_execute(long index, ACTOR *speaker);
int  actor_speaks(ACTOR *act);
extern u8 script_flags[64];

/* move.c */
void move_actor(ACTOR *a);
void next_frame(ACTOR *a);
void set_thor_vars(void);
int  check_move2(int x, int y, ACTOR *act);
int  reverse_direction(ACTOR *a);
void thor_damaged(ACTOR *src);
void actor_damaged(ACTOR *act, int damage);
extern int thor_x1, thor_y1, thor_x2, thor_y2, thor_real_y1;

/* object.c */
void objects_build_map(void);
void objects_draw(int page);
void objects_check_pickup(void);
void pick_up_object(int p);
extern u8 object_map[GRID_W * GRID_H];
extern u8 object_index[GRID_W * GRID_H];

/* sptile.c */
int  special_tile_thor(int gy, int gx, int icon);
int  special_tile_actor(ACTOR *act, int gy, int gx, int icon);
void sptile_reset_informs(void);
extern int warp_pending, warp_new_level, warp_new_tile, warp_scroll, end_tile;

/* Tile classification thresholds (1_MOVPAT.C). */
#define TILE_SOLID    80
#define TILE_FLY     140
#define TILE_SPECIAL 200

extern int switch_flag;
void level_invalidate_bg(void);
void level_set_area(int a);
const u8 *level_tile(int idx);
const u8 *level_object_tile(int idx);

/* actors.c */
void actors_init(void);
void actor_setup(ACTOR *a, int num, int dir, int x, int y);
void actors_show_enemies(void);
void actors_draw(int page);
void actors_move(void);
int  actor_load(int num);
const u8 *actor_frame(int actor_num, int dir, int frame);
const ACTOR_NFO *actor_info(int num);

/* input.c */
void input_poll(void);

/* panel.c */
void panel_init(void);
void panel_draw(void);
void panel_health(void);
void panel_magic(void);
void panel_jewels(void);
void panel_keys(void);
void panel_score(void);

/* sound.c - stubs for now, PWM later. */
void snd_init(void);
void snd_play(int index);
void music_play(int num, int override);
void music_pause(void);
void music_resume(void);
void snd_update(void);

/* util */
int  got_rnd(int max);
void got_srand(u32 seed);
void *got_memcpy(void *d, const void *s, u32 n);
void *got_memset(void *d, int c, u32 n);
int   got_strlen(const char *s);
void  got_itoa(int v, char *buf);

#endif /* GOT_H */
