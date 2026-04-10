; Serial - ESP driver Theodoulos Liontakis  2025

ESP_INOUT EQU       $800025
ESP_STAT  EQU       $800024
ESP_RD    EQU       0
ESP_DR    EQU       1
ESP_R     EQU       1
ESP_W     EQU       2


MT.ALCHP  EQU  $18
MT.LIOD   EQU  $20
MM.ALCHP  EQU  $C0
MM.RECHP  EQU  $C2
ERR.NC    EQU  -1
ERR.BP    EQU  -15
ERR.NF    EQU  -7    
IO.SERIO  EQU  $EA
IO.NAME   EQU  $122  
IO.SSTRG  EQU  $7

	section serdrv

INIT
     movem.l A0/A3,-(A7)
     moveq   #MT.ALCHP,D0   ; allocate linkage block
     moveq   #$36,D1
     moveq   #0,D2           ; owner job 0
     trap    #1
	 tst.l   D0
	 bne.s   INIT_EXIT
	 
	 lea     $1C(A0),A3   ; fill linakege block
	 lea     IO(PC),A2     ; 1C
	 move.l  a2,(a3)+
	 lea     open(PC),a2   
	 move.l  a2,(a3)+      ; 20
	 lea     close(PC),a2
	 move.l  a2,(a3)+      ; 24
	 lea     pendi(PC),a2
	 move.l  a2,(a3)+      ; 28
	 lea     fetch(PC),a2
	 move.l  a2,(a3)+      ; 2C
     lea     send(PC),a2
	 move.l  a2,(a3)+      ; 30
	 move.w  #$4E75,(a3)+   ; RTS at $34
	 
	 lea     $18(A0),A0
	 moveq   #MT.LIOD,D0
	 trap    #1
	 
INIT_EXIT
	movem.l  (A7)+,A0/A3
	rts
	
IO  cmp.b    #IO.SSTRG,D0  ; block file operations
    bhi.s    ERR_BP
	pea      $28(A3)      ; as if called from there
	move.w   IO.SERIO,A4
	jmp      (A4)

ERR_NC
	moveq    #ERR.NC,D0
	rts
	
ERR_BP
	moveq    #ERR.BP,D0
	rts
	
ispending	ds.b 1
byteread    ds.b 1
	
open:
       move.w   IO.NAME,A4
	   jsr      (a4)
	   bra.s    tesp
	   bra.s    tesp
	   bra.s    alchp
	   dc.w		4,'SER3'
	   dc.w     0
tesp  
       move.w   IO.NAME,A4
	   jsr      (a4)
	   bra.s    notfnd
	   bra.s    notfnd
	   bra.s    alchp
	   dc.w		4,'ESP1'
	   dc.w     0
alchp	   
	   bsr.s   fetch           ; make sure receive fifo is empty
       beq.s   alchp
       moveq   #0,d0           ; signal OK
	   move.b  d0,ispending

	   moveq    #$18,d1
       move.w   MM.ALCHP,a4
	   JMP      (a4)
	   
notfnd moveq   #ERR.NF,d0
        rts
      
          
close   move.w  MM.RECHP,a2
        jmp     (a2)
          
pendi   tst.b   ispending    
        bne.s   pending        
        moveq   #ERR.NC,d0      
        btst.b  #ESP_DR,ESP_STAT    
        beq.s   pexit        
        move.b  #1,ispending   
		move.b  ESP_INOUT,d1
		move.b  #ESP_R,ESP_STAT
		nop
		move.b  #0,ESP_STAT
        move.b  d1,byteread 
        moveq   #0,d0          
pexit   rts

pending move.b  byteread,d1  
        move.b  #0,d0           
        rts

fetch   bsr     pendi           
        bne.s   fexit     
        move.b  #0,ispending    
fexit:
        tst.l   d0
        rts                     

send    btst.b  #ESP_RD,ESP_STAT      
        beq.s   send_nc         
		move.b  d1,ESP_INOUT
 		move.b  #ESP_W,ESP_STAT
		nop	
		move.b  #0,ESP_STAT
        moveq   #0,d0           
        rts
send_nc moveq   #ERR.NC,d0     
        rts
		
		
		end
	