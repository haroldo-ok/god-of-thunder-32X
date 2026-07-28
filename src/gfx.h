/* God of Thunder 32X - graphics primitives (linear 8bpp replacements for
   the original VGA Mode X routines). */
#ifndef GOT_GFX_H
#define GOT_GFX_H

#include "mars.h"

/* The game logic still thinks in terms of "pages". On the 32X we render
   everything into one off-screen buffer in SDRAM and blit it to the
   framebuffer at flip time, which keeps tearing away and makes the
   scrolling transitions cheap. */
#define PAGE_GAME   0   /* 320x192 play area                */
#define PAGE_PANEL  1   /* 320x32 status panel              */
#define PAGE_SCRATCH 2  /* off-screen composition (scroll)  */

void  gfx_init(void);

u8   *gfx_page(int page);
int   gfx_page_h(int page);

void  gfx_clear_page(int page, u8 color);
void  gfx_fill_rect(int page, int x, int y, int w, int h, u8 color);
void  gfx_pset(int page, int x, int y, u8 color);
u8    gfx_point(int page, int x, int y);
void  gfx_hline(int page, int x, int y, int w, u8 color);
void  gfx_box(int page, int x, int y, int w, int h, u8 color);

/* Blit a 16x16 opaque tile (background icon). */
void  gfx_tile(int page, int x, int y, const u8 *tile);
/* Same, but straight into a caller-supplied GAME_W x GAME_H buffer. */
void  gfx_tile_to(u8 *dst, int x, int y, const u8 *tile);
void  gfx_tile_masked_to(u8 *dst, int x, int y, const u8 *tile);
/* Blit a 16x16 tile treating colour 0 as transparent (objects/sprites). */
void  gfx_tile_masked(int page, int x, int y, const u8 *tile);
/* Arbitrary-size masked blit. */
void  gfx_blit_masked(int page, int x, int y, const u8 *src, int w, int h);
void  gfx_blit(int page, int x, int y, const u8 *src, int w, int h);

/* Copy a rectangle between pages (used for the scrolling transitions). */
void  gfx_copy(int dstpage, int dx, int dy,
               int srcpage, int sx, int sy, int w, int h);

/* Text. The original 8x8 font lives in the TEXT resource as 94 glyphs of
   72 bytes (a 6-byte header + 8x8 paned pixels). */
void  gfx_font_init(const u8 *text_res, u32 size);
void  gfx_print(int page, int x, int y, const char *s, u8 color);
void  gfx_print_shadow(int page, int x, int y, const char *s, u8 color);
int   gfx_text_width(const char *s);

/* Present: composites the game page + status panel into the 32X
   framebuffer and flips. */
void  gfx_present(void);

/* Palette handling (fade in/out mirror the DOS behaviour). */
void  gfx_set_palette(const u8 *pal768);
void  gfx_fade_in(void);
void  gfx_fade_out(void);
void  gfx_palette_apply(void);

#endif /* GOT_GFX_H */
