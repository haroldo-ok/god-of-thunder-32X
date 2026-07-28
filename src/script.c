/*
 * God of Thunder 32X - dialogue and script execution.
 *
 * Port of the useful subset of 1_SCRIPT.C / 1_DIALOG.C.
 *
 * Scripts live as plain text inside the SPEAKn resources, keyed by a line
 * of the form "|<index>" where index = current_level * 1000 + actor_num,
 * and terminated by "|STOP". This is what makes signs readable, villagers
 * talk and crystal balls fire their effects.
 *
 * The original interpreter is a small BASIC dialect (variables, IF/ELSE,
 * FOR/NEXT, GOSUB/RETURN, ASK menus...). Implemented here are the commands
 * that actually produce visible behaviour on screen:
 *
 *   SAY / "..."  - show a dialogue box with the following quoted lines
 *   PAUSE n      - wait
 *   SOUND n      - play a sound effect
 *   ADDJEWELS n  - adjust inventory / stats
 *   ADDHEALTH n
 *   ADDMAGIC n
 *   ADDKEYS n
 *   ADDSCORE n
 *   SETFLAG n    - set a persistent script flag
 *   PLACETILE x,y,t - mutate the tile grid (this is how crystal balls
 *                     open walls and bridges)
 *   VISIBLE n    - show/hide an actor
 *   END          - stop
 *
 * Control flow (IF/GOTO/GOSUB/FOR) is parsed and skipped rather than
 * executed; the commands above run in sequence. That is enough for signs,
 * greetings and the "activate something" crystal balls, and it degrades
 * gracefully on the shop scripts rather than misbehaving.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

#define MAX_SAY_LINES 12
#define MAX_LINE      64

/* Persistent script flags (SETFLAG). */
u8 script_flags[64];

static const u8 *s_speak;
static u32       s_speak_len;

void script_init(void)
{
	s_speak = res_getn("SPEAK", area ? area : 1, &s_speak_len);
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */

static int str_eq_n(const char *a, const char *b, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		char ca = a[i], cb = b[i];

		if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
		if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
		if (ca != cb)
			return 0;
	}
	return 1;
}

static int parse_int(const char *s, int *out)
{
	int v = 0, neg = 0, got = 0;

	while (*s == ' ')
		s++;
	if (*s == '-') { neg = 1; s++; }
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		s++;
		got = 1;
	}
	*out = neg ? -v : v;
	return got;
}

/* Copy one CR/LF-terminated line out of the resource. Returns bytes used. */
static u32 get_line(const u8 *p, u32 remain, char *dst, int max)
{
	u32 n = 0;
	int o = 0;

	while (n < remain && p[n] != '\r' && p[n] != '\n') {
		if (o < max - 1)
			dst[o++] = (char)p[n];
		n++;
	}
	dst[o] = 0;
	while (n < remain && (p[n] == '\r' || p[n] == '\n'))
		n++;
	return n;
}

/* ------------------------------------------------------------------ */
/* Dialogue box                                                        */

/*
 * display_speech() in 1_BACK.C draws a framed panel using background tiles
 * 192..199 as the border, with the speaker's face at the left. The panel
 * occupies x=32..288, y=48..160.
 */
void dialog_show(const char lines[][MAX_LINE], int count, const u8 *face)
{
	int l;
	const u8 *t;

	gfx_fill_rect(PAGE_GAME, 48, 64, 225, 81, 215);

	/* Corners and edges. */
	t = level_tile(192); if (t) gfx_tile(PAGE_GAME, 32, 48, t);
	t = level_tile(193); if (t) gfx_tile(PAGE_GAME, 272, 48, t);
	t = level_tile(194); if (t) gfx_tile(PAGE_GAME, 32, 144, t);
	t = level_tile(195); if (t) gfx_tile(PAGE_GAME, 272, 144, t);
	for (l = 0; l < 14; l++) {
		t = level_tile(196); if (t) gfx_tile(PAGE_GAME, 48 + (l << 4), 48, t);
		t = level_tile(197); if (t) gfx_tile(PAGE_GAME, 48 + (l << 4), 144, t);
	}
	for (l = 0; l < 6; l++) {
		t = level_tile(198); if (t) gfx_tile(PAGE_GAME, 32, 64 + (l << 4), t);
		t = level_tile(199); if (t) gfx_tile(PAGE_GAME, 272, 64 + (l << 4), t);
	}

	/* Speaker portrait: PIC entries carry a big-endian w/h header. */
	if (face) {
		const int fw = (face[0] << 8) | face[1];
		const int fh = (face[2] << 8) | face[3];

		if (fw > 0 && fw <= 64 && fh > 0 && fh <= 64)
			gfx_blit_masked(PAGE_GAME, 56, 72, face + 4, fw, fh);
	}

	for (l = 0; l < count && l < MAX_SAY_LINES; l++)
		gfx_print(PAGE_GAME, face ? 104 : 64, 72 + l * 9, lines[l], 14);

	gfx_present();
}

/*
 * Hold the dialogue on screen until the player acknowledges it, or a
 * timeout expires. Keeps the game responsive in automated tests, which
 * never press a button.
 */
/*
 * Hold the composed dialogue frame on screen.
 *
 * gfx_present() copies the game page into whichever framebuffer is about to
 * be shown, and the 32X double-buffers, so the page must be re-presented
 * every frame or the box flickers away after one flip. The caller has
 * already drawn the box into PAGE_GAME; we simply keep pushing it.
 */
static void dialog_wait(void)
{
	int frames = 0;

	/* Ignore the press that opened the box. */
	while ((g_input.held & (KEY_FIRE | KEY_START | KEY_MAGIC)) &&
	       frames < 90) {
		input_poll();
		gfx_present();
		frames++;
	}

	/* Then wait for an acknowledgement, with a timeout so the game can
	   never wedge (and so the automated tests stay deterministic). */
	frames = 0;
	while (frames < 420) {
		input_poll();
		if (g_input.pressed & (KEY_FIRE | KEY_START | KEY_MAGIC))
			break;
		gfx_present();
		frames++;
	}
}

/* Look up the face picture for a speaking actor: its name starts "NN:". */
static const u8 *actor_face(ACTOR *act)
{
	u32 size;
	int v = 0;
	const char *n = act->name;

	if (n[0] >= '0' && n[0] <= '9') {
		v = n[0] - '0';
		if (n[1] >= '0' && n[1] <= '9')
			v = v * 10 + (n[1] - '0');
	}
	if (v < 1 || v > 20)
		return 0;
	return res_getn("FACE", v, &size);
}

/* ------------------------------------------------------------------ */
/* Interpreter                                                         */

static void apply_add(const char *arg, int what)
{
	int v = 0;

	if (!parse_int(arg, &v))
		return;

	switch (what) {
	case 0:
		thor_info.jewels += (s16)v;
		if (thor_info.jewels < 0) thor_info.jewels = 0;
		if (thor_info.jewels > 999) thor_info.jewels = 999;
		panel_jewels();
		break;
	case 1:
		thor->health += (s16)v;
		if (thor->health < 0) thor->health = 0;
		if (thor->health > 150) thor->health = 150;
		panel_health();
		break;
	case 2: {
		int m = (int)thor_info.magic + v;
		if (m < 0) m = 0;
		if (m > 150) m = 150;
		thor_info.magic = (u8)m;
		panel_magic();
		break;
	}
	case 3: {
		int k = (int)thor_info.keys + v;
		if (k < 0) k = 0;
		if (k > 255) k = 255;
		thor_info.keys = (u8)k;
		panel_keys();
		break;
	}
	default:
		thor_info.score += v;
		panel_score();
		break;
	}
}

/*
 * Run the script identified by `index`. Returns 1 if a script was found.
 */
int script_execute(long index, ACTOR *speaker)
{
	char line[MAX_LINE];
	char want[16] = { 0 };
	char say[MAX_SAY_LINES][MAX_LINE];
	const u8 *p;
	u32 remain;
	int nsay = 0;
	int found = 0;
	int guard = 0;
	const u8 *face = speaker ? actor_face(speaker) : 0;

	if (!s_speak)
		script_init();
	if (!s_speak)
		return 0;

	/* Build the "|<index>" key. */
	{
		char digits[12];
		int n = 0;
		int o = 1;
		long v = index;

		if (v <= 0) {
			digits[n++] = '0';
		} else {
			while (v > 0 && n < 11) {
				digits[n++] = (char)('0' + (int)(v % 10));
				v /= 10;
			}
		}
		want[0] = '|';
		while (n > 0)
			want[o++] = digits[--n];
		want[o] = 0;
	}

	p = s_speak;
	remain = s_speak_len;

	/* Find the header line. */
	while (remain > 0) {
		u32 used = get_line(p, remain, line, MAX_LINE);

		p += used;
		remain -= used;
		if (line[0] == '|') {
			int i = 0;
			while (want[i] && line[i] == want[i])
				i++;
			if (!want[i] && (line[i] == 0 || line[i] == ' ')) {
				found = 1;
				break;
			}
		}
	}
	if (!found)
		return 0;

	/* Execute until |STOP, END, or a sanity limit. */
	while (remain > 0 && guard++ < 400) {
		u32 used = get_line(p, remain, line, MAX_LINE);
		char *c = line;

		p += used;
		remain -= used;

		if (line[0] == '|')
			break;                       /* |STOP or next script */

		while (*c == ' ' || *c == '\t')
			c++;
		if (!*c)
			continue;

		/* Quoted text accumulates into the current SAY block. */
		if (*c == '"') {
			int o = 0;
			c++;
			while (*c && o < MAX_LINE - 1) {
				if (*c == '~' && c[1]) {     /* colour escape */
					c += 2;
					continue;
				}
				say[nsay][o++] = *c++;
			}
			say[nsay][o] = 0;
			if (nsay < MAX_SAY_LINES - 1)
				nsay++;
			continue;
		}

		/* A non-quoted line ends any pending SAY block. */
		if (nsay > 0 && !str_eq_n(c, "SAY", 3)) {
			dialog_show(say, nsay, face);
			dialog_wait();
			nsay = 0;
			/* Redraw the world behind the box. */
			level_build_screen(PAGE_GAME);
			objects_draw(PAGE_GAME);
			actors_draw(PAGE_GAME);
		}

		if (str_eq_n(c, "SAY", 3))            continue;
		if (str_eq_n(c, "END", 3))            break;
		if (str_eq_n(c, "PAUSE", 5)) {
			int v = 0;
			parse_int(c + 5, &v);
			while (v-- > 0) {
				input_poll();
				gfx_present();
			}
			continue;
		}
		if (str_eq_n(c, "SOUND", 5)) {
			int v = 0;
			parse_int(c + 5, &v);
			snd_play(v);
			continue;
		}
		if (str_eq_n(c, "ADDJEWELS", 9)) { apply_add(c + 9, 0); continue; }
		if (str_eq_n(c, "ADDHEALTH", 9)) { apply_add(c + 9, 1); continue; }
		if (str_eq_n(c, "ADDMAGIC", 8))  { apply_add(c + 8, 2); continue; }
		if (str_eq_n(c, "ADDKEYS", 7))   { apply_add(c + 7, 3); continue; }
		if (str_eq_n(c, "ADDSCORE", 8))  { apply_add(c + 8, 4); continue; }
		if (str_eq_n(c, "SETFLAG", 7)) {
			int v = 0;
			if (parse_int(c + 7, &v) && v >= 0 && v < 64)
				script_flags[v] = 1;
			continue;
		}
		if (str_eq_n(c, "PLACETILE", 9)) {
			/* PLACETILE x,y,tile - how crystal balls open the world. */
			int xv = 0, yv = 0, tv = 0;
			char *q = c + 9;

			parse_int(q, &xv);
			while (*q && *q != ',') q++;
			if (*q == ',') { q++; parse_int(q, &yv); }
			while (*q && *q != ',') q++;
			if (*q == ',') { q++; parse_int(q, &tv); }
			level_place_tile(xv, yv, tv);
			continue;
		}
		if (str_eq_n(c, "VISIBLE", 7)) {
			int v = 0;
			if (parse_int(c + 7, &v) && v >= 0 && v < MAX_ACTORS)
				actor[v].used = actor[v].used ? 0 : 1;
			continue;
		}
		/* Everything else (IF/GOTO/GOSUB/FOR/variables) is skipped. */
	}

	if (nsay > 0) {
		dialog_show(say, nsay, face);
		dialog_wait();
	}

	/*
	 * Tell the main loop to skip one redraw so the caller's
	 * level_build_screen()/actors_draw() pass does not immediately
	 * overwrite the final frame of the conversation.
	 */
	level_build_screen(PAGE_GAME);
	objects_draw(PAGE_GAME);
	actors_draw(PAGE_GAME);
	gfx_present();
	return 1;
}

/*
 * actor_speaks(): entry point used by the special-actor handlers. Scripts
 * are keyed by level*1000 + actor_num.
 */
int actor_speaks(ACTOR *act)
{
	long index = (long)current_level * 1000L + (long)act->actor_num;

	return script_execute(index, act);
}
