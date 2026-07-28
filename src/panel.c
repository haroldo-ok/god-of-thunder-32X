/*
 * God of Thunder 32X - status panel.
 *
 * The DOS build used the VGA split-screen (CRTC line-compare) so the bottom
 * 48 scanlines were a separate, non-scrolling page: 192 play area + 48 panel
 * = 240 lines total.
 *
 * NTSC 32X only gives us 224 lines. Rather than crop the play area (which
 * would hide part of the world and break the 12x20 tile grid the whole
 * engine is built around), the play area stays pixel-exact at 320x192 and
 * the 48-line panel artwork is compressed into the remaining 32 lines.
 *
 * The compression is not a blur: the original panel is drawn in 4-line
 * bands (see the row histogram of STATUS - every value repeats four times),
 * so dropping one row in four reproduces it faithfully at 3/4 height.
 * Bar and text coordinates are scaled by the same 3/4 factor.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

#define STAT_COLOR   206

/* Original panel coordinates, from 1_PANEL.C. */
#define BAR_X        59
#define BAR_END      209
#define SRC_HEALTH_Y 8
#define SRC_MAGIC_Y  20
#define SRC_TEXT_Y   32

/* 48 source lines -> 32 displayed lines. */
#define PY(y)        (((y) * PANEL_H) / STATUS_SRC_H)

static u8  s_panel_bg[SCREEN_W * PANEL_H];
static int s_have_bg;

void panel_init(void)
{
	u32 size;
	const u8 *p = res_get("STATUS", &size);
	int w = 0, h = 0;

	s_have_bg = 0;

	if (p && size > 4) {
		/* PIC entries carry a big-endian u16 width + u16 height. */
		w = (p[0] << 8) | p[1];
		h = (p[2] << 8) | p[3];
		p += 4;

		if (w == SCREEN_W && h >= STATUS_SRC_H &&
		    size >= 4 + (u32)w * STATUS_SRC_H) {
			int dy;

			/* Vertical 48->32 decimation, dropping every 4th row. */
			for (dy = 0; dy < PANEL_H; dy++) {
				int sy = (dy * STATUS_SRC_H) / PANEL_H;
				got_memcpy(&s_panel_bg[dy * SCREEN_W],
				           &p[sy * SCREEN_W], SCREEN_W);
			}
			s_have_bg = 1;
		}
	}

	panel_draw();
}

void panel_draw(void)
{
	if (s_have_bg)
		gfx_blit(PAGE_PANEL, 0, 0, s_panel_bg, SCREEN_W, PANEL_H);
	else
		gfx_clear_page(PAGE_PANEL, STAT_COLOR);

	panel_health();
	panel_magic();
	panel_jewels();
	panel_keys();
	panel_score();
}

/* xfillrectangle(59,8,b,12) / (b,8,209,12) */
void panel_health(void)
{
	int h = thor->health;
	int b;
	int y = PY(SRC_HEALTH_Y);
	int hh = PY(SRC_HEALTH_Y + 4) - y;

	if (h < 0) h = 0;
	if (h > 150) h = 150;
	b = BAR_X + h;
	if (b > BAR_END) b = BAR_END;

	gfx_fill_rect(PAGE_PANEL, BAR_X, y, b - BAR_X, hh, 32);
	gfx_fill_rect(PAGE_PANEL, b, y, BAR_END - b, hh, STAT_COLOR);
}

/* xfillrectangle(59,20,b,24) / (b,20,209,24) */
void panel_magic(void)
{
	int m = thor_info.magic;
	int b;
	int y = PY(SRC_MAGIC_Y);
	int hh = PY(SRC_MAGIC_Y + 4) - y;

	if (m < 0) m = 0;
	if (m > 150) m = 150;
	b = BAR_X + m;
	if (b > BAR_END) b = BAR_END;

	gfx_fill_rect(PAGE_PANEL, BAR_X, y, b - BAR_X, hh, 96);
	gfx_fill_rect(PAGE_PANEL, b, y, BAR_END - b, hh, STAT_COLOR);
}

/* Counters share the y=32 text row in the original. */
static void draw_counter(int x0, int x1, int value, int digits_x1)
{
	char s[16];
	int l, x;
	int y = PY(SRC_TEXT_Y);

	got_itoa(value, s);
	l = got_strlen(s);
	x = digits_x1 - (l * 8);
	if (x < x0) x = x0;

	gfx_fill_rect(PAGE_PANEL, x0, y, x1 - x0, 8, STAT_COLOR);
	gfx_print(PAGE_PANEL, x, y, s, 14);
}

void panel_jewels(void)
{
	draw_counter(59, 85, thor_info.jewels, 82);
}

void panel_keys(void)
{
	draw_counter(139, 165, thor_info.keys, 162);
}

void panel_score(void)
{
	draw_counter(223, 279, (int)thor_info.score, 276);
}
