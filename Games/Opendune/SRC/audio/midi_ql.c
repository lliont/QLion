/** @file src/audio/midi_ql.c
 * Music for the Sinclair QL + QLion Gold card : a small MIDI synth on the
 * card's YM2149 (AY-3-8910 compatible), 3 tone voices + noise.
 *
 * mt32mpu.c turns the Dune II XMI songs into MIDI messages and sends them
 * here. Each note-on takes one of the 3 AY voices (newest note wins when
 * all are busy). Drums (MIDI channel 10) use the noise generator, pitched
 * by drum type (only when digitised sound is off: normally the drums are
 * samples played by dsp_ql.c, see DSP_Drum), and never take a voice from a note : a drum plays alone on
 * a free voice at drumvolume percent; when all voices hold notes, the bass
 * voice plays the drum for 1/10 s in place of its tone and then continues
 * its note. Volume follows a decibel curve, since the AY levels are about
 * 3 dB apart. A 60 Hz timer callback gives notes a slow decay and makes
 * lone drums fade out quickly.
 *
 * opendune.ini : music=0 (default) no music, music=1 on;
 * musicvolume=60 (%), drumvolume=50 (%).
 */

#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "midi.h"
#include "../timer.h"
#include "../inifile.h"

/* ------------------------------------------------------------------ */
/* QLion YM2149                                                         */
/* ------------------------------------------------------------------ */
#define AY_DATA   ((volatile uint8 *)0x800028UL)
#define AY_CTRL   ((volatile uint8 *)0x80002AUL)
#define QL_CARD_ID ((volatile uint16 *)0x80001EUL)
#define QLION_ID   0x4C30		/* "L0" : QLion card, AY at 2.75 MHz */
static unsigned long s_ayClockKHz = 2750UL;	/* standalone cores : 2.5 MHz */

/* Same bus sequence as aywrite() in the QLion ymplayer :
 * ay_data is the AY data bus, ay_ctrl drives the bus control lines
 * (5 = latch register address, 4 = write data, 0 = inactive).
 * Each active state is written twice to hold it long enough. */
/* music watchdog, ql_midiwatch.s */
extern volatile int16 ql_midi_idle;
extern volatile uint8 ql_midi_muted;
extern volatile uint8 ql_ay_busy;
extern void QL_MidiWatch_Install(void);
extern void QL_MidiWatch_Remove(void);

/* dsp_ql.c : drum hit on the sample buffer; false = digitised sound off */
extern bool DSP_Drum(uint8 note, uint8 level);

static void AY_Write(uint8 reg, uint8 val)
{
	ql_ay_busy = 1;		/* keep the watchdog off the AY bus */
	*AY_DATA = reg;
	*AY_CTRL = 5;
	*AY_CTRL = 5;
	*AY_CTRL = 0;
	*AY_DATA = val;
	*AY_CTRL = 4;
	*AY_CTRL = 4;
	*AY_CTRL = 0;
	ql_ay_busy = 0;
}

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */
#define VOICES     3
#define DRUM_CHAN  9			/* MIDI channel 10 */

typedef struct Voice {
	int    chan;			/* MIDI channel, -1 = free */
	uint8  note;
	uint8  level;			/* current AY volume 0..15 */
	uint16 level16;			/* level << 4, for smooth decay */
	uint16 decay;			/* subtracted from level16 each tick */
	uint32 age;			/* for voice stealing */
	bool   drum;
} Voice;

static Voice  s_voice[VOICES];
static uint8  s_chanVolume[16];		/* CC 7, 0..127 */
static uint16 s_period[128];		/* AY tone period per MIDI note */
static uint8  s_levelCurve[128];	/* amplitude 0..127 -> AY level 0..15 */
static uint32 s_clock = 0;
static uint8  s_mixer = 0x3F;		/* reg 7 : all tone/noise off */
static bool   s_init = false;
static uint32 s_volume = 100;		/* percent, opendune.ini : musicvolume=60 */
static uint32 s_drumVolume = 50;	/* percent, opendune.ini : drumvolume=50 */
static uint8  s_noiseTicks[VOICES];	/* drum noise mixed onto a tone voice */

#define DRUM_MIX_TICKS 6		/* 1/10 s of noise on top of a tone */

/* ------------------------------------------------------------------ */
/* Tables                                                               */
/* ------------------------------------------------------------------ */

/* AY period = clock / (16 * f), f = 440 * 2^((note - 69) / 12),
 * built with a running multiply so no maths library is needed. */
static void Midi_BuildPeriods(void)
{
	double f = 8.1757989156;	/* MIDI note 0 */
	int n;
	for (n = 0; n < 128; n++) {
		double p = (double)s_ayClockKHz * 1000.0 / (16.0 * f);
		if (p > 4095.0) p = 4095.0;	/* 12-bit tone period */
		if (p < 1.0) p = 1.0;
		s_period[n] = (uint16)(p + 0.5);
		f *= 1.0594630943592953;	/* one semitone */
	}
}

/* Amplitude 0..127 to AY level : the AY levels are about 3 dB apart,
 * so map on a decibel scale rather than linearly. */
static void Midi_BuildCurve(void)
{
	double thr[16];
	int a, l;

	thr[15] = 127.0;
	for (l = 14; l >= 1; l--) thr[l] = thr[l + 1] * 0.7079457844;	/* -3 dB */
	thr[0] = 0.0;

	for (a = 0; a < 128; a++) {
		int level = 0;
		if (a > 0) {
			for (l = 15; l >= 1; l--) {
				if ((double)a >= thr[l] * 0.84) { level = l; break; }
			}
			if (level == 0) level = 1;
		}
		s_levelCurve[a] = (uint8)level;
	}
}

static uint16 Midi_Level16(int chan, uint8 vel)
{
	uint32 a = ((uint32)vel * s_chanVolume[chan]) / 127;	/* 0..127 */
	a = a * s_volume / 100;
	if (a > 127) a = 127;
	return (uint16)(s_levelCurve[a] << 4);
}

/* ------------------------------------------------------------------ */
/* Voices                                                               */
/* ------------------------------------------------------------------ */
static void Voice_SetVolume(int v, uint8 level)
{
	s_voice[v].level = level;
	AY_Write((uint8)(8 + v), level);
}

static void Voice_Off(int v)
{
	s_noiseTicks[v] = 0;
	Voice_SetVolume(v, 0);
	s_voice[v].chan = -1;
	s_voice[v].drum = false;
	s_mixer |= (uint8)((1 << v) | (8 << v));	/* tone and noise off */
	AY_Write(7, s_mixer);
}

/* Free voice, else the oldest one. Drums may only take a free voice or
 * one already playing a drum (-1 = drop the drum hit). */
static int Voice_Allocate(bool drum)
{
	int v, oldest = -1;
	for (v = 0; v < VOICES; v++) {
		if (s_voice[v].chan < 0) return v;
	}
	for (v = 0; v < VOICES; v++) {
		if (drum && !s_voice[v].drum) continue;
		if (oldest < 0 || s_voice[v].age < s_voice[oldest].age) oldest = v;
	}
	return oldest;
}

/* ------------------------------------------------------------------ */
/* MIDI messages                                                        */
/* ------------------------------------------------------------------ */
/* AY noise period (1..31, higher = lower pitch) for a General MIDI drum note */
static uint8 Midi_DrumNoise(uint8 note)
{
	if (note <= 36) return 28;	/* bass drums */
	if (note <= 41) return 12;	/* snares, low toms, claps */
	if (note <= 48) return 8;	/* toms, closed / pedal hi-hat */
	return 3;			/* open hi-hat, cymbals, others */
}

/* Drum hit : on a free voice it sounds alone at drumvolume. Only when all
 * voices hold notes is it mixed onto the bass voice, whose volume is raised
 * to the drum's level for the burst and restored afterwards. */
static void Midi_Drum(uint8 chan, uint8 note, uint8 vel)
{
	int v, bass = -1;
	uint8 drumLevel;
	uint32 l;

	/* drums go to the sample buffer when digitised sound is on (they are
	 * left out while an effect or voice plays); the AY noise is the
	 * fallback for soundbuffer=0 */
	{
		uint32 a = (uint32)vel * s_chanVolume[chan] / 127;
		a = a * s_volume / 100;
		a = a * s_drumVolume / 100;
		if (a > 127) a = 127;
		if (DSP_Drum(note, (uint8)a)) return;
	}

	l = (uint32)Midi_Level16(chan, vel) * s_drumVolume / 100;
	drumLevel = (uint8)(l >> 4);
	if (drumLevel == 0) return;

	AY_Write(6, Midi_DrumNoise(note));

	v = Voice_Allocate(true);		/* free voice, or one playing a drum */
	if (v >= 0) {
		s_voice[v].chan = (int)chan;
		s_voice[v].note = note;
		s_voice[v].age  = ++s_clock;
		s_voice[v].drum = true;
		s_voice[v].level16 = (uint16)l;
		s_voice[v].decay = 24;
		s_noiseTicks[v] = 0;
		s_mixer |= (uint8)(1 << v);	/* tone off */
		s_mixer &= (uint8)~(8 << v);	/* noise on */
		AY_Write(7, s_mixer);
		Voice_SetVolume(v, drumLevel);
		return;
	}

	/* all voices hold notes : the bass voice plays the drum for the short
	 * burst instead of its tone (tone and noise together on one AY channel
	 * are ANDed and sound distorted), then its note continues */
	for (v = 0; v < VOICES; v++) {
		if (bass < 0 || s_voice[v].note < s_voice[bass].note) bass = v;
	}
	s_mixer |= (uint8)(1 << bass);		/* tone off */
	s_mixer &= (uint8)~(8 << bass);		/* noise on */
	AY_Write(7, s_mixer);
	s_noiseTicks[bass] = DRUM_MIX_TICKS;
	AY_Write((uint8)(8 + bass), drumLevel);
}

static void Midi_NoteOn(uint8 chan, uint8 note, uint8 vel)
{
	bool drum = (chan == DRUM_CHAN);
	int v;
	Voice *vo;

	if (drum) { Midi_Drum(chan, note, vel); return; }

	v = Voice_Allocate(false);
	if (v < 0) return;
	vo = &s_voice[v];
	s_noiseTicks[v] = 0;

	vo->chan = (int)chan;
	vo->note = note;
	vo->age  = ++s_clock;
	vo->drum = false;
	vo->level16 = Midi_Level16(chan, vel);

	{
		uint16 p = s_period[note & 127];
		AY_Write((uint8)(v * 2), (uint8)(p & 0xFF));
		AY_Write((uint8)(v * 2 + 1), (uint8)(p >> 8));
		s_mixer &= (uint8)~(1 << v);		/* tone on */
		s_mixer |= (uint8)(8 << v);		/* noise off */
		vo->decay = 1;				/* slow fade while held */
	}
	AY_Write(7, s_mixer);
	Voice_SetVolume(v, (uint8)(vo->level16 >> 4));
}

static void Midi_NoteOff(uint8 chan, uint8 note)
{
	int v;
	for (v = 0; v < VOICES; v++) {
		if (s_voice[v].chan == (int)chan && s_voice[v].note == note && !s_voice[v].drum) {
			Voice_Off(v);
		}
	}
}

static void Midi_AllNotesOff(int chan)
{
	int v;
	for (v = 0; v < VOICES; v++) {
		if (s_voice[v].chan >= 0 && (chan < 0 || s_voice[v].chan == chan)) Voice_Off(v);
	}
}

/* 60 Hz : decay the voices */
static void Midi_Tick(void)
{
	int v;

	/* the game was busy (loading a screen) and the watchdog silenced the
	 * AY : drop the notes that were hanging, then carry on normally */
	if (ql_midi_muted) {
		Midi_AllNotesOff(-1);
		ql_midi_muted = 0;
	}
	ql_midi_idle = 0;

	for (v = 0; v < VOICES; v++) {
		Voice *vo = &s_voice[v];
		uint8 level;
		if (s_noiseTicks[v] != 0 && --s_noiseTicks[v] == 0 && !vo->drum) {
			s_mixer |= (uint8)(8 << v);		/* drum burst over : noise off */
			if (vo->chan >= 0) s_mixer &= (uint8)~(1 << v);	/* tone back on */
			AY_Write(7, s_mixer);
			AY_Write((uint8)(8 + v), vo->chan >= 0 ? vo->level : 0);
		}
		if (vo->chan < 0) continue;
		vo->level16 = (vo->level16 > vo->decay) ? (uint16)(vo->level16 - vo->decay) : 0;
		level = (uint8)(vo->level16 >> 4);
		if (level == 0) {
			Voice_Off(v);
		} else if (level != vo->level) {
			if (s_noiseTicks[v] != 0) vo->level = level;	/* burst running : write at its end */
			else Voice_SetVolume(v, level);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Watchdog                                                             */
/* ------------------------------------------------------------------ */
static bool s_watchInstalled = false;

/* Also registered with atexit() : a polled-interrupt server left in the
 * QDOS list after the program has gone would crash the machine. */
static void Midi_WatchRemove(void)
{
	if (!s_watchInstalled) return;
	QL_MidiWatch_Remove();
	s_watchInstalled = false;
}

/* ------------------------------------------------------------------ */
/* Driver interface (see midi.h)                                        */
/* ------------------------------------------------------------------ */
bool midi_init(void)
{
	int i;

	/* AY music is off unless opendune.ini has music=1 */
	if (IniFile_GetInteger("music", 0) == 0) return true;

	s_ayClockKHz = (*QL_CARD_ID == QLION_ID) ? 2750UL : 2500UL;
	Midi_BuildPeriods();
	Midi_BuildCurve();
	s_volume = (uint32)IniFile_GetInteger("musicvolume", 60);
	if (s_volume > 400) s_volume = 400;
	s_drumVolume = (uint32)IniFile_GetInteger("drumvolume", 50);
	if (s_drumVolume > 400) s_drumVolume = 400;
	for (i = 0; i < 16; i++) s_chanVolume[i] = 100;
	for (i = 0; i < VOICES; i++) { s_voice[i].chan = -1; s_voice[i].drum = false; s_noiseTicks[i] = 0; }
	s_mixer = 0x3F;
	AY_Write(7, s_mixer);
	for (i = 0; i < VOICES; i++) AY_Write((uint8)(8 + i), 0);
	Timer_Add(Midi_Tick, 1000000 / 60, false);

	ql_midi_idle = 0;
	ql_midi_muted = 0;
	QL_MidiWatch_Install();
	s_watchInstalled = true;
	atexit(Midi_WatchRemove);

	s_init = true;
	return true;
}

void midi_uninit(void)
{
	if (!s_init) return;
	Midi_AllNotesOff(-1);
	Timer_Remove(Midi_Tick);
	Midi_WatchRemove();
	s_init = false;
}

void midi_reset(void)
{
	int i;
	Midi_AllNotesOff(-1);
	for (i = 0; i < 16; i++) s_chanVolume[i] = 100;
}

/** data = status | (data1 << 8) | (data2 << 16) */
void midi_send(uint32 data)
{
	uint8 status = (uint8)(data & 0xFF);
	uint8 d1 = (uint8)((data >> 8) & 0x7F);
	uint8 d2 = (uint8)((data >> 16) & 0x7F);
	uint8 chan = (uint8)(status & 0x0F);

	if (!s_init) return;

	switch (status & 0xF0) {
		case 0x90:
			if (d2 != 0) { Midi_NoteOn(chan, d1, d2); break; }
			/* velocity 0 = note off */
			/* fall through */
		case 0x80:
			Midi_NoteOff(chan, d1);
			break;
		case 0xB0:
			if (d1 == 7) s_chanVolume[chan] = d2;				/* volume */
			else if (d1 == 120 || d1 == 123) Midi_AllNotesOff(chan);	/* sound / notes off */
			break;
		default:
			break;	/* program change, pitch bend etc. : ignored */
	}
}

/** SysEx (MT-32 setup) : not used on the AY */
uint16 midi_send_string(const uint8 *data, uint16 len)
{
	(void)data;
	return len;
}
