* ql_midiwatch.s - music watchdog for OpenDUNE on the QLion card
*
* The music (midi_ql.c) is driven by the game's timer, which stops while
* the game loads or builds a new screen. Notes sounding at that moment
* would then hang at a constant volume. This QDOS polled-interrupt server
* runs 50 times a second regardless: if midi_ql.c has not ticked for
* WATCH_FRAMES frames, it sets the three AY volumes to 0 and flags it,
* so the next music tick can clear its voices cleanly.
*
* It never writes the AY while midi_ql.c is in the middle of an AY write
* (_ql_ay_busy), so the two cannot interleave on the AY bus.

	.text

	.globl	_QL_MidiWatch_Install
	.globl	_QL_MidiWatch_Remove
	.globl	_ql_midi_idle
	.globl	_ql_midi_muted
	.globl	_ql_ay_busy

MT_LPOLL	equ	$1C
MT_RPOLL	equ	$1D
AY_DATA		equ	$800028
AY_CTRL		equ	$80002A
WATCH_FRAMES	equ	15		; 15 frames = 0.3 s without a music tick

* void QL_MidiWatch_Install(void)
_QL_MidiWatch_Install:
	movem.l	d0-d3/a0-a3,-(sp)
	lea	wlink,a0
	moveq	#MT_LPOLL,d0
	trap	#1
	movem.l	(sp)+,d0-d3/a0-a3
	rts

* void QL_MidiWatch_Remove(void)
_QL_MidiWatch_Remove:
	movem.l	d0-d3/a0-a3,-(sp)
	lea	wlink,a0
	moveq	#MT_RPOLL,d0
	trap	#1
	movem.l	(sp)+,d0-d3/a0-a3
	rts

* Polled server, called every frame interrupt.
watch_int:
	movem.l	d0-d1,-(sp)
	tst.b	_ql_midi_muted		; already silenced ?
	bne	wi_done
	addq.w	#1,_ql_midi_idle
	cmpi.w	#WATCH_FRAMES,_ql_midi_idle
	blt	wi_done
	tst.b	_ql_ay_busy		; main program inside an AY write ?
	bne	wi_done

	moveq	#8,d0			; AY registers 8, 9, 10 : volume A, B, C
wi_loop:
	move.b	d0,AY_DATA		; latch register
	move.b	#5,AY_CTRL
	move.b	#5,AY_CTRL
	move.b	#0,AY_CTRL
	move.b	#0,AY_DATA		; volume 0
	move.b	#4,AY_CTRL
	move.b	#4,AY_CTRL
	move.b	#0,AY_CTRL
	addq.b	#1,d0
	cmpi.b	#11,d0
	blt	wi_loop

	move.b	#1,_ql_midi_muted
wi_done:
	movem.l	(sp)+,d0-d1
	rts

	.data
	.even
wlink:	dc.l	0			; link, filled in by QDOS
	dc.l	watch_int		; server address

_ql_midi_idle:	dc.w	0
_ql_midi_muted:	dc.b	0
_ql_ay_busy:	dc.b	0
	.even
