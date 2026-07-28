/*
 * God of Thunder 32X - input.
 *
 * The DOS game installed an INT 9 keyboard handler and read a 100-entry
 * key_flag[] array of scancodes. Here the 68000 samples both pads every
 * vblank and publishes them in the COMM mailbox; we translate to the
 * engine's logical buttons.
 *
 *   D-pad  -> movement
 *   B      -> swing hammer   (was Ctrl / Space)
 *   C      -> use magic      (was Alt)
 *   A      -> cycle item     (was Tab)
 *   Start  -> options menu   (was Esc)
 */

#include "got.h"

input_t g_input;

void input_poll(void)
{
	u16 raw = mars_read_pad(0);
	u16 v = 0;

	if (raw & SEGA_CTRL_UP)    v |= KEY_UP;
	if (raw & SEGA_CTRL_DOWN)  v |= KEY_DOWN;
	if (raw & SEGA_CTRL_LEFT)  v |= KEY_LEFT;
	if (raw & SEGA_CTRL_RIGHT) v |= KEY_RIGHT;
	if (raw & SEGA_CTRL_B)     v |= KEY_FIRE;
	if (raw & SEGA_CTRL_C)     v |= KEY_MAGIC;
	if (raw & SEGA_CTRL_A)     v |= KEY_SELECT;
	if (raw & SEGA_CTRL_START) v |= KEY_START;

	g_input.last    = g_input.held;
	g_input.held    = v;
	g_input.pressed = (u16)(v & ~g_input.last);
}
