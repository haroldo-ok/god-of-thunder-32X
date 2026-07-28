/*
 * God of Thunder 32X - audio.
 *
 * STUBBED FOR NOW, PWM TO FOLLOW.
 *
 * The DOS original used a licensed third-party library that was never part
 * of the source release, driving AdLib/Sound Blaster. The resources that
 * remain are:
 *   SONGn / OPENSONG / WINSONG / BOSSSONG  - sequenced music
 *   DIGSOUND                               - digitised sound effects
 *   PCSOUNDS                               - PC speaker effects
 *
 * All of it is still embedded in the ROM (see tools/mkassets.py), so the
 * PWM implementation can be dropped in behind these entry points without
 * touching the rest of the engine. Until then every call is a no-op and the
 * game runs silently at full speed.
 */

#include "got.h"

static int s_music_cur = -1;

void snd_init(void)
{
	/* PWM setup will live here:
	 *   MARS_PWM_CYCLE = (clock / sample_rate) + 1
	 *   MARS_PWM_CTRL  = stereo | timer interrupt every sample
	 * plus a ring buffer fed from the DIGSOUND resource.
	 */
	s_music_cur = -1;
}

void snd_play(int index)
{
	(void)index;
}

void music_play(int num, int override)
{
	(void)override;
	s_music_cur = num;
}

void music_pause(void)
{
}

void music_resume(void)
{
}

/* Called once per frame; will drain the PWM ring buffer later. */
void snd_update(void)
{
}
