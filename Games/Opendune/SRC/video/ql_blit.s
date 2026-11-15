* ql_blit.s - fast 8bpp -> RRRGGGBB screen copy for OpenDUNE on the QLion card
*
* void QL_BlitRectAsm(const uint8 *src, uint8 *dst, int width, int height,
*                     const uint8 *lut, const uint16 *lut_hi);
*
*   src     game buffer, 320 bytes per line
*   dst     QLion VRAM, 512 bytes per line
*   width   pixels per line, multiple of 16, > 0
*   height  lines, > 0
*   lut     256 bytes   : palette index -> RRRGGGBB
*   lut_hi  256 words   : palette index -> RRRGGGBB << 8
*
* Two pixels per word write; lut_hi avoids a slow 8-bit shift per pair.
* The inner loop is unrolled to 16 pixels.

	.text
	.globl	_QL_BlitRectAsm

_QL_BlitRectAsm:
	movem.l	d2-d7/a2-a3,-(sp)	; 8 registers = 32 bytes
	movea.l	36(sp),a0		; src
	movea.l	40(sp),a1		; dst
	move.l	44(sp),d4		; width
	move.l	48(sp),d3		; height
	movea.l	52(sp),a2		; lut
	movea.l	56(sp),a3		; lut_hi
	tst.l	d4
	ble	bl_end
	tst.l	d3
	ble	bl_end

	move.l	#320,d5
	sub.l	d4,d5			; source skip at end of line
	move.l	#512,d6
	sub.l	d4,d6			; destination skip at end of line
	lsr.l	#4,d4			; groups of 16 pixels
	subq.l	#1,d4			; for dbra
	subq.l	#1,d3			; for dbra
	moveq	#0,d2			; upper bytes of d2 stay 0 (byte index)

bl_row:
	move.w	d4,d7
bl_grp:
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	moveq	#0,d1
	move.b	(a0)+,d1		; p0
	add.w	d1,d1
	move.w	0(a3,d1.w),d0		; lut[p0] << 8
	move.b	(a0)+,d2		; p1
	move.b	0(a2,d2.w),d0		; | lut[p1]
	move.w	d0,(a1)+
	dbra	d7,bl_grp

	adda.l	d5,a0
	adda.l	d6,a1
	dbra	d3,bl_row

bl_end:
	movem.l	(sp)+,d2-d7/a2-a3
	rts
