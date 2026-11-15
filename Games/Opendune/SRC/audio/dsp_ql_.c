/** @file src/audio/dsp_ql.c
 * Digitised sound (effects and voices) for the Sinclair QL + QLion Gold card.
 *
 * QLion PCM playback:
 *   $C04000   sample buffer, 16 KB (sbuf16 = 1) or 8 KB on older QLion
 *             firmware, one word per stereo frame,
 *             left in the high byte, right in the low byte, unsigned 8-bit;
 *             playback loops over the buffer while running
 *   $800034   write 1/0 = start/stop, read = current playback position (words)
 *   $800036   period = 5000000 / frequency
 *   $800038   bit 0 : 1 = 16 KB buffer (QLion card only, see DSP_Init)
 *   $80001E   card ID word, $4C30 ("L0") = QLion card
 *
 * The game hands us one VOC sound at a time (8-bit unsigned mono). It is
 * streamed into the ring buffer from a 60 Hz timer callback: every tick the
 * space the player has already passed is refilled with the next part of the
 * sound, then with silence. When the sound has been played out, playback is
 * stopped so idle ticks cost nothing.
 *
 * opendune.ini : soundbuffer=16 (default) 16 KB buffer, 8 = 8 KB buffer
 * (older QLion firmware), 0 = no digitised sound; soundvolume=100 (%).
 */

#include <string.h>
#include "types.h"
#include "dsp.h"
#include "../os/endian.h"
#include "../timer.h"
#include "../inifile.h"

#define DA_BUF      ((volatile uint16 *)0xC04000UL)
#define DA_CTRL     ((volatile uint16 *)0x800034UL)	/* write start/stop, read position */
#define DA_PERIOD   ((volatile uint16 *)0x800036UL)
#define DA_SIZE     ((volatile uint16 *)0x800038UL)
#define QL_CARD_ID  ((volatile uint16 *)0x80001EUL)
#define QLION_ID    0x4C30			/* "L0" */

/* Buffer size in stereo frames (2 bytes each) : 8192 = 16 KB, 4096 = 8 KB.
 * Set once in DSP_Init. */
static uint16 s_frames = 8192;
static uint16 s_mask   = 8191;
#define DA_FRAMES   s_frames
#define DA_MASK     s_mask
#define DA_GAP      64				/* frames kept between player and writer */
#define SILENCE     0x8080

static bool   s_init = false;
static bool   s_enabled = false;	/* soundbuffer is 8 or 16 */
static uint8  s_vol[256];		/* sample -> sample at soundvolume % */

static const uint8 *s_data = NULL;	/* next sample byte to write */
static uint32 s_left = 0;		/* sample bytes still to write */
static uint16 s_wpos = 0;		/* next frame to write */
static uint16 s_lastpos = 0;		/* player position at the last tick */
static uint32 s_written = 0;		/* frames written since start */
static uint32 s_played = 0;		/* frames played since start */
static uint32 s_end = 0;		/* s_played value at which the sound is over */
static bool   s_playing = false;

/* Timing fallback : if the position register does not move (or a core
 * counts differently), playback is followed by elapsed time instead, and
 * a sound always ends after its own length + DSP_GRACE_MS. */
#define DSP_STUCK_MS  50
#define DSP_GRACE_MS  500
static uint32 s_freq = 11025;		/* current sound's sample rate */
static uint32 s_startTime = 0;		/* Timer_GetTime() at start */
static uint32 s_lastMove = 0;		/* last time the position changed */
static uint32 s_maxTime = 0;		/* ms after which the sound is over regardless */
static bool   s_timeBased = false;	/* position register unusable this sound */

static uint16 DSP_PlayPos(void)
{
	return (uint16)(*DA_CTRL & DA_MASK);	/* position counts stereo words */
}

static void DSP_FillSilence(void)
{
	volatile uint16 *p = DA_BUF;
	int i;
	for (i = 0; i < DA_FRAMES; i++) *p++ = SILENCE;
}

/* Refill everything the player has passed, keeping DA_GAP frames of margin. */
static void DSP_Refill(void)
{
	uint16 pos = DSP_PlayPos();
	uint32 now = Timer_GetTime();
	uint32 ahead, room;

	if (pos != s_lastpos) {
		s_played += (uint16)((pos - s_lastpos) & DA_MASK);
		s_lastpos = pos;
		s_lastMove = now;
	} else if (now - s_lastMove > DSP_STUCK_MS) {
		s_timeBased = true;
	}
	if (s_timeBased) {
		/* frames played = elapsed time x sample rate (+ the initial gap) */
		uint32 t = DA_GAP + (now - s_startTime) * s_freq / 1000;
		if (t > s_played) s_played = t;
	}

	ahead = (s_written > s_played) ? s_written - s_played : 0;
	if (ahead + DA_GAP >= DA_FRAMES) return;
	room = DA_FRAMES - DA_GAP - ahead;

	while (room-- > 0) {
		uint16 v = SILENCE;
		if (s_left > 0) {
			uint8 b = s_vol[*s_data++];
			s_left--;
			v = (uint16)((b << 8) | b);	/* mono -> left and right */
		}
		DA_BUF[s_wpos] = v;
		s_wpos = (uint16)((s_wpos + 1) & DA_MASK);
		s_written++;
	}
}

static void DSP_Tick(void)
{
	if (!s_playing) return;

	DSP_Refill();

	if (s_played >= s_end || Timer_GetTime() - s_startTime > s_maxTime) {
		*DA_CTRL = 0;		/* sound fully played : stop the player */
		s_playing = false;
	}
}

bool DSP_Init(void)
{
	long kb  = IniFile_GetInteger("soundbuffer", 16);
	long vol = IniFile_GetInteger("soundvolume", 100);
	int i;

	/* soundbuffer=16 (default : current QLion firmware and the standalone
	 * cores), 8 (older QLion firmware with only the 8 KB buffer), 0 = off */
	if (kb != 8 && kb != 16) return true;

	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	for (i = 0; i < 256; i++) s_vol[i] = (uint8)(128 + ((i - 128) * vol) / 100);

	*DA_CTRL = 0;
	/* Only the QLion card has the 8/16 KB switch; the standalone cores
	 * always have a 16 KB buffer and may use $800038 for something else. */
	if (*QL_CARD_ID == QLION_ID && kb == 8) {
		s_frames = 4096;
		s_mask   = 4095;
		*DA_SIZE = 0;		/* 8 KB sample buffer */
	} else {
		s_frames = 8192;
		s_mask   = 8191;
		if (*QL_CARD_ID == QLION_ID) *DA_SIZE = 1;	/* 16 KB sample buffer */
	}
	DSP_FillSilence();
	Timer_Add(DSP_Tick, 1000000 / 60, false);
	s_enabled = true;
	s_init = true;
	return true;
}

void DSP_Uninit(void)
{
	if (!s_init) return;
	DSP_Stop();
	Timer_Remove(DSP_Tick);
	s_init = false;
}

void DSP_Stop(void)
{
	if (!s_enabled) return;
	*DA_CTRL = 0;
	s_playing = false;
	s_left = 0;
}

/**
 * Play a VOC sound. data points to the start of the VOC file.
 * Only block type 1 (8-bit unsigned PCM) is used by Dune II.
 */
void DSP_Play(const uint8 *data)
{
	uint32 len;
	uint32 freq;
	uint16 pos;

	if (!s_init) return;
	DSP_Stop();

	data += READ_LE_UINT16(data + 20);	/* skip the VOC header */
	if (*data != 1) return;			/* not a sound data block */

	len  = (READ_LE_UINT32(data) >> 8) - 2;	/* block length minus 2 header bytes */
	freq = 1000000UL / (256 - data[4]);	/* VOC time constant */
	data += 6;
	if (freq < 1300) freq = 1300;		/* keep the period within 12 bits */

	DSP_FillSilence();
	*DA_PERIOD = (uint16)(5000000UL / freq);
	*DA_CTRL = 1;

	pos = DSP_PlayPos();
	s_lastpos = pos;
	s_freq      = freq;
	s_startTime = Timer_GetTime();
	s_lastMove  = s_startTime;
	s_timeBased = false;
	s_maxTime   = (len * 1000UL) / freq + DSP_GRACE_MS;
	s_wpos    = (uint16)((pos + DA_GAP) & DA_MASK);
	s_written = DA_GAP;			/* the gap is already silence */
	s_played  = 0;
	s_data    = data;
	s_left    = len;
	s_end     = DA_GAP + len;
	s_playing = true;

	DSP_Refill();
}

uint8 DSP_GetStatus(void)
{
	/* 2 = playing, 0 = idle (same convention as the other DSP drivers) */
	return s_playing ? 2 : 0;
}
