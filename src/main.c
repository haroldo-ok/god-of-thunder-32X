/*
 * God of Thunder 32X - SH2 entry points.
 *
 * crt0.s brings both SH2s up, clears bss, and jumps here on the master.
 * The slave currently idles; once PWM audio lands it will run the mixer.
 */

#include "got.h"
#include "gfx.h"
#include "res.h"

/* Referenced by crt0.s. */
volatile unsigned mars_pwdt_ovf_count;
volatile unsigned mars_swdt_ovf_count;

void pri_vbi_handler(void)
{
}

void pri_cmd_handler(void)
{
}

void sec_cmd_handler(void)
{
}

void sec_dma1_handler(void)
{
}

void secondary(void)
{
	/* Slave SH2: idle for now. The PWM mixer will live here. */
	for (;;)
		;
}

int main(void)
{
	mars_init();
	game_init();

	for (;;) {
		game_run_frame();
	}

	return 0;
}
