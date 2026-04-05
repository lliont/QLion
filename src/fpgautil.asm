* Liontakis Theodoulos 2022

    section fpgautil
	
	include 'dev8_keys_sbasic'
	include 'dev8_keys_sys'
    include dev8_keys_qlv
    include dev8_keys_err

	
cmbit0 equ	$0055
cmbit1 equ	$00AA
cmbit3 equ	$5500
cmbit2 equ	$AA00
	
start
        move.w  $110,a2
        lea     pdef(pc),a1
        jsr     (a2)
        moveq   #0,d0
        rts
		
pdef    dc.w    12
;        dc.w    colsb-*
;        dc.b    6,'COLSUB'
        dc.w    plot-*
        dc.b    5,'FPLOT'
        dc.w    clrsp-*
        dc.b    5,'CLRSP'
        dc.w    setsp-*
        dc.b    5,'SETSP'
		dc.w    ffast-*
		dc.b    5,'FFAST'
		dc.w    fslow-*
		dc.b    5,'FSLOW'
		dc.w    fcon-*
		dc.b    4,'FCON'
		dc.w    fcoff-*
		dc.b    5,'FCOFF'
		dc.w    fsound-*
		dc.b    6,'FSOUND'
		dc.w    fsettim-*
		dc.b    7,'FSETTIM'
		dc.w    palet8-*
		dc.b    6,'FPALET'
		dc.w    ink16-*
		dc.b    4,'FINK'
		dc.w    fpaper-*
		dc.b    6,'FPAPER'
        dc.w    0
        dc.w    2
		dc.w    ftimer-*
		dc.b    6,'FTIMER'
		dc.w    fplay-*
		dc.b    5,'FPLAY'
        dc.w    0

bi_gtchn
         moveq    #1,d7             default channel no.
bi_gtall
         movem.l  a3/a5,-(sp)       save A3/A5
         moveq    #0,d0
         moveq    #0,d6             if default, no stack correction
         cmp.l    a3,a5             no parameter?
         beq.s    get_defc          yes, so use default
         move.l   a3,a5             let us check whether the ...
         addq.l   #8,a5             ... first parameter is preceeded ...
         btst     #7,1(a6,a3.l)     ... by a hash
         beq.s    get_defc          no, also use default
         move.w   sb.gtint,a2       yes, get integer value
         jsr      (a2)
         tst.l    d0                error, no integer?
         bne.s    get_erbp          yes, return error
         moveq    #0,d7
         moveq    #8,d6             we can correct the A3 pointer
         move.w   0(a6,a1.l),d7     get channel number
get_defc mulu     #$28,d7           calculate channel ID now
         move.l   $30(a6),a2
         add.l    d7,a2
         cmp.l    $34(a6),a2
         bhi.s    get_erno
         beq.s    get_erno
         tst.b    (a6,a2.l)             -ve?
         bmi.s    get_erno
         move.l   0(a6,a2.l),a0
         movem.l  (sp)+,a3/a5
         add.l    d6,a3
         rts
*
get_erno moveq    #err.ichn,d0
         bra.s    get_errt
get_erbp moveq    #err.ipar,d0
get_errt addq.l   #8,sp
         rts

fpaper  jsr     bi_gtchn
		or      d0,d0
		bne.s   paexit
		move.l  a0,d5
		move.w  $112,a2
		jsr     (a2)
		subi.w  #1,d3
		bne     err1(pc)
		move.w  0(a6,a1.l),d4
		;move.l $30(a6),a4         ; basic channel table in a4
		;move.l $28(a6,a4.l),d5    ; get basic channel #1 id 
		move.l  #0,d0
		trap    #1                ; get system variables address
		move.l  $78(a0),a2        ; channel table
		move.l  $7C(a0),a3
		;move.l  d5,d6
		lsr.l   #8,d5
		lsr.l   #8,d5
		;and.l   #$0000FFFF,d5
		jsr     schn
		bne.s   paexit   
		move.b  d4,$46(a3)
		move.b  d4,$45(a3)
		moveq	#0,d5
		btst    #0,d4
		beq.s	pap1
		or.w	#cmbit0,d5
pap1    btst    #1,d4
		beq.s	pap2
		or.w	#cmbit1,d5
pap2    btst    #2,d4
		beq.s	pap3
		or.w	#cmbit2,d5
pap3    btst    #3,d4
		beq.s	pap4
		or.w	#cmbit3,d5
pap4
		move.w  d5,$38(a3)   ;-- paper
		move.w  d5,$36(a3)   ;-- paper
		move.l  $36(a3),$3A(a3) ;-- paper to stripe
		moveq.l	#0,d0
paexit	rts	
		
schn   	move.l (a2),a4
		cmp.w  $10(a4),d5
		beq.s  fchan
		add.l  #4,a2
		cmp.l  a2,a3
		bcc.s  schn
		moveq  #err.ichn,d0
		rts
fchan   move.l  a4,a3
		moveq.l   #0,d0
		rts
		
ink16
		jsr     bi_gtchn
		or      d0,d0
		bne.s	paexit
		move.l  a0,d5
		move.w  $112,a2
		jsr     (a2)
		subi.w  #1,d3
		bne     err1(pc)
		move.w  0(a6,a1.l),d4
		move.l  #0,d0
		trap    #1                ; get system variables address
		move.l  $78(a0),a2        ; channel table base
		move.l  $7C(a0),a3
		;move.l  d5,d6
		lsr.l   #8,d5
		lsr.l   #8,d5
		;and.l   #$0000FFFF,d5
		jsr     schn
		bne.s   paexit
		;lsl.w   #2,d5
		;move.l  0(a2,d5.w),a3     ;
		move.b  d4,$44(a3)
		moveq	#0,d5
		btst    #0,d4
		beq.s	ink1
		or.w	#cmbit0,d5
ink1    btst    #1,d4
		beq.s	ink2
		or.w	#cmbit1,d5
ink2    btst    #2,d4
		beq.s 	ink3
		or.w	#cmbit2,d5
ink3    btst    #3,d4
		beq.s	ink4
		or.w	#cmbit3,d5
ink4
		move.w  d5,$3E(a3)   ;-- ink
		move.w  d5,$40(a3)   ;-- ink
		;move.l  $36(a3),$3A(a3) ;-- paper to stripe
		moveq.l	#0,d0
		rts
palet8
		move.w  $112,a2
        jsr     (a2)
        subi.w  #1,d3
        bne     err1(pc)
		move.w  0(a6,a1.l),d0  ; p
        and.b   #7,d0
		move.b  (4190208),d1
		and.b    #$F1,d1
		lsl.b	#1,d0
		or.b     d0,d1 
		move.b   d1,(4190208)
		bra      exit(pc)
fplay   move.l  $58(a6),a1
		subq.l	#2,a1
		move.l  a1,$58(a6)
		moveq   #0,d1
		move.b  4190228,d1
		move.w  d1,(a6,a1.l)
		moveq   #3,d4
		moveq	#0,d0
        rts
fsettim move.w  $112,a2
        jsr     (a2)
        subi.w  #1,d3
        bne     err1(pc)
		move.w  0(a6,a1.l),d0
		move.w  d0,4190212
		moveq	#0,d0
        rts
ftimer  move.l  $58(a6),a1
		subq.l	#2,a1
		move.l  a1,$58(a6)
		move.w  4190222,d1
		move.w  d1,(a6,a1.l)
		moveq   #3,d4
		moveq	#0,d0
        rts
fsound
		move.w  $112,a2
        jsr     (a2)
        subi.w  #4,d3
        bne     err1(pc)
		move.w  0(a6,a1.l),d0  ; freq
        and.l   #$FFF,d0
        move.w  2(a6,a1.l),d1  ; duration
        and.l   #$F,d1
		move.w  4(a6,a1.l),d2  ; volume
        and.l   #$FF,d2
		move.w  6(a6,a1.l),d3  ; harmonic
        and.l   #7,d3
		move.l  #100000,d4		
		divu    d0,d4          ; fbase/f
		and.l   #$0000FFFF,d4
		sub.w   #1,d4
		lsl.w   #8,d1
		lsl.w   #5,d1
		or.w    d1,d4
		move.w  d4,(4190224)
		move.b  d2,(4190226)
		move.b  d3,(4190227)
		move.b  #0,(4190228)
		move.b  #1,(4190228)
		moveq   #0,d0
        rts
fcon 
		move.b  (4190208),d1
		or.b     #1,d1 
		move.b   d1,(4190208)
		bra      exit(pc) 
fcoff 
		move.b  (4190208),d1
		and.b    #$FE,d1 
		move.b   d1,(4190208)
		bra      exit(pc)
fslow 
		move.b  (4190209),d1
		or.b     #1,d1 
		move.b   d1,(4190209)
		bra      exit(pc) 
ffast  
		move.b  (4190209),d1
		and.b    #$FE,d1 
		move.b   d1,(4190209)
		bra      exit(pc)
setsp   move.w  $112,a2
        jsr     (a2)
        subi.w  #4,d3
        bne     err1(pc)
		move.w  0(a6,a1.l),d0  ; s
        and.l   #63,d0
        move.w  2(a6,a1.l),d1  ; en
        and.l   #1,d1
        move.w  4(a6,a1.l),d2  ; x
        and.l   #$FF,d2
        move.w  6(a6,a1.l),d3  ; y
		and.l   #$FF,d3
		move.l  #$3fc000,a2
		move.l  d0,d4
		divu    #30,d4   ; bank
		move.l  d4,d5
		mulu    #30,d5
		sub.l   d5,d0   ; ss
		mulu    #4096,d4
		lsl.w   #2,d0
		add.l   d4,a2
		add.l   d0,a2
		move.b  d1,(a2)
		move.b  d2,1(a2)
		move.b  d3,2(a2)
		bra     exit(pc)
clrsp   move.l  #$3fc000,a1
        move.l  #63,d3
lp2     move.l  #0,(a1)+
        dbra    d3,lp2(pc) 
		move.l  #$3fc000+4096,a1
        move.l  #63,d3
		bra.s   lp1
cls64   move.l  #$1C000,a1
        move.l  #4095,d3
lp1     move.l  #0,(a1)+
        dbra    d3,lp1(pc)        
        bra     exit
plot    move.w  $112,a2
        jsr     (a2)
        subi.w  #3,d3
        bne     err1(pc)
        moveq   #0,d1
        moveq   #0,d2
        move.w  0(a6,a1.l),d1  * x
		and.w   #$1FF,d1
        move.w  2(a6,a1.l),d2  * y
		and.w   #$FF,d2
        move.w  4(a6,a1.l),d3  * color
		and.w   #$F,d3
        move.l  #$27F80,a2
        move.l  d1,d4
        and.l   #3,d4
        and.l   #$FC,d1
        lsl.w   #7,d2
        sub.l   d2,a2
        lsr.w   #1,d1
        add.l   d1,a2
        move.w  d3,d5
        and.l   #3,d3
        and.l   #12,d5
        btst    #2,d5
        beq.s   skp1
        bclr    #2,d5
        bset    #4,d5
skp1    lsl.w   #3,d5
        lsl.w   #6,d3
        lsl.w   #1,d4
        lsr.w   d4,d3
        lsr.w   d4,d5
        move.b  (a2),d1
        move.b  1(a2),d2
        move.w  #6,d0
        sub.w   d4,d0
        move.w  d0,d4
        bclr    d4,d1
        bclr    d4,d2
        addq    #1,d4
        bclr    d4,d1
        bclr    d4,d2
        or.b    d5,d1
        or.b    d3,d2
        move.b  d1,(a2)
        move.b  d2,1(a2)
        moveq   #0,d0
        rts
colsb   move.w  $112,a2
        jsr     (a2)
        subi.w  #5,d3
        bne     err1(pc)
        moveq   #0,d1
        moveq   #0,d2
        moveq   #0,d3
        moveq   #0,d4
        moveq   #0,d5
        moveq   #0,d6
        move.l  #$28000,a2
        move.l  a2,a3
        sub.l   #128,a2
        move.w  0(a6,a1.l),d1
        and.w   #$FFFC,d1
        lsr.l   #1,d1
        add.l   d1,a2
        move.w  2(a6,a1.l),d2
        lsl.w   #7,d2
        sub.l   d2,a2
        move.w  4(a6,a1.l),d3
        and.w   #$FFFC,d3
        lsr.w   #1,d3
*        add.l   d3,a3   
        move.w  6(a6,a1.l),d4
        lsl.w   #7,d4
        sub.l   d4,a3
        move.w  8(a6,a1.l),d4
        and.w   #7,d4
        move.w  d1,d0
lop1    move.b  0(a2),d5
        move.b  1(a2),d6
        and.b   #$80,d5
        and.b   #$C0,d6
        lsr.b   #5,d5
        lsr.b   #6,d6
        or.b    d5,d6
        cmp.b   d4,d6
        bne.s     pix2
        bset.b  #6,0(a2)        
pix2    move.b  (a2),d5         
        move.b  1(a2),d6
        and.b   #$20,d5
        and.b   #$30,d6
        lsr.b   #3,d5
        lsr.b   #4,d6
        or.b    d5,d6
        cmp.b   d4,d6
        bne.s   pix3
        bset.b  #4,0(a2)
pix3    move.b  (a2),d5
        move.b  1(a2),d6
        and.b   #$8,d5
        and.b   #$c,d6
        lsr.b   #1,d5
        lsr.b   #2,d6
        or.b    d5,d6
        cmp.b   d4,d6
        bne.s   pix4
        bset.b  #2,0(a2)
pix4    move.b  (a2),d5
        move.b  1(a2),d6
        and.b   #2,d5
        and.b   #3,d6
        lsl.b   #1,d5
        or.b    d5,d6
        cmp.b   d4,d6
        bne.s   lext
        bset    #0,0(a2)
lext    
        add.w   #2,d0
        cmp.w   d0,d3
        bgt.s   nendx
        sub.l   #128,a2
        add.l   d1,a2
        sub.l   d3,a2
        move.w  d1,d0
nendx   add.l   #2,a2
contl   cmp.l   a2,a3
        ble     lop1(pc)
exit    moveq   #0,d0
        rts
err1    moveq   #-15,d0
        rts
        end


