/*
 * God of Thunder 32X - graphics primitives.
 *
 * The DOS original drove VGA Mode X: a 4-plane, 320x240 unchained mode where
 * every write had to go through the Sequencer's map-mask register, and where
 * sprites were pre-shifted into four separate alignments so that the planar
 * layout could be exploited. All of that complexity exists purely to work
 * around VGA hardware.
 *
 * The 32X gives us a linear 8-bit chunky framebuffer, so the entire Mode X
 * apparatus collapses into ordinary memory copies. Assets are de-planed at
 * build time by tools/mkassets.py, so blitting is a straight byte copy with
 * an optional transparency test.
 */

#include <stdint.h>
#include "gfx.h"
#include "res.h"

/*
 * Row addressing. SCREEN_W is 320 = 256 + 64, so y*320 is (y<<8) + (y<<6):
 * two shifts and an add instead of a multiply. The SH2 has no single-cycle
 * multiplier for this, and these expressions sit in the innermost loops of
 * every blit, so it is worth spelling out.
 */
#define ROW(y)  (((y) << 8) + ((y) << 6))

/* Off-screen pages live in SDRAM; the framebuffer is only touched at
   present() time. 320*192 + 320*32 + 320*192 = 138 KB. That is a large
   slice of the 256 KB SDRAM but leaves ample room for game state. */
static u8 s_game[GAME_W * GAME_H];
static u8 s_panel[SCREEN_W * PANEL_H];
static u8 s_scratch[GAME_W * GAME_H];

/* Working palette (6-bit VGA values, as loaded from the resource) and the
   fade level applied on top of it. */
static u8  s_pal[768];
static int s_fade = 64;      /* 0 = black, 64 = full brightness */

/* Font glyphs, de-planed to 8x8 chunky at init. */
static u8  s_font[96][64];
static int s_font_ready;

/* ------------------------------------------------------------------ */

u8 *gfx_page(int page)
{
	switch (page) {
	case PAGE_PANEL:   return s_panel;
	case PAGE_SCRATCH: return s_scratch;
	default:           return s_game;
	}
}

int gfx_page_h(int page)
{
	return (page == PAGE_PANEL) ? PANEL_H : GAME_H;
}

void gfx_init(void)
{
	int i;

	for (i = 0; i < GAME_W * GAME_H; i++) {
		s_game[i] = 0;
		s_scratch[i] = 0;
	}
	for (i = 0; i < SCREEN_W * PANEL_H; i++)
		s_panel[i] = 0;
	s_fade = 64;
}

void gfx_clear_page(int page, u8 color)
{
	u8 *p = gfx_page(page);
	int n = SCREEN_W * gfx_page_h(page);

	while (n--)
		*p++ = color;
}

void gfx_fill_rect(int page, int x, int y, int w, int h, u8 color)
{
	u8 *base = gfx_page(page);
	const int ph = gfx_page_h(page);
	int yy;

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > ph)       h = ph - y;
	if (w <= 0 || h <= 0)
		return;

	for (yy = 0; yy < h; yy++) {
		u8 *d = base + ROW(y + yy) + x;
		int n = w;

		while (n--)
			*d++ = color;
	}
}

void gfx_pset(int page, int x, int y, u8 color)
{
	if (x < 0 || y < 0 || x >= SCREEN_W || y >= gfx_page_h(page))
		return;
	gfx_page(page)[ROW(y) + x] = color;
}

u8 gfx_point(int page, int x, int y)
{
	if (x < 0 || y < 0 || x >= SCREEN_W || y >= gfx_page_h(page))
		return 0;
	return gfx_page(page)[ROW(y) + x];
}

void gfx_hline(int page, int x, int y, int w, u8 color)
{
	gfx_fill_rect(page, x, y, w, 1, color);
}

void gfx_box(int page, int x, int y, int w, int h, u8 color)
{
	gfx_fill_rect(page, x, y, w, 1, color);
	gfx_fill_rect(page, x, y + h - 1, w, 1, color);
	gfx_fill_rect(page, x, y, 1, h, color);
	gfx_fill_rect(page, x + w - 1, y, 1, h, color);
}

/* ------------------------------------------------------------------ */

/*
 * Opaque blit.
 *
 * Clipping is resolved once, before the loops, rather than testing every
 * pixel: the previous form cost two compares and a branch per byte, which
 * on an SH2 fetching code from ROM dominated the blit. Row addressing uses
 * ROW() (a pair of shifts, since 320 = 256 + 64) so there is no multiply
 * per scanline either.
 */
void gfx_blit(int page, int x, int y, const u8 *src, int w, int h)
{
	u8 *base = gfx_page(page);
	const int ph = gfx_page_h(page);
	const int srcw = w;
	int sx = 0, sy = 0;
	int yy;

	if (x < 0) { sx = -x; w += x; x = 0; }
	if (y < 0) { sy = -y; h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > ph)       h = ph - y;
	if (w <= 0 || h <= 0)
		return;

	for (yy = 0; yy < h; yy++) {
		const u8 *s = src + (sy + yy) * srcw + sx;
		u8 *d = base + ROW(y + yy) + x;
		int n = w;

		while (n--)
			*d++ = *s++;
	}
}

/*
 * Masked blit (colour 0 == transparent). Same hoisting as gfx_blit; the
 * only per-pixel work left is the transparency test itself.
 */
void gfx_blit_masked(int page, int x, int y, const u8 *src, int w, int h)
{
	u8 *base = gfx_page(page);
	const int ph = gfx_page_h(page);
	const int srcw = w;
	int sx = 0, sy = 0;
	int yy;

	if (x < 0) { sx = -x; w += x; x = 0; }
	if (y < 0) { sy = -y; h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > ph)       h = ph - y;
	if (w <= 0 || h <= 0)
		return;

	for (yy = 0; yy < h; yy++) {
		const u8 *s = src + (sy + yy) * srcw + sx;
		u8 *d = base + ROW(y + yy) + x;
		int n = w;

		while (n--) {
			const u8 c = *s++;
			if (c)
				*d = c;
			d++;
		}
	}
}

/* Fast paths for the ubiquitous 16x16 case. */
void gfx_tile(int page, int x, int y, const u8 *tile)
{
	u8 *base = gfx_page(page);
	const int ph = gfx_page_h(page);
	int yy;

	/* Fully on-screen and long-aligned: 4 long stores per row. Tiles are
	   always placed on a 16px grid in practice, so this is the path that
	   actually runs. */
	if (x >= 0 && y >= 0 && x + 16 <= SCREEN_W && y + 16 <= ph &&
	    ((x | (int)(intptr_t)base) & 3) == 0) {
		u8 *d = base + ROW(y) + x;
		const u32 *s = (const u32 *)tile;

		for (yy = 0; yy < 16; yy++) {
			u32 *dw = (u32 *)d;

			dw[0] = s[0];
			dw[1] = s[1];
			dw[2] = s[2];
			dw[3] = s[3];
			s += 4;
			d += SCREEN_W;
		}
		return;
	}
	gfx_blit(page, x, y, tile, 16, 16);
}

/*
 * Tile blits into an arbitrary linear GAME_W-wide buffer. Used to build the
 * cached background page. Callers only ever place tiles on the 16px grid,
 * so these take the aligned fast path unconditionally.
 */
void gfx_tile_to(u8 *dst, int x, int y, const u8 *tile)
{
	u32 *d = (u32 *)(dst + ROW(y) + x);
	const u32 *s = (const u32 *)tile;
	int yy;

	for (yy = 0; yy < 16; yy++) {
		d[0] = s[0];
		d[1] = s[1];
		d[2] = s[2];
		d[3] = s[3];
		s += 4;
		d += SCREEN_W >> 2;
	}
}

void gfx_tile_masked_to(u8 *dst, int x, int y, const u8 *tile)
{
	u8 *d = dst + ROW(y) + x;
	const u8 *s = tile;
	int yy;

	for (yy = 0; yy < 16; yy++) {
		int xx;

		for (xx = 0; xx < 16; xx++) {
			const u8 c = s[xx];
			if (c)
				d[xx] = c;
		}
		s += 16;
		d += SCREEN_W;
	}
}

void gfx_tile_masked(int page, int x, int y, const u8 *tile)
{
	gfx_blit_masked(page, x, y, tile, 16, 16);
}

void gfx_copy(int dstpage, int dx, int dy,
              int srcpage, int sx, int sy, int w, int h)
{
	u8 *dbase = gfx_page(dstpage);
	u8 *sbase = gfx_page(srcpage);
	int dh = gfx_page_h(dstpage);
	int sh = gfx_page_h(srcpage);
	int yy;

	for (yy = 0; yy < h; yy++) {
		int syy = sy + yy;
		int dyy = dy + yy;
		u8 *d;
		const u8 *s;
		int xx;

		if (syy < 0 || syy >= sh || dyy < 0 || dyy >= dh)
			continue;
		s = sbase + syy * SCREEN_W + sx;
		d = dbase + dyy * SCREEN_W + dx;
		for (xx = 0; xx < w; xx++) {
			int dxx = dx + xx;
			int sxx = sx + xx;
			if (dxx < 0 || dxx >= SCREEN_W || sxx < 0 || sxx >= SCREEN_W)
				continue;
			d[xx] = s[xx];
		}
	}
}

/* ------------------------------------------------------------------ */
/* Font                                                                */

void gfx_font_init(const u8 *text_res, u32 size)
{
	/* TEXT holds 94 glyphs, 72 bytes each: 6-byte pic header then 8x8
	   pixels stored as four planes of 2 bytes per row. mkassets keeps
	   TEXT raw so we de-plane here (it is tiny and only done once). */
	u32 n = size / 72;
	u32 g;

	if (n > 94)
		n = 94;

	for (g = 0; g < n; g++) {
		const u8 *blk = text_res + g * 72 + 6;
		int plane, yy, xx;

		for (plane = 0; plane < 4; plane++) {
			for (yy = 0; yy < 8; yy++) {
				for (xx = 0; xx < 2; xx++) {
					u8 v = blk[plane * 16 + yy * 2 + xx];
					s_font[g][yy * 8 + xx * 4 + plane] = v;
				}
			}
		}
	}
	s_font_ready = 1;
}

static void draw_glyph(int page, int x, int y, int g, u8 color)
{
	const u8 *px = s_font[g];
	int yy, xx;

	for (yy = 0; yy < 8; yy++) {
		for (xx = 0; xx < 8; xx++) {
			if (px[yy * 8 + xx])
				gfx_pset(page, x + xx, y + yy, color);
		}
	}
}

int gfx_text_width(const char *s)
{
	int w = 0;
	while (*s++)
		w += 8;
	return w;
}

void gfx_print(int page, int x, int y, const char *s, u8 color)
{
	if (!s_font_ready)
		return;
	while (*s) {
		unsigned char ch = (unsigned char)*s++;
		if (ch > 31 && ch < 127)
			draw_glyph(page, x, y, ch - 32, color);
		x += 8;
	}
}

void gfx_print_shadow(int page, int x, int y, const char *s, u8 color)
{
	if (!s_font_ready)
		return;
	{
		const char *p = s;
		int xx = x;
		while (*p) {
			unsigned char ch = (unsigned char)*p++;
			if (ch > 31 && ch < 127) {
				draw_glyph(page, xx + 1, y + 1, ch - 32, 0);
			}
			xx += 8;
		}
	}
	gfx_print(page, x, y, s, color);
}

/* ------------------------------------------------------------------ */
/* Palette                                                             */

void gfx_set_palette(const u8 *pal768)
{
	int i;

	/* The PALETTE resource stores 8-bit values; the VGA DAC only took
	   6 bits, so the original shifted every component right by two
	   before programming it (see load_palette() in 1_GRP.C). Keep the
	   working palette in that same 0..63 space. */
	for (i = 0; i < 768; i++)
		s_pal[i] = (u8)(pal768[i] >> 2);

	/* The DOS game reserves 240..247 for the pulsing "magic" colours. */
	for (i = 240; i < 244; i++) {
		s_pal[i * 3 + 0] = 0;
		s_pal[i * 3 + 1] = 0;
		s_pal[i * 3 + 2] = 0x3B;
	}
	for (i = 244; i < 248; i++) {
		s_pal[i * 3 + 0] = 0x3B;
		s_pal[i * 3 + 1] = 0;
		s_pal[i * 3 + 2] = 0;
	}
	gfx_palette_apply();
}

void gfx_palette_apply(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		int r = s_pal[i * 3 + 0] * s_fade / 64;
		int g = s_pal[i * 3 + 1] * s_fade / 64;
		int b = s_pal[i * 3 + 2] * s_fade / 64;

		mars_set_color(i, r, g, b);
	}
}

void gfx_fade_in(void)
{
	int f;

	for (f = 0; f <= 64; f += 4) {
		s_fade = f;
		gfx_palette_apply();
		mars_wait_vblank();
	}
	s_fade = 64;
	gfx_palette_apply();
}

void gfx_fade_out(void)
{
	int f;

	for (f = 64; f >= 0; f -= 4) {
		s_fade = f;
		gfx_palette_apply();
		mars_wait_vblank();
	}
	s_fade = 0;
	gfx_palette_apply();
}

/* ------------------------------------------------------------------ */

void gfx_present(void)
{
	u8 *fb = mars_framebuffer();
	const u8 *src = s_game;
	int y;

	/* Game area occupies the top 192 lines. */
	for (y = 0; y < GAME_H; y++) {
		const u32 *s = (const u32 *)(src + ROW(y));
		u32 *d = (u32 *)(fb + ROW(y));
		int n = GAME_W >> 2;
		while (n--)
			*d++ = *s++;
	}

	/* Status panel underneath it. */
	for (y = 0; y < PANEL_H; y++) {
		const u32 *s = (const u32 *)(s_panel + ROW(y));
		u32 *d = (u32 *)(fb + ROW(GAME_H + y));
		int n = SCREEN_W >> 2;
		while (n--)
			*d++ = *s++;
	}

	mars_flip(1);
}
