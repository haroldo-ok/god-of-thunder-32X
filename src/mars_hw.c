/*
 * God of Thunder 32X - Mars hardware layer.
 *
 * The 32X VDP's 8-bit "packed pixel" mode is a near perfect match for the
 * original VGA Mode X game: both are 256-colour chunky bitmaps. The DOS
 * version used 4 hardware pages and the VGA split screen to keep a status
 * panel pinned to the bottom; here we render into a single 320x224 linear
 * buffer that is flipped once per frame.
 *
 * Framebuffer layout in the 32X:
 *   0x24000000 .. +0x200   line table (256 u16 entries, word offsets)
 *   0x24000200 ..          pixel data
 */

#include "mars.h"

#define FB_BASE   ((volatile u16 *)0x24000000)
#define FB_PIXELS ((volatile u8 *)0x24000200)

static u32 s_frame;
static u16 s_pad[2];

/* ------------------------------------------------------------------ */

static void mars_init_line_table(void)
{
	volatile u16 *lines = FB_BASE;
	int j;
	int blank;

	for (j = 0; j < SCREEN_H; j++)
		lines[j] = (u16)(j * SCREEN_W / 2 + 0x100);

	blank = j * SCREEN_W / 2;
	for (; j < 256; j++)
		lines[j] = (u16)(blank + 0x100);

	/* make sure the blank line really is blank */
	for (j = blank; j < blank + SCREEN_W / 2; j++)
		lines[j] = 0;
}

/* Flip and (optionally) wait until the flip has actually taken effect. */
void mars_flip(int wait_vblank)
{
	u16 want = (u16)(MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;

	MARS_VDP_FBCTL = want;
	if (wait_vblank) {
		while ((MARS_VDP_FBCTL & MARS_VDP_FS) != want)
			;
	}
	s_frame++;
}

u8 *mars_framebuffer(void)
{
	return (u8 *)FB_PIXELS;
}

void mars_wait_vblank(void)
{
	while ((MARS_VDP_DISPMODE & MARS_VDP_VBLK) != 0)
		;
	while ((MARS_VDP_DISPMODE & MARS_VDP_VBLK) == 0)
		;
}

void mars_clear(u8 color)
{
	volatile u32 *p = (volatile u32 *)FB_PIXELS;
	u32 v = ((u32)color << 24) | ((u32)color << 16) | ((u32)color << 8) | color;
	int n = SCREEN_W * SCREEN_H / 4;

	while (n--)
		*p++ = v;
}

/*
 * The DOS palette is 6 bits per gun (VGA DAC). The 32X CRAM is RGB555 with
 * the bits laid out as 0BBBBBGG GGGRRRRR, so we shift 6-bit values up by
 * two and pack.
 */
void mars_set_color(int idx, int r, int g, int b)
{
	if (idx < 0 || idx > 255)
		return;
	MARS_CRAM[idx] = (u16)(((b & 0x3E) << 9) | ((g & 0x3E) << 4) | ((r & 0x3E) >> 1));
}

void mars_set_palette(const u8 *rgb666, int first, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		int idx = first + i;
		int r = rgb666[i * 3 + 0];
		int g = rgb666[i * 3 + 1];
		int b = rgb666[i * 3 + 2];

		if (idx < 0 || idx > 255)
			continue;
		/* 6-bit -> 5-bit */
		MARS_CRAM[idx] = (u16)(((b >> 1) << 10) | ((g >> 1) << 5) | (r >> 1));
	}
}

/* ------------------------------------------------------------------ */

u16 mars_read_pad(int port)
{
	u16 v;

	if (port == 0)
		v = MARS_SYS_COMM8;
	else
		v = MARS_SYS_COMM10;

	/* 0xF000 means "no pad"; the present bit is not a button. */
	if ((v & 0xF000) == 0xF000)
		return 0;
	v &= 0x0FFF;
	s_pad[port & 1] = v;
	return v;
}

u32 mars_frame_count(void)
{
	return s_frame;
}

void mars_heartbeat(u16 value)
{
	MARS_SYS_COMM14 = value;
}

/* ------------------------------------------------------------------ */

void mars_init(void)
{
	int i;

	/* Wait until the 68000 has handed us the Mars hardware. */
	while ((MARS_SYS_INTMSK & MARS_SH2_ACCESS_VDP) == 0)
		;

	MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;

	/* Build the line table in *both* framebuffers and clear both. */
	for (i = 0; i < 2; i++) {
		volatile u32 *p = (volatile u32 *)FB_PIXELS;
		int n = SCREEN_W * SCREEN_H / 4;

		mars_init_line_table();
		while (n--)
			*p++ = 0;

		mars_flip(1);
	}

	/* Start from a known, definitely-not-black palette entry 0. */
	for (i = 0; i < 256; i++)
		MARS_CRAM[i] = 0;

	s_frame = 0;
}
