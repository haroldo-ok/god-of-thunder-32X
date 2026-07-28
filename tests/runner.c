/*
 * God of Thunder 32X - headless emulator test harness.
 *
 * Loads the PicoDrive libretro core, runs the ROM for a number of frames,
 * and dumps framebuffer statistics so the test suite can assert on what is
 * actually rendered. Supports scripted controller input so we can drive the
 * game and prove it is interactive, not just displaying something.
 *
 * Build:  cc -O2 -o runner runner.c -ldl
 * Usage:  ./runner <core.so> <rom.32x> [options]
 *           --frames N            run N frames (default 600)
 *           --dump-at N:file.ppm  write frame N to a PPM
 *           --stats-at N          print stats for frame N (repeatable)
 *           --press F:N:BUTTON    hold BUTTON from frame F for N frames
 *           --json                emit machine-readable summary
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal libretro ABI ---- */
#define RETRO_DEVICE_JOYPAD 1
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT 10
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27
#define RETRO_ENVIRONMENT_GET_CAN_DUPE 3

#define RETRO_DEVICE_ID_JOYPAD_B      0
#define RETRO_DEVICE_ID_JOYPAD_Y      1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START  3
#define RETRO_DEVICE_ID_JOYPAD_UP     4
#define RETRO_DEVICE_ID_JOYPAD_DOWN   5
#define RETRO_DEVICE_ID_JOYPAD_LEFT   6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT  7
#define RETRO_DEVICE_ID_JOYPAD_A      8
#define RETRO_DEVICE_ID_JOYPAD_X      9

struct retro_game_info {
	const char *path;
	const void *data;
	size_t size;
	const char *meta;
};

struct retro_game_geometry {
	unsigned base_width, base_height, max_width, max_height;
	float aspect_ratio;
};
struct retro_system_timing { double fps, sample_rate; };
struct retro_system_av_info {
	struct retro_game_geometry geometry;
	struct retro_system_timing timing;
};
struct retro_system_info {
	const char *library_name, *library_version, *valid_extensions;
	bool need_fullpath, block_extract;
};
struct retro_variable { const char *key, *value; };

typedef bool (*env_t)(unsigned, void *);
typedef void (*video_t)(const void *, unsigned, unsigned, size_t);
typedef void (*audio_t)(const int16_t *, size_t);
typedef void (*audio_sample_t)(int16_t, int16_t);
typedef void (*input_poll_t)(void);
typedef int16_t (*input_state_t)(unsigned, unsigned, unsigned, unsigned);

/* ---- captured frame ---- */
static unsigned char g_rgb[1024 * 512 * 3];
static unsigned g_w, g_h;
static int g_have_frame;
static unsigned g_pixfmt = 1; /* 0=0RGB1555 1=XRGB8888 2=RGB565 */

static char g_sysdir[] = ".";

static bool env_cb(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		g_pixfmt = *(const unsigned *)data;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = g_sysdir;
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;
		v->value = NULL;
		return false;
	}
	default:
		return false;
	}
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
	unsigned x, y;

	if (!data || w == 0 || h == 0)
		return;
	if (w > 1024) w = 1024;
	if (h > 512) h = 512;
	g_w = w; g_h = h;

	for (y = 0; y < h; y++) {
		const unsigned char *row = (const unsigned char *)data + y * pitch;
		for (x = 0; x < w; x++) {
			unsigned char r, g, b;
			if (g_pixfmt == 2) {
				uint16_t p = ((const uint16_t *)row)[x];
				r = (unsigned char)(((p >> 11) & 0x1F) << 3);
				g = (unsigned char)(((p >> 5) & 0x3F) << 2);
				b = (unsigned char)((p & 0x1F) << 3);
			} else if (g_pixfmt == 0) {
				uint16_t p = ((const uint16_t *)row)[x];
				r = (unsigned char)(((p >> 10) & 0x1F) << 3);
				g = (unsigned char)(((p >> 5) & 0x1F) << 3);
				b = (unsigned char)((p & 0x1F) << 3);
			} else {
				uint32_t p = ((const uint32_t *)row)[x];
				r = (unsigned char)((p >> 16) & 0xFF);
				g = (unsigned char)((p >> 8) & 0xFF);
				b = (unsigned char)(p & 0xFF);
			}
			g_rgb[(y * w + x) * 3 + 0] = r;
			g_rgb[(y * w + x) * 3 + 1] = g;
			g_rgb[(y * w + x) * 3 + 2] = b;
		}
	}
	g_have_frame = 1;
}

static void audio_cb(const int16_t *d, size_t f) { (void)d; (void)f; }

/* Peek 32X COMM registers out of the core's memory map (diagnostics). */
static unsigned char *g_sdram;
static size_t g_sdram_sz;
static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }

/* ---- scripted input ---- */
#define MAX_PRESS 32
static struct { int from, len, id; } g_press[MAX_PRESS];
static int g_press_n;
static int g_frame;

static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
	int i;
	(void)dev; (void)idx;
	if (port != 0)
		return 0;
	for (i = 0; i < g_press_n; i++) {
		if ((int)id == g_press[i].id &&
		    g_frame >= g_press[i].from &&
		    g_frame < g_press[i].from + g_press[i].len)
			return 1;
	}
	return 0;
}

static int button_id(const char *n)
{
	if (!strcasecmp(n, "up")) return RETRO_DEVICE_ID_JOYPAD_UP;
	if (!strcasecmp(n, "down")) return RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (!strcasecmp(n, "left")) return RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (!strcasecmp(n, "right")) return RETRO_DEVICE_ID_JOYPAD_RIGHT;
	if (!strcasecmp(n, "a")) return RETRO_DEVICE_ID_JOYPAD_A;
	if (!strcasecmp(n, "b")) return RETRO_DEVICE_ID_JOYPAD_B;
	if (!strcasecmp(n, "c")) return RETRO_DEVICE_ID_JOYPAD_X;
	if (!strcasecmp(n, "start")) return RETRO_DEVICE_ID_JOYPAD_START;
	return -1;
}

/* ---- frame analysis ---- */
struct stats {
	unsigned long nonblack;
	unsigned long total;
	int distinct;
	unsigned long sum;
	int maxr, maxg, maxb;
};

static void analyse(struct stats *st)
{
	unsigned char seen[4096];
	unsigned i, n = g_w * g_h;

	memset(seen, 0, sizeof(seen));
	memset(st, 0, sizeof(*st));
	st->total = n;

	for (i = 0; i < n; i++) {
		int r = g_rgb[i * 3 + 0];
		int g = g_rgb[i * 3 + 1];
		int b = g_rgb[i * 3 + 2];
		int key;

		if (r > 8 || g > 8 || b > 8)
			st->nonblack++;
		st->sum += (unsigned)(r + g + b);
		if (r > st->maxr) st->maxr = r;
		if (g > st->maxg) st->maxg = g;
		if (b > st->maxb) st->maxb = b;

		key = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
		if (!seen[key]) { seen[key] = 1; st->distinct++; }
	}
}

static void write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%u %u\n255\n", g_w, g_h);
	fwrite(g_rgb, 1, (size_t)g_w * g_h * 3, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	void *h;
	void (*rinit)(void);
	void (*rdeinit)(void);
	void (*rrun)(void);
	bool (*rload)(const struct retro_game_info *);
	void (*rsetenv)(env_t);
	void (*rsetvideo)(video_t);
	void (*rsetaudio)(audio_t);
	void (*rsetaudios)(audio_sample_t);
	void (*rsetpoll)(input_poll_t);
	void (*rsetstate)(input_state_t);
	void (*rsetctrl)(unsigned, unsigned);
	void (*rgetav)(struct retro_system_av_info *);
	void *(*rgetmem)(unsigned) = NULL;
	size_t (*rgetmemsz)(unsigned) = NULL;

	const char *core, *rom;
	int frames = 600, json = 0, i;
	int dump_at[16], dump_n = 0;
	const char *dump_file[16];
	int stats_at[32], stats_n = 0;
	struct retro_game_info gi;
	struct retro_system_av_info av;
	FILE *rf;
	long romsz;
	void *rombuf;
	struct stats last;
	int first_nonblack = -1;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <core.so> <rom> [opts]\n", argv[0]);
		return 2;
	}
	core = argv[1];
	rom = argv[2];

	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
			frames = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--json")) {
			json = 1;
		} else if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) {
			char *s = argv[++i];
			char *c = strchr(s, ':');
			if (c && dump_n < 16) {
				*c = 0;
				dump_at[dump_n] = atoi(s);
				dump_file[dump_n] = c + 1;
				dump_n++;
			}
		} else if (!strcmp(argv[i], "--stats-at") && i + 1 < argc) {
			if (stats_n < 32) stats_at[stats_n++] = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--press") && i + 1 < argc) {
			char *s = argv[++i];
			char *c1 = strchr(s, ':');
			char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
			if (c1 && c2 && g_press_n < MAX_PRESS) {
				*c1 = 0; *c2 = 0;
				g_press[g_press_n].from = atoi(s);
				g_press[g_press_n].len = atoi(c1 + 1);
				g_press[g_press_n].id = button_id(c2 + 1);
				if (g_press[g_press_n].id >= 0) g_press_n++;
			}
		}
	}

	h = dlopen(core, RTLD_NOW);
	if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define SYM(v, n) do { *(void **)(&v) = dlsym(h, n); \
	if (!v) { fprintf(stderr, "missing %s\n", n); return 2; } } while (0)
	SYM(rinit, "retro_init");
	SYM(rdeinit, "retro_deinit");
	SYM(rrun, "retro_run");
	SYM(rload, "retro_load_game");
	SYM(rsetenv, "retro_set_environment");
	SYM(rsetvideo, "retro_set_video_refresh");
	SYM(rsetaudio, "retro_set_audio_sample_batch");
	SYM(rsetaudios, "retro_set_audio_sample");
	SYM(rsetpoll, "retro_set_input_poll");
	SYM(rsetstate, "retro_set_input_state");
	SYM(rsetctrl, "retro_set_controller_port_device");
	SYM(rgetav, "retro_get_system_av_info");
	*(void **)(&rgetmem) = dlsym(h, "retro_get_memory_data");
	*(void **)(&rgetmemsz) = dlsym(h, "retro_get_memory_size");
#undef SYM

	rsetenv(env_cb);
	rsetvideo(video_cb);
	rsetaudio(audio_cb);
	rsetaudios(audio_sample_cb);
	rsetpoll(input_poll_cb);
	rsetstate(input_state_cb);

	rinit();

	rf = fopen(rom, "rb");
	if (!rf) { fprintf(stderr, "cannot open %s\n", rom); return 2; }
	fseek(rf, 0, SEEK_END);
	romsz = ftell(rf);
	fseek(rf, 0, SEEK_SET);
	rombuf = malloc((size_t)romsz);
	if (fread(rombuf, 1, (size_t)romsz, rf) != (size_t)romsz) {
		fprintf(stderr, "short read\n");
		return 2;
	}
	fclose(rf);

	memset(&gi, 0, sizeof(gi));
	gi.path = rom;
	gi.data = rombuf;
	gi.size = (size_t)romsz;

	if (!rload(&gi)) {
		fprintf(stderr, "retro_load_game failed\n");
		return 3;
	}
	rsetctrl(0, RETRO_DEVICE_JOYPAD);
	rgetav(&av);

	if (!json)
		printf("core loaded: %ux%u @ %.2f fps\n",
		       av.geometry.base_width, av.geometry.base_height,
		       av.timing.fps);

	memset(&last, 0, sizeof(last));

	for (g_frame = 0; g_frame < frames; g_frame++) {
		int d;

		rrun();

		if (g_have_frame) {
			analyse(&last);
			if (first_nonblack < 0 && last.nonblack > (last.total / 100))
				first_nonblack = g_frame;
		}

		for (d = 0; d < dump_n; d++)
			if (dump_at[d] == g_frame)
				write_ppm(dump_file[d]);

		for (d = 0; d < stats_n; d++) {
			if (stats_at[d] == g_frame) {
				printf("frame %d: %ux%u nonblack=%lu/%lu (%.1f%%) "
				       "distinct=%d avg=%.1f max=(%d,%d,%d)\n",
				       g_frame, g_w, g_h, last.nonblack, last.total,
				       last.total ? 100.0 * last.nonblack / last.total : 0.0,
				       last.distinct,
				       last.total ? (double)last.sum / (last.total * 3) : 0.0,
				       last.maxr, last.maxg, last.maxb);
			}
		}
	}

	if (getenv("DUMP_COMM") && rgetmem) {
		/* PicoDrive exposes 32X SDRAM; COMM regs are not in it, so
		   fall back to reporting via the final frame instead. */
	}
	if (json) {
		printf("{\"frames\":%d,\"width\":%u,\"height\":%u,"
		       "\"nonblack\":%lu,\"total\":%lu,\"nonblack_pct\":%.3f,"
		       "\"distinct\":%d,\"avg\":%.3f,"
		       "\"first_nonblack_frame\":%d,\"got_frame\":%d}\n",
		       frames, g_w, g_h, last.nonblack, last.total,
		       last.total ? 100.0 * last.nonblack / last.total : 0.0,
		       last.distinct,
		       last.total ? (double)last.sum / (last.total * 3) : 0.0,
		       first_nonblack, g_have_frame);
	} else {
		printf("final: %ux%u nonblack=%.1f%% distinct=%d first_nonblack_frame=%d\n",
		       g_w, g_h,
		       last.total ? 100.0 * last.nonblack / last.total : 0.0,
		       last.distinct, first_nonblack);
	}

	rdeinit();
	free(rombuf);
	return 0;
}
