* ql_mouse.s - QIMI-style mouse step counter for OpenDUNE on the QLion card
*
* Installs a handler in the QDOS external interrupt list (MT.LXINT).
* Every mouse interrupt reports one step per axis in the movement
* register $1BFBC (bits: 00 yd xi 0 xd 0 yi; xd/yd = step pending,
* xi/yi = direction : xi 1 = right, yi 1 = up).
* The handler adds the step to _mouse_x / _mouse_y, acknowledges the mouse by accessing $1BFBE
* and clears the EXTINT bit in the ZX8302.
* C reads the two counters in QL_Pointer_Read().
*
* Do not run the Pointer Environment's own QIMI driver at the same time,
* or the two handlers will share (and lose) the steps.

	.text

	.globl	_QL_Mouse_Install
	.globl	_QL_Mouse_Remove
	.globl	_mouse_x
	.globl	_mouse_y

MT_LXINT	equ	$1A
MT_RXINT	equ	$1B
MOUSE_MOVE	equ	$1BFBC
MOUSE_ACK	equ	$1BFBE		; any access acknowledges (irq_ack : A5=1, A1=1)
PC_INTR		equ	$18021
SV_PCINT	equ	$35		; offset of the interrupt mask in sys vars
EXT_INT		equ	$10		; ZX8302 bit 4 : external interrupt

* void QL_Mouse_Install(void)
_QL_Mouse_Install:
	movem.l	d0-d3/a0-a3,-(sp)
	lea	mlink,a0
	moveq	#MT_LXINT,d0
	trap	#1
	movem.l	(sp)+,d0-d3/a0-a3
	rts

* void QL_Mouse_Remove(void)
_QL_Mouse_Remove:
	movem.l	d0-d3/a0-a3,-(sp)
	lea	mlink,a0
	moveq	#MT_RXINT,d0
	trap	#1
	movem.l	(sp)+,d0-d3/a0-a3
	rts

* External interrupt server. Called by QDOS with A6 = system variables.
mouse_int:
	movem.l	d0-d1,-(sp)
	move.b	MOUSE_MOVE,d0		; 00 yd xi 0 xd 0 yi
	move.b	d0,d1
	andi.b	#$24,d1			; xd / yd : a step pending on X or Y ?
	beq	mi_done			; no : not our interrupt, leave it

	btst	#2,d0			; xd : X step pending (mdx /= 0)
	beq	mi_y
	btst	#4,d0			; xi : X direction, 1 = right
	beq	mi_xl
	addq.w	#1,_mouse_x
	bra	mi_y
mi_xl:	subq.w	#1,_mouse_x

mi_y:	btst	#5,d0			; yd : Y step pending (mdy /= 0)
	beq	mi_4
	btst	#0,d0			; yi : Y direction, 1 = up
	beq	mi_yd
	subq.w	#1,_mouse_y		; up : screen Y decreases
	bra	mi_4
mi_yd:	addq.w	#1,_mouse_y		; down : screen Y increases

mi_4:	tst.b	MOUSE_ACK		; acknowledge : core drains this step
	move.b	SV_PCINT(a6),d1		; current enable mask
	ori.b	#EXT_INT,d1		; + clear the external interrupt
	move.b	d1,PC_INTR
mi_done:
	movem.l	(sp)+,d0-d1
	rts

	.data
	.even
mlink:	dc.l	0			; link, filled in by QDOS
	dc.l	mouse_int		; server address

_mouse_x:	dc.w	0
_mouse_y:	dc.w	0
