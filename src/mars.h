/* God of Thunder 32X - Mars (SH2) hardware definitions. */
#ifndef GOT_MARS_H
#define GOT_MARS_H

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef signed short   s16;
typedef unsigned int   u32;
typedef signed int     s32;

#define MARS_CRAM           ((volatile u16 *)0x20004200)
#define MARS_FRAMEBUFFER    ((volatile u16 *)0x24000000)
#define MARS_OVERWRITE_IMG  ((volatile u16 *)0x24020000)

#define MARS_SYS_INTMSK     (*(volatile u16 *)0x20004000)
#define MARS_SYS_VRESI_CLR  (*(volatile u16 *)0x20004014)
#define MARS_SYS_VINT_CLR   (*(volatile u16 *)0x20004016)
#define MARS_SYS_HINT_CLR   (*(volatile u16 *)0x20004018)
#define MARS_SYS_CMDI_CLR   (*(volatile u16 *)0x2000401A)
#define MARS_SYS_PWMI_CLR   (*(volatile u16 *)0x2000401C)

#define MARS_SYS_COMM0      (*(volatile s16 *)0x20004020)
#define MARS_SYS_COMM2      (*(volatile s16 *)0x20004022)
#define MARS_SYS_COMM4      (*(volatile s16 *)0x20004024)
#define MARS_SYS_COMM6      (*(volatile s16 *)0x20004026)
#define MARS_SYS_COMM8      (*(volatile u16 *)0x20004028)
#define MARS_SYS_COMM10     (*(volatile u16 *)0x2000402A)
#define MARS_SYS_COMM12     (*(volatile u16 *)0x2000402C)
#define MARS_SYS_COMM14     (*(volatile u16 *)0x2000402E)

#define MARS_PWM_CTRL       (*(volatile u16 *)0x20004030)
#define MARS_PWM_CYCLE      (*(volatile u16 *)0x20004032)
#define MARS_PWM_LEFT       (*(volatile u16 *)0x20004034)
#define MARS_PWM_RIGHT      (*(volatile u16 *)0x20004036)
#define MARS_PWM_MONO       (*(volatile u16 *)0x20004038)

#define MARS_VDP_DISPMODE   (*(volatile u16 *)0x20004100)
#define MARS_VDP_FILLEN     (*(volatile u16 *)0x20004104)
#define MARS_VDP_FILADR     (*(volatile u16 *)0x20004106)
#define MARS_VDP_FILDAT     (*(volatile u16 *)0x20004108)
#define MARS_VDP_FBCTL      (*(volatile u16 *)0x2000410A)

#define MARS_SH2_ACCESS_VDP 0x8000
#define MARS_NTSC_FORMAT    0x8000
#define MARS_VDP_PRIO_32X   0x0080
#define MARS_224_LINES      0x0000
#define MARS_240_LINES      0x0040
#define MARS_VDP_MODE_OFF   0x0000
#define MARS_VDP_MODE_256   0x0001
#define MARS_VDP_MODE_32K   0x0002
#define MARS_VDP_VBLK       0x8000
#define MARS_VDP_FS         0x0001

/*
 * Genesis pad bits as published by the 68000 into COMM8/COMM10.
 * Layout matches d32xr's get_pad:
 *     0 0 0 1  M X Y Z  S A C B  R L D U   (active high)
 * An empty port reports 0xF000.
 */
#define SEGA_CTRL_UP        0x0001
#define SEGA_CTRL_DOWN      0x0002
#define SEGA_CTRL_LEFT      0x0004
#define SEGA_CTRL_RIGHT     0x0008
#define SEGA_CTRL_B         0x0010
#define SEGA_CTRL_C         0x0020
#define SEGA_CTRL_A         0x0040
#define SEGA_CTRL_START     0x0080
#define SEGA_CTRL_Z         0x0100
#define SEGA_CTRL_Y         0x0200
#define SEGA_CTRL_X         0x0400
#define SEGA_CTRL_MODE      0x0800
#define SEGA_CTRL_PRESENT   0x1000
#define SEGA_CTRL_NONE      0xF000

/* Display geometry. The 32X 8-bit "packed pixel" mode gives us a linear
   320x224 chunky framebuffer, which is exactly what the DOS game wants
   (it ran 320x192 game area + 48 line status panel = 320x240 logical). */
/*
 * NTSC 32X displays 224 lines (240-line mode is PAL-only), while the DOS
 * game wanted 192 play area + 48 status panel = 240. The play area is kept
 * pixel-exact at 320x192 and the status panel is re-laid out into the 32
 * lines that remain - see panel.c.
 */
#define SCREEN_W    320
#define SCREEN_H    224
#define GAME_W      320
#define GAME_H      192
#define PANEL_H     32
#define STATUS_SRC_H 48

void     mars_init(void);
void     mars_flip(int wait_vblank);
void     mars_set_palette(const u8 *rgb666, int first, int count);
void     mars_set_color(int idx, int r, int g, int b);
u16      mars_read_pad(int port);
u32      mars_frame_count(void);
void     mars_wait_vblank(void);
u8      *mars_framebuffer(void);
void     mars_clear(u8 color);

/* Watchdog/heartbeat used by the automated tests to prove the game is
   alive and producing frames. Published in COMM14 for the emulator. */
void     mars_heartbeat(u16 value);

#endif /* GOT_MARS_H */
