/** @file src/audio/dsp_ql.c
 * Digitised sound for the Sinclair QL + QLion Gold card : the game's sound
 * effects and voices, and the drums of the music.
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
 * Effects : the game hands us one VOC sound at a time (8-bit unsigned mono).
 * It is streamed into the ring buffer from a 60 Hz timer callback: every
 * tick the space the player has already passed is refilled with the next
 * part of the sound, then with silence. When the sound has been played
 * out, playback is stopped so idle ticks cost nothing.
 *
 * Drums : midi_ql.c sends the music's drum hits to DSP_Drum. The drum
 * sounds are made at start-up (kick, snare, hi-hats, tom, cymbal) and up to
 * two of them are mixed into the same stream at 11025 Hz. While an effect
 * or voice plays, drum hits are ignored; an effect cuts off a drum.
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
#define DRUM_MARGIN 2048			/* silence written after the last drum */
#define DRUM_AHEAD  1536			/* drums are written at most this far ahead
					 * (1/7 s), so a new hit rewrites little */

static bool   s_init = false;
static uint8  s_vol[256];		/* effect sample -> sample at soundvolume % */

/* the stream */
static uint16 s_wpos = 0;		/* next frame to write */
static uint16 s_lastpos = 0;		/* player position at the last tick */
static uint32 s_written = 0;		/* frames written since start */
static uint32 s_played = 0;		/* frames played since start */
static uint32 s_end = 0;		/* s_played value at which the sound is over */
static bool   s_playing = false;	/* the player is running */
static bool   s_effect = false;		/* ... with a game effect (else drums) */

/* effect source */
static const uint8 *s_data = NULL;	/* next sample byte to write */
static uint32 s_left = 0;		/* sample bytes still to write */

/* drum sources */
typedef struct Drum {
	const uint8 *base;		/* the drum sound, NULL = none yet */
	const uint8 *data;		/* next sample to write */
	uint32 len;
	uint32 left;			/* samples still to write */
	long  start;			/* stream frame (s_written count) of its first sample */
	uint16 scale;			/* 0..256 */
} Drum;
static Drum   s_drum[2];

static void DSP_DrumsOff(void)
{
	s_drum[0].left = s_drum[1].left = 0;
	s_drum[0].base = s_drum[1].base = NULL;
}

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

/* ------------------------------------------------------------------ */
/* Drum sounds                                                          */
/* ------------------------------------------------------------------ */
#define DRUM_RATE 11025
enum { DK_KICK, DK_SNARE, DK_HATC, DK_HATO, DK_TOM, DK_CYM, DK_COUNT };
static const uint16 s_drumLen[DK_COUNT] = { 1800, 1500, 600, 3300, 2400, 6000 };
static uint8  s_drumData[1800 + 1500 + 600 + 3300 + 2400 + 6000];
static uint8 *s_drumSnd[DK_COUNT];

static uint32 s_seed = 12345;
static int Noise(void)			/* -128..127 */
{
	s_seed = s_seed * 1103515245UL + 12345UL;
	return (int)((s_seed >> 16) & 0xFF) - 128;
}

static int Tri(uint32 phase)		/* phase 0..65535 -> -128..127 */
{
	int t = (int)((phase >> 8) & 0xFF);
	return (t < 128) ? t * 2 - 128 : 383 - t * 2;
}

/* All in integers : the QL has no FPU and this runs once at start-up. */
static void DSP_MakeDrums(void)
{
	uint8 *p = s_drumData;
	int k;

	for (k = 0; k < DK_COUNT; k++) {
		long len = s_drumLen[k];
		long i;
		uint32 phase = 0;
		int prev = 0;

		s_drumSnd[k] = p;
		for (i = 0; i < len; i++) {
			long e  = ((len - i) * 256) / len;	/* 256 .. 0, linear */
			long e2 = (e * e) >> 8;		/* faster fall */
			long e4 = (e2 * e2) >> 8;		/* fastest */
			long s = 0, n, f;

			switch (k) {
				case DK_KICK:		/* triangle falling 170 -> 45 Hz */
					f = 45 + ((125 * e4) >> 8);
					phase += (uint32)((f << 16) / DRUM_RATE);
					s = (Tri(phase) * e2) >> 8;
					break;
				case DK_SNARE:		/* noise + 190 Hz body */
					f = 190;
					phase += (uint32)((f << 16) / DRUM_RATE);
					s = ((Noise() * 3 + Tri(phase) * 2) / 5 * e2) >> 8;
					break;
				case DK_HATC:		/* bright noise, short */
				case DK_HATO:		/* bright noise, longer */
				case DK_CYM:		/* bright noise, long */
					n = Noise();
					s = n - prev;	/* simple high-pass */
					prev = n;
					s = s / 2;
					s = (s * (k == DK_HATC ? e2 : e)) >> 8;
					if (k == DK_CYM) s = s * 3 / 4;
					break;
				case DK_TOM:		/* triangle falling 150 -> 95 Hz */
					f = 95 + ((55 * e2) >> 8);
					phase += (uint32)((f << 16) / DRUM_RATE);
					s = (Tri(phase) * e2) >> 8;
					break;
			}
			if (s > 127) s = 127;
			if (s < -127) s = -127;
			*p++ = (uint8)(s + 128);
		}
	}
}

/* General MIDI drum note -> drum sound */
static int DSP_DrumKind(uint8 note)
{
	switch (note) {
		case 35: case 36:                                   return DK_KICK;
		case 37: case 38: case 39: case 40:                 return DK_SNARE;
		case 41: case 43: case 45: case 47: case 48: case 50: return DK_TOM;
		case 46:                                            return DK_HATO;
		case 49: case 51: case 52: case 53: case 55: case 57: case 59: return DK_CYM;
		default:                                            return DK_HATC;
	}
}

/* ------------------------------------------------------------------ */
/* Stream                                                               */
/* ------------------------------------------------------------------ */
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

/* Bring s_played up to date with the player. */
static void DSP_Update(void)
{
	uint16 pos = DSP_PlayPos();
	uint32 now = Timer_GetTime();

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
}

/* Refill everything the player has passed, keeping DA_GAP frames of margin. */
static void DSP_Refill(void)
{
	uint32 ahead, room;

	DSP_Update();

	ahead = (s_written > s_played) ? s_written - s_played : 0;
	if (ahead + DA_GAP >= DA_FRAMES) return;
	room = DA_FRAMES - DA_GAP - ahead;

	if (s_effect) {
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
		return;
	}

	/* drums : write only up to a margin of silence past the end */
	if (s_written >= s_end + DRUM_MARGIN) return;
	if (room > s_end + DRUM_MARGIN - s_written) room = s_end + DRUM_MARGIN - s_written;
	if (s_written >= s_played + DRUM_AHEAD) return;
	if (room > s_played + DRUM_AHEAD - s_written) room = s_played + DRUM_AHEAD - s_written;
	while (room-- > 0) {
		long a = 0;
		int k;
		uint8 b;
		for (k = 0; k < 2; k++) {
			Drum *d = &s_drum[k];
			if (d->left > 0) {
				a += (((long)*d->data++ - 128) * d->scale) >> 8;
				d->left--;
			}
		}
		if (a > 127) a = 127;
		if (a < -127) a = -127;
		b = (uint8)(a + 128);
		DA_BUF[s_wpos] = (uint16)((b << 8) | b);
		s_wpos = (uint16)((s_wpos + 1) & DA_MASK);
		s_written++;
	}
}

static void DSP_Tick(void)
{
	if (!s_playing) return;

	DSP_Refill();

	if (s_played >= s_end || Timer_GetTime() - s_startTime > s_maxTime) {
		*DA_CTRL = 0;		/* fully played : stop the player */
		s_playing = false;
		s_effect = false;
		DSP_DrumsOff();
	}
}

/* (Re)start the stream at the player's position + DA_GAP. */
static void DSP_StartStream(uint32 freq, uint32 len)
{
	uint16 pos = DSP_PlayPos();
	s_lastpos   = pos;
	s_freq      = freq;
	s_startTime = Timer_GetTime();
	s_lastMove  = s_startTime;
	s_timeBased = false;
	s_maxTime   = (len * 1000UL) / freq + DSP_GRACE_MS;
	s_wpos      = (uint16)((pos + DA_GAP) & DA_MASK);
	s_written   = DA_GAP;		/* the gap is already silence (or old sound) */
	s_played    = 0;
	s_end       = DA_GAP + len;
	s_playing   = true;
}

/* ------------------------------------------------------------------ */
/* Interface                                                            */
/* ------------------------------------------------------------------ */
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
	DSP_MakeDrums();
	Timer_Add(DSP_Tick, 1000000 / 60, false);
	s_init = true;
	return true;
}

void DSP_Uninit(void)
{
	if (!s_init) return;
	*DA_CTRL = 0;
	s_playing = s_effect = false;
	Timer_Remove(DSP_Tick);
	s_init = false;
}

/* Stops a game effect; drums of the music are left alone. */
void DSP_Stop(void)
{
	if (!s_init || !s_effect) return;
	*DA_CTRL = 0;
	s_playing = false;
	s_effect = false;
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

	if (!s_init) return;
	*DA_CTRL = 0;			/* stops an effect or drums */
	s_playing = s_effect = false;
	s_left = 0;
	DSP_DrumsOff();

	data += READ_LE_UINT16(data + 20);	/* skip the VOC header */
	if (*data != 1) return;			/* not a sound data block */

	len  = (READ_LE_UINT32(data) >> 8) - 2;	/* block length minus 2 header bytes */
	freq = 1000000UL / (256 - data[4]);	/* VOC time constant */
	data += 6;
	if (freq < 1300) freq = 1300;		/* keep the period within 12 bits */

	DSP_FillSilence();
	*DA_PERIOD = (uint16)(5000000UL / freq);
	*DA_CTRL = 1;

	DSP_StartStream(freq, len);
	s_effect = true;
	s_data   = data;
	s_left   = len;

	DSP_Refill();
}

/**
 * A drum hit of the music. level 0..127. Returns false when digitised sound
 * is off (the music then plays the drum on the AY), true otherwise, also
 * when the hit is dropped because an effect or voice is playing.
 */
bool DSP_Drum(uint8 note, uint8 level)
{
	const uint8 *snd;
	uint32 len, keep;
	int k, slot;

	if (!s_init) return false;
	if (s_effect || level == 0) return true;	/* effects have priority */

	k   = DSP_DrumKind(note);
	snd = s_drumSnd[k];
	len = s_drumLen[k];

	if (s_playing) {
		/* the stream restarts just after the player : put the drums still
		 * sounding back to the sample the player will reach there */
		long at;
		DSP_Update();
		at = (long)(s_played + DA_GAP);
		for (slot = 0; slot < 2; slot++) {
			Drum *d = &s_drum[slot];
			long off;
			if (d->base == NULL) continue;
			off = at - d->start;
			if (off < 0) off = 0;
			if (off >= (long)d->len) { d->left = 0; continue; }
			d->data  = d->base + off;
			d->left  = d->len - (uint32)off;
			d->start = DA_GAP - off;	/* in the new stream's frames */
		}
	} else {
		/* start the player; silence where it starts */
		uint16 pos = DSP_PlayPos();
		int i;
		for (i = 0; i < DA_GAP * 2; i++) DA_BUF[(pos + i) & DA_MASK] = SILENCE;
		DSP_DrumsOff();
		*DA_PERIOD = (uint16)(5000000UL / DRUM_RATE);
		*DA_CTRL = 1;
	}

	/* a free slot, else the one closer to its end */
	slot = (s_drum[0].left == 0) ? 0 : (s_drum[1].left == 0) ? 1 :
	       (s_drum[0].left < s_drum[1].left) ? 0 : 1;
	s_drum[slot].base  = snd;
	s_drum[slot].data  = snd;
	s_drum[slot].len   = len;
	s_drum[slot].left  = len;
	s_drum[slot].start = DA_GAP;
	s_drum[slot].scale = (uint16)(level * 2);

	keep = (s_drum[0].left > s_drum[1].left) ? s_drum[0].left : s_drum[1].left;
	DSP_StartStream(DRUM_RATE, keep);
	DSP_Refill();
	return true;
}

uint8 DSP_GetStatus(void)
{
	/* 2 = playing, 0 = idle (same convention as the other DSP drivers);
	 * the music's drums don't count */
	return s_effect ? 2 : 0;
}
