/*
 * God of Thunder 32X - freestanding runtime helpers.
 *
 * We link with -nostdlib, so the handful of libc facilities the engine
 * actually uses are provided here. They are deliberately small and
 * dependency-free.
 */

#include "got.h"

void *got_memcpy(void *d, const void *s, u32 n)
{
	u8 *dp = (u8 *)d;
	const u8 *sp = (const u8 *)s;

	/* Word-copy when both sides share alignment - the SH2 traps on
	   unaligned long accesses, so check before being clever. */
	if (((u32)dp & 3) == 0 && ((u32)sp & 3) == 0) {
		u32 *dw = (u32 *)dp;
		const u32 *sw = (const u32 *)sp;
		u32 words = n >> 2;

		while (words--)
			*dw++ = *sw++;
		dp = (u8 *)dw;
		sp = (const u8 *)sw;
		n &= 3;
	}
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *got_memset(void *d, int c, u32 n)
{
	u8 *dp = (u8 *)d;
	u8 v = (u8)c;

	if (((u32)dp & 3) == 0 && n >= 4) {
		u32 w = ((u32)v << 24) | ((u32)v << 16) | ((u32)v << 8) | v;
		u32 *dw = (u32 *)dp;
		u32 words = n >> 2;

		while (words--)
			*dw++ = w;
		dp = (u8 *)dw;
		n &= 3;
	}
	while (n--)
		*dp++ = v;
	return d;
}

int got_strlen(const char *s)
{
	int n = 0;

	while (*s++)
		n++;
	return n;
}

void got_itoa(int v, char *buf)
{
	char tmp[12];
	int n = 0;
	int neg = 0;

	if (v < 0) {
		neg = 1;
		v = -v;
	}
	if (v == 0)
		tmp[n++] = '0';
	while (v > 0) {
		tmp[n++] = (char)('0' + (v % 10));
		v /= 10;
	}
	if (neg)
		*buf++ = '-';
	while (n > 0)
		*buf++ = tmp[--n];
	*buf = 0;
}

/*
 * The original used the C library rand() plus a 200-byte table of
 * pre-rolled values (the RANDOM resource) for the movement patterns.
 * A plain LCG reproduces the same distribution closely enough and keeps
 * behaviour deterministic for the automated tests.
 */
static u32 s_seed = 1234;

void got_srand(u32 seed)
{
	s_seed = seed ? seed : 1;
}

int got_rnd(int max)
{
	if (max <= 0)
		return 0;
	s_seed = s_seed * 1103515245u + 12345u;
	return (int)((s_seed >> 16) % (u32)max);
}

/* GCC may emit calls to these for struct assignment / array init even at
   -Os, so provide them under their standard names too. */
void *memcpy(void *d, const void *s, unsigned long n)
{
	return got_memcpy(d, s, (u32)n);
}

void *memset(void *d, int c, unsigned long n)
{
	return got_memset(d, c, (u32)n);
}
