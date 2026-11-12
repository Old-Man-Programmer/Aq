; Joystick test: print reg. value, dial direction, button states
; Technically not a sound test, but it does use the sound chip.
; See chkport for a way to convert port values to button mask + dir

.area	code1	(abs)
.org	32000

; IMPORTANT:  If changes are made to this, recheck all commented out
; "inc hl" instructions, or more precisely the #<... before it to ensure
; that the inc is never necessary

.include "aquarius.mac"

start:
	AYREG	7,0	; just to be sure: DDRA = DDRB = 0 = in
	ld	a,#11
	PRNTCHR
	ld	hl,#SCREEN + 41
	ld	(hl),#' '	; erase cursor
	ld	hl,#SCREEN + 40 + 5
	ld	(hl),#'A
	ld	hl,#SCREEN + 40 + 10
	ld	(hl),#'B
loop:
	ld	hl,#SCREEN + 40 * 2 + 5
	ld	a,#14
	out	(AYREG),a
	in	a,(AYDAT)
	ld	d,a		; d is joy A/1/left
	call	puthex
	inc	hl
	inc	hl
	inc	hl
	ld	a,#15
	out	(AYREG),a
	in	a,(AYDAT)
	ld	e,a		; e is joy B/2/right
	call	puthex

	ld	hl,#SCREEN + 40 * 3 + 5
	ld	a,d
	call	prport
	ld	hl,#SCREEN + 40 * 3 + 10
	ld	a,e
	call	prport
	jr	loop

dirs:
	.ascii	'E  ESESE SSES  SSWSW WSWW  WNWNW NNWN  NNENE ENE   '

prport:
	push	hl
	exx
	call	chkport
	; print dir in b
	ld	hl,#40 * 3
	pop	de
	add	hl,de
	push	de
	ex	de,hl
	ld	h,#>dirs
	ld	a,b
	add	b
	add	b
	add	a,#<dirs
	ld	l,a
;	jr	nc,1$
;	inc	h
;1$:
	ld	a,c	; ldi trashes everything but a
	ldi
	ldi
	ldi
	ld	c,a
	; print buttons in c
	pop	de
	ld	b,#6
2$:
	rrc	c
	ld	a,#' '
	jr	nc,3$
	ld	a,#6+'1
	sub	b
3$:
	ld	(de),a
	inc	de
	ld	a,b
	cp	#4
	jr	nz,4$
	; move to next line
	ld	hl,#40-3
	add	hl,de
	ex	de,hl
4$:
	djnz	2$
	exx
	ret

chkport:	; b=dir, c=buttons (from a=port val); whacks de,hl,a
	ld	d,a
	cpl	; change active low to active high
	ld	c,#0	; no buttons yet
	and	#0xe0
	jr	z,butdone
	; bit 6 always indicates button 1
	bit	6,a	; faster/smaller than  and#0x40+ld a,b
	jr	z,1$
	inc	c	; currently 0, so set bit 0
1$:
	; bit 5 indicates either 3 (b7=0) or 4 (b7=1)
	bit	5,a
	jr	z,butdone
	; no bit 7 always means 4
	or	a
	jp	p,3$
	set	2,c
	jr	butdone
3$:
	; it's possible both are pressed, but that won't be considered.
	; it's possible 3 and one of 2,5,6 are pressed
	; it's possible 4 and one of 2,5,6 are pressed
	; in any case, always choose 3, even though 4 is possible
	set	3,c
butdone:
	ld	a,d
	; Use old way if no buttons pressed: scan for exact mask match
	or	#0x60	; ignore button 1 & button 4; set sign to high bit
	jp	p,maybutton
	ld	b,#16
	ld	hl,#dialmask
1$:
	cp	(hl)
	jr	z,2$
	inc	hl
	djnz	1$
2$:
	ld	a,#16
	sub	b
	ld	b,a
	ret
	
maybutton:
	; bit 7 indicates 2, 5, 6, and/or 3.
	; if it's possible to 3, 3 was set above.  So now just
	; consider 2, 5, 6.  They interfere with the dial.  See table below.
	and	#31
	add	#<dialval
	ld	l,a
	ld	h,#>dialval
;	jr	nc,4$
;	inc	h
;4$:
	ld	b,(hl)		; b now contains direction (16=none)
	; if bit 7 not set, or lower 3 all clear, we're done, even if dir invalid
	ld	a,d
	cpl
	and	#7
	ret	z
	; othewise, filter out dir mask
	ld	d,a
	ld	h,#>dialmask
	ld	a,#<dialmask
	add	b
	ld	l,a
;	jr	nc,5$
;	inc	h
;5$:
	; only set bits in d that are inactive in dir
	ld	a,(hl)
	and	d
	; now add any bits still set
	rrc	a
	jr	nc,6$
	set	5,c
6$:
	rrc	a
	jr	nc,7$
	set	4,c
7$:
	rrc	a
	ret	nc
	set	1,c
	ret

dialmask: ; given the dir, what is the mask value?
	.db	0xfd, 0xed, 0xec, 0xfc, 0xfe, 0xee, 0xe6, 0xf6
	.db	0xf7, 0xe7, 0xe3, 0xf3, 0xfb, 0xeb, 0xe9, 0xf9
	.db	0xff

; b2: 011 11 011
; b5: 011 11 101
; b6: 011 11 110
dialval: ; given the 32-bit lower half, what is the dir?
	.db	10, 10, 6, 10, 6, 9, 6, 16
; da: 111 00 011
; d6: 111 00 110
; d9: 111 00 111
   ; 0xx 00 000 -> *da+b5+b6 d6+b2+b5
   ; 0xx 00 001 -> *da+b5
   ; 0xx 00 010 -> *d6+b2 da+b6
   ; 0xx 00 011 -> *da[+b2] d9+b2
   ; 0xx 00 100 -> *d6+b5
   ; 0xx 00 101 -> *d9+b5
   ; 0xx 00 110 -> *d6+b6 d9+b6
   ; 0xx 00 111 -> impossible
	.db	2,14, 5, 13, 2, 1, 5, 16
; e: 111 01 001
; d: 111 01 011
; 2: 111 01 100
; 1: 111 01 101
; 5: 111 01 110
   ; 0xx 01 000 -> *d2+b2 de+b6
   ; 0xx 01 001 -> *de[+b2] de[+b5] dd+b5 d1+b2
   ; 0xx 01 010 -> *d5+b2 dd+b6
   ; 0xx 01 011 -> *dd[+b2]
   ; 0xx 01 100 -> *d2[+b5] d2[+b6] d1+b6 d5+b5
   ; 0xx 01 101 -> *d1[+b5]
   ; 0xx 01 110 -> *d5[+b6]
   ; 0xx 01 111 -> impossible
	.db	11, 11, 7, 11, 7, 8, 7, 8
; b: 111 10 011
; 7: 111 10 110
; 8: 111 10 111
   ; 0xx 10 000 -> *db+b5+b6 d7+b2+b5
   ; 0xx 10 001 -> *db+b5
   ; 0xx 10 010 -> *d7+b2 db+b6
   ; 0xx 10 011 -> *db[+b2]
   ; 0xx 10 100 -> *d7+b5
   ; 0xx 10 101 -> *d8+b5
   ; 0xx 10 110 -> *d7[+b6] d8+b6
   ; 0xx 10 111 -> d8 (only if b3)
	.db	3, 15, 4, 16, 3, 16, 16, 16
; f: 111 11 001
; c: 111 11 011
; 3: 111 11 100
; 0: 111 11 101
; 4: 111 11 110
   ; 0xx 11 000 -> *d3+b2 df+b6
   ; 0xx 11 001 -> *df[+b2] df[+b5] dc+b5 d0+b2
   ; 0xx 11 010 -> *d4+b2 dc+b6
   ; 0xx 11 011 -> *b2
   ; 0xx 11 100 -> *d3[+b5] d3[+b6] d0+b6 d4+b5
   ; 0xx 11 101 -> *b5
   ; 0xx 11 110 -> *b6
   ; 0xx 11 111 -> [no direction; must be b3]

puthex: ; print a at (hl) in hex; corrupts a, b and adjusts hl
	ld	b,a
	srl	a
	srl	a
	srl	a
	srl	a
	call	hexdig
	ld	a,b
	and	#0x0f
hexdig:
	add	#'0
	cp	#'9+1
	jr	c,putdig
	add	#'A-'9-1
putdig:
	ld	(hl),a
	inc	hl
	ret
