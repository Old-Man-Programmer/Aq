; z80dasm 1.1.6
; command line: z80dasm -a -l -g 57344 -t -s ebasic.sym roms/ebasic.bin

	.area code1 (abs)
.ifeq 1
	.org	0xe000
	; Cartridge header (requires encyrping bytes 16+)
	.db 'L','I',' '
	.db 0x2A
	.db 'W',0x9C,'M',0xB0,'\t',0x6C
	.db 'W',0x64,'A',0xA8,'+',0x70

start:	ld de,#0x3193
	ld hl,#lffd8h
	ld bc,#0x23
	ldir
	ld de,#0x3210
	ld hl,#0xb5
	ld bc,#0x19
	ldir
le026h:
	ld b,#0x3
	call sub_e037h
	ld b,#0x2
	call sub_e037h
	ld b,#0x6
	call sub_e037h
	jr le026h
sub_e037h:
	ld hl,#0x3400
le03ah:
	ld (hl),b
	inc hl
	ld a,h
	cp #0x38
	jr nz,le03ah
	ld hl,#0x4000
le044h:
	call 0x1e80
	cp #0xd
	jr z,le060h
	cp #0x3
	jr z,le055h
	dec hl
	ld a,h
	or l
	jr nz,le044h
	ret

le055h:
	ld a,#0xb
	call 0x1d72
	call 0xbe5
	call 0x1a40
le060h:
	ld hl,#0x187
	ld bc,#0x51
	ld de,#0x3803
	ldir
	xor a
	ld (0x38a9),a
	ld (0x38f9),a
	ld hl,#0x395d
le075h:
	inc hl
	ld c,(hl)
	ld a,h
	or l
	jr z,le086h
	xor c
	ld (hl),a
	ld b,(hl)
	cpl
	ld (hl),a
	ld a,(hl)
	cpl
	ld (hl),c
	cp b
	jr z,le075h
le086h:
	dec hl
	ld de,#0x3a25
	rst 0x20
	jp c,0xbb7
	ld de,#0xffce
	ld (0x38ad),hl
	add hl,de
	ld (0x384b),hl
	call 0xbbe
.else
	.org	0x2000
	; Extended BASIC header
	.ascii ',3$$7+'
	.db '*'
	.ascii 'LI WM\tWA+'
.endif

	ld hl,#le0e5h
	call 0xe9d
	ld hl,#0x163
	ld a,(hl)
	cp #0x79
	jr nz,le0ach
	dec hl
	dec hl
	dec hl
le0ach:
	call 0xe9d
	ld hl,#le0f7h
	ld (0x3806),hl
	ld hl,#0
	ld (0x3931),hl
	ld hl,#0x704
	ld (0x3900),hl
	ld hl,#0x3948
	ld (hl),#0
	inc hl
	ld (0x384f),hl
	call 0xbbe
	ld a,#0xc3
	ld (0x390b),a
	ld (0x390e),a
	ld (0x393e),a
	xor a
	ld (0x3933),a
	ld (0x3934),a
	ld (0x3935),a
	jp 0x402

le0e5h: .ascii '\13Extended BASIC\r\n\0'

le0f7h:
	ex (sp),hl
	push af
le0f9h:
	ld a,(hl)
	inc hl
	push hl
	ld hl,#le128h
	push bc
	ld bc,#0x9
	cpir
	ld a,c
	pop bc
	add a,a
	ld hl,#le116h
	add a,l
	ld l,a
	ld a,#0
	adc a,h
	ld h,a
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	jp (hl)

le116h: .dw le138h,le177h,le130h,le284h,le202h,le22ch,le1c2h
	.dw le18ah,le19ch

le128h:	.db  0x17,   0x16,  0x12,  0x10,  0xf,  0xe,  0xd
	.db  0xa

le130h:	ld hl,(0x3931)
	ld a,h
	or l
	jp nz,le3ddh

le138h:
	pop hl
	pop af
	ex (sp),hl
le13bh:
	ret
le13ch:
	.db 0x80|'X','O','R'
	.db 0x80|'E','D','I','T'
	.db 0x80|'L','O','C','A','T','E'
	.db 0x80|'M','E','N','U'
	.db 0x80|'C','L','S'
	.db 0x80|'L','I','N','E'
	.db 0x80|'C','I','R','C','L','E'
	.db 0x80|'G','E','T'
	.db 0x80|'P','U','T'
	.db 0x80|'D','R','A','W'
	.db 0x80

le165h:	.dw EDIT
	.dw LOCATE
	.dw MENU
	.dw CLS
	.dw LINE
	.dw CIRCLE
	.dw GET
	.dw PUT
	.dw DRAW
	
le177h:	ld a,b
	cp #0xcb
	jp nz,le138h
	pop bc
	pop af
	pop hl
	push bc
	ex de,hl
	ld de,#le13bh
	ld b,#0xce
	jp 0x4f9

le18ah:	pop de
	pop af
	pop hl
	cp #0xcf
	jr nc,le193h
	push de	
	ret
le193h:
	sub #0xce
	ld c,a
	ld de,#le13ch
	jp 0x5a8

le19ch:	pop bc
	pop af
	pop hl
	jr nc,le1adh
	push af
	cp #0x1c
	jr z,le1f4h
	cp #0x1d
	jr z,le1f0h
	pop af
	push bc
	ret
le1adh:
	sub #0x50
	jp c,0x3c4
	cp #0x9
	jp nc,0x3c4
	rlca
	ld c,a
	ld b,#0
	ex de,hl
	ld hl,#le165h
	jp 0x665
le1c2h:
	pop hl
	pop af
	pop hl
	call 0x1e80
	exx
	ld hl,#le13ch
	cp #0xf
	jr z,le1dfh
	cp #0x19
	jr z,le1e3h
	cp #0x10
	jr z,le1ebh
	cp #0x1e
	jr z,le1e7h
	or a
	exx
	ret
le1dfh:
	ld c,#0x6
	jr le1edh
le1e3h:
	ld c,#0x1
	jr le1edh
le1e7h:
	ld c,#0x9
	jr le1edh
le1ebh:
	ld c,#0x5
le1edh:
	jp 0x1f28
le1f0h:
	pop af
	xor a
	jr le1f7h
le1f4h:
	pop af
	ld a,#0x1
le1f7h:
	inc hl
	ex af,af'
	rst 8
	.db '('
	dec hl
	call sub_e5d2h
	jp 0x1a55
le202h:
	pop hl
	pop af
	pop hl
	call sub_e275h
	call 0xb45
	ld bc,#0x71c
	push bc
	push de
	rst 8
	.db '('
	call 0x10d1
	push hl
	ex de,hl
	dec hl
	ld d,(hl)
	dec hl
	ld e,(hl)
	pop hl
	call 0x975
	rst 8
	.db ')'
	rst 8
	.db 0xb0
	ld b,h
	ld c,l
	ex (sp),hl
	ld (hl),c
	inc hl
	ld (hl),b
	jp 0xe59

le22ch:	pop hl
	pop af
	pop hl
	call sub_e275h
	push de
	call 0xa37
	call 0x975
	ex (sp),hl
	ld e,(hl)
	inc hl
	ld d,(hl)
	inc hl
	ld a,d
	or e
	jp z,0x3d0
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	ld hl,(0x38de)
	ex (sp),hl
	ld (0x38de),hl
	ld hl,(0x38e2)
	push hl
	ld hl,(0x38e0)
	push hl
	ld hl,#0x38e0
	push de
	call 0x153a
	pop hl
	call 0x972
	dec hl
	rst 0x10
	jp nz,0x3c4
	pop hl
	ld (0x38e0),hl
	pop hl
	ld (0x38e2),hl
	pop hl
	ld (0x38de),hl
	pop hl
	ret

sub_e275h:
	rst 8
	.db 0xa2
	ld a,#0x80
	ld (0x38cb),a
	or (hl)
	ld c,a
	call 0x10d6
	jp 0x975
le284h:	pop hl
	pop af
	pop hl
	rst 0x28
	call m,#0x1770
	call m,#0x150b
	ld a,(0x38e7)
	cp #0x81
	jp c,le2a2h
	ld bc,#0x8100
	ld d,c
	ld e,c
	call 0x142f
	ld hl,#0x1258
	push hl
le2a2h:
	ld hl,#le2ach
	call 0x1837
	ld hl,#0x1953
	ret
le2ach:
	.db 0x9, 0x4a, 0xd7, 0x3b, 0x78, 0x2, 0x6e, 0x84
	.db 0x7b, 0xfe, 0xc1, 0x2f, 0x7c, 0x74, 0x31, 0x9a
	.db 0x7d, 0x84, 0x3d, 0x5a, 0x7d, 0xc8, 0x7f, 0x91
	.db 0x7e, 0xe4, 0xbb, 0x4c, 0x7e, 0x6c, 0xaa, 0xaa
	.db 0x7f, 0x, 0x, 0x, 0x81

CLS:	ld a,#0xb
	rst 0x18
	ret

LOCATE:	call 0xb54
	push af
	dec a
	cp #0x26
	jp nc,0x697
	rst 8
	.db ','
	call 0xb54
	cp #0x18
	jp nc,0x697
	inc e
	pop af
	ld d,a
	ex de,hl
	call sub_edd8h
	ex de,hl
	ret

MENU:	cp #0x40
	call z,#0x66b
	call sub_e36bh
	push de
	ld e,#0x1
	ld a,(hl)
	cp #0x2c
	jr nz,le30ah
	rst 0x10
	call 0xb54
	push de
	rst 8
	.db ';'
	pop de
le30ah:
	ld c,e
	ld b,#0x1
	pop de
	push de
	push bc
le310h:
	push hl
	ex de,hl
	push hl
	call sub_edd8h
	ld a,#0x2e
	rst 0x18
	pop hl
	ex (sp),hl
	call 0x985
	push hl
	call 0xfc6
	call 0xea0
	pop hl
	pop de
	pop bc
	ld a,(hl)
	cp #0x2c
	jr nz,le339h
	ex de,hl
	inc b
	call sub_e386h
	push bc
	push hl
	ex de,hl
	rst 0x10
	pop de
	jr le310h
le339h:
	cp #0x88
le33bh:
	jp nz,0x3c4
	ex (sp),hl
le33fh:
	push hl
	push bc
	ld e,#0
le343h:
	call sub_e359h
	inc e
	jr c,le353h
	call sub_e386h
	dec b
	jr nz,le343h
	pop bc
	pop hl
	jr le33fh
le353h:
	pop hl
	pop hl
	pop hl
	jp 0x785
sub_e359h:
	push bc
	call sub_edd8h
le35dh:
	call 0x1a2f
	cp #0xd
	scf
	jr z,le369h
	cp #0x20
	jr nz,le35dh
le369h:
	pop bc
	ret
sub_e36bh:
	rst 8
	.db '('
	call 0xb54
	cp #0x26
	jr nc,le37dh
	inc a
	push af
	rst 8
	.db ','
	call 0xb54
	cp #0x25
le37dh:
	jp nc,0x697
	pop de
	inc a
	ld e,a
	rst 8
	.db ')'
	ret
sub_e386h:
	ld a,l
	add a,c
	ld l,a
	cp #0x18
	ret c
	ld l,#0x17
	ret
sub_e38fh:
	rst 0x28
	jp m,le399h
	ex de,hl
	call 0x682
	ex de,hl
	ret
le399h:
	push de
	call 0x150b
	call 0x682
	call 0x150b
	ld hl,#0
	or a
	sbc hl,de
	pop de
	ret
sub_e3abh:
	push hl
	push de
	push bc
	ld a,h
	ld d,l
	call 0xb23
	pop bc
	pop de
	pop hl
	ret
le3b7h:
	ld a,(hl)
	or a
	ret z
	rst 0x18
	inc hl
	jp le3b7h
sub_e3bfh:
	cp #'a'
	ret c
	cp #'z'+1
	ret nc
	and #0x5f
	ret
sub_e3c8h:
	push hl
	ld hl,#0x2
	add hl,sp
	ld (0x392f),hl
	ld hl,#0x3860
	ld (0x3931),hl
	ld hl,(0x384f)
	ex (sp),hl
	jp 0x598
le3ddh:
	pop af
	pop af
	cp #0xd
	jr z,le3eah
	ld (hl),a
	inc hl
	ld (0x3931),hl
	pop hl
	ret
le3eah:
	xor a
	ld (hl),a
	ld h,a
	ld l,a
	ld (0x3931),hl
	ld hl,(0x392f)
	ld sp,hl
	ret

PUT:	ld a,#0x1
	jr le3fbh

GET:	xor a
le3fbh:
	jp lea0fh
sub_e3feh:
	call 0x985
	push hl
	call 0x975
	call 0x682
	pop hl
	ret

EDIT:	call 0x69c
	ret nz
	pop hl
le40fh:
	call 0x49f
	jp nc,0x6f3
	ld h,b
	ld l,c
	inc hl
	inc hl
	ld c,(hl)
	inc hl
	ld b,(hl)
	inc hl
	push bc
	call sub_e3c8h
le421h:
	pop hl
	push hl
	ld a,h
	and l
	inc a
	ld a,#0x21
	call z,#0x198a
	call nz,#0x1675
	ld a,#0x20
	rst 0x18
	ld hl,#0x3860
	push hl
	ld c,#0xff
le437h:
	inc c
	ld a,(hl)
	inc hl
	or a
	jr nz,le437h
	pop hl
	ld b,a
le43fh:
	ld d,#0
le441h:
	call 0x19da
	or a
	jr z,le441h
	call sub_e3bfh
	sub #0x30
	jr c,le45ch
	cp #0xa
	jr nc,le45ch
	ld e,a
	ld a,d
	rlca
	rlca
	add a,d
	rlca
	add a,e
	ld d,a
	jr le441h
le45ch:
	push hl
	ld hl,#le43fh
	ex (sp),hl
	dec d
	inc d
	jr nz,le466h
	inc d
le466h:
	cp #0xec
	jp z,le5a4h
	cp #0xdd
	jp z,le5b1h
	cp #0xf0
	jr z,le4b6h
	cp #0x31
	jr c,le47ah
	sub #0x20
le47ah:
	cp #0x21
	jp z,le5c7h
	cp #0x1c
	jr z,le4ebh
	cp #0x23
	jr z,le4c8h
	cp #0x19
	jp z,le53ch
	cp #0x14
	jr z,le4f5h
	cp #0x13
	jr z,le50eh
	cp #0x15
	jp z,le5b4h
	cp #0x28
	jp z,le537h
	cp #0x1b
	jr z,le4c2h
	cp #0x18
	jp z,le534h
	cp #0x11
	ld a,#0x7
	jp nz,0x198a
	pop bc
	pop de
	call 0x19ea
	jp le40fh
le4b6h:
	ld a,(hl)
	or a
	ret z
	inc b
	call 0x198a
	inc hl
	dec d
	jr nz,le4b6h
	ret
le4c2h:
	push hl
	ld hl,#le50ah
	ex (sp),hl
	scf
le4c8h:
	push af
	call 0x19da
	ld e,a
	pop af
	push af
	call c,le50ah
le4d2h:
	ld a,(hl)
	or a
	jr z,le4e9h
	call 0x198a
	pop af
	push af
	call c,sub_e571h
	jr c,le4e2h
	inc hl
	inc b
le4e2h:
	ld a,(hl)
	cp e
	jr nz,le4d2h
	dec d
	jr nz,le4d2h
le4e9h:
	pop af
	ret
le4ebh:
	call le3b7h
	call 0x19ea
	pop bc
	jp le421h
le4f5h:
	ld a,(hl)
	or a
	ret z
	ld a,#0x5c
	call 0x198a
le4fdh:
	ld a,(hl)
	or a
	jr z,le50ah
	call 0x198a
	call sub_e571h
	dec d
	jr nz,le4fdh
le50ah:
	ld a,#0x5c
	rst 0x18
	ret
le50eh:
	ld a,(hl)
	or a
	ret z
le511h:
	call 0x19da
	cp #0x20
	jp nc,le52ah
	cp #0xa
	jr z,le52ah
	cp #0x7
	jr z,le52ah
	cp #0x9
	jr z,le52ah
	ld a,#0x7
	rst 0x18
	jr le511h
le52ah:
	ld (hl),a
	call 0x198a
	inc hl
	inc b
	dec d
	jr nz,le50eh
	ret
le534h:
	ld (hl),#0
	ld c,b
le537h:
	ld d,#0xff
	call le4b6h
le53ch:
	call 0x19da
	cp #0x1c
	jr z,le562h
	cp #0xd
	jr z,le5b1h
	cp #0x11
	ret z
	cp #0x8
	jr z,le564h
	cp #0xa
	jr z,le580h
	cp #0x7
	jr z,le580h
	cp #0x9
	jr z,le580h
	cp #0x20
	jr c,le53ch
	cp #0x5f
	jr nz,le580h
le562h:
	ld a,#0x5f
le564h:
	dec b
	inc b
	jr z,le587h
	call 0x198a
	dec hl
	dec b
	ld de,#le53ch
	push de
sub_e571h:
	push hl
	dec c
le573h:
	ld a,(hl)
	or a
	scf
	jp z,0x141a
	inc hl
	ld a,(hl)
	dec hl
	ld (hl),a
	inc hl
	jr le573h
le580h:
	push af
	ld a,c
	cp #0x48
	jr c,le58ch
	pop af
le587h:
	ld a,#0x7
	rst 0x18
le58ah:
	jr le53ch
le58ch:
	sub b
	inc c
	inc b
	push bc
	ex de,hl
	ld l,a
	ld h,#0
	add hl,de
	ld b,h
	ld c,l
	inc hl
	call 0xb95
	pop bc
	pop af
	ld (hl),a
	call 0x198a
	inc hl
	jr le58ah
le5a4h:
	ld a,b
	or a
	ret z
	dec b
	dec hl
	ld a,(hl)
	call 0x198a
	dec d
	jr nz,le5a4h
	ret
le5b1h:
	call le3b7h
le5b4h:
	call 0x19ea
	pop bc
le5b8h:
	pop de
	ld a,d
	and e
	inc a
	ld hl,#0x385f
	ret z
	scf
	inc bc
	push af
	inc hl
	jp 0x428
le5c7h:
	pop bc
	pop de
	ld a,d
	and e
	inc a
	jp z,0x19e5
	jp 0x402
sub_e5d2h:
	ld a,(hl)
	cp #0x40
	call z,#0x66b
	ld bc,#0
	ld d,b
	ld e,c
	cp #0xa9
	jr z,le5f7h
sub_e5e1h:
	ld a,(hl)
	cp #0xa7
	push af
	call z,#0x66b
	rst 8
	.db '('
	call sub_e3feh
	push de
	rst 8
	.db ','
	call sub_e3feh
	rst 8
	.db ')'
	pop bc
	pop af
le5f7h:
	push hl
	ld hl,(0x3909)
	jr z,le600h
	ld hl,#0
le600h:
	add hl,bc
	ld (0x3909),hl
	ld (0x3903),hl
	ld b,h
	ld c,l
	ld hl,(0x3907)
	jr z,le611h
	ld hl,#0
le611h:
	add hl,de
	ld (0x3907),hl
	ld (0x3905),hl
	ex de,hl
	pop hl
	ret
sub_e61bh:
	ld a,(0x3900)
	push bc
	push de
	ld e,a
	dec hl
	rst 0x10
	jr z,le62eh
	rst 8
	.db ','
	cp #','
	jr z,le62eh
	call 0xb54
le62eh:
	ld a,e
	push hl
	call sub_ee0fh
	jp c,0x697
	pop hl
	pop de
	pop bc
	jp 0x66c
sub_e63ch:
	ld hl,(0x3903)
	ld a,l
	sub c
	ld l,a
	ld a,h
	sbc a,b
	ld h,a
le645h:
	ret nc
sub_e646h:
	xor a
	sub l
	ld l,a
	sbc a,h
	sub l
	ld h,a
	scf
	ret
sub_e64eh:
	ld hl,(0x3905)
	ld a,l
	sub e
	ld l,a
	ld a,h
	sbc a,d
	ld h,a
	jr le645h
sub_e659h:
	push hl
	ld hl,(0x3905)
	ex de,hl
	ld (0x3905),hl
	pop hl
	ret
sub_e663h:
	call sub_e659h
sub_e666h:
	push hl
	push bc
	ld hl,(0x3903)
	ex (sp),hl
	ld (0x3903),hl
	pop bc
	pop hl
	ret

LINE:	call sub_e5d2h
	push bc
	push de
	rst 8
	.db 0xa9
	call sub_e5e1h
	call sub_e61bh
	pop de
	pop bc
	jr z,le6c6h
	rst 8
	.db ','
	rst 8
	.db 'B'
	jr z,le6dch
	rst 8
	.db 'F'
	push hl
	call sub_eeb6h
	call sub_e663h
	call sub_eeb6h
	call sub_e64eh
	call c,sub_e659h
	inc hl
	push hl
	call sub_e63ch
	call c,sub_e666h
	inc hl
	push hl
	call sub_ee88h
	pop de
	pop bc
le6aah:
	push de
	push bc
	call sub_ee08h
	push af
	push hl
	ex de,hl
	call sub_ee4bh
	pop hl
	pop af
	call sub_ee01h
	call sub_ef48h
	pop bc
	pop de
	dec bc
	ld a,b
	or c
	jr nz,le6aah
	pop hl
	ret
le6c6h:
	push bc
	push de
	push hl
	call sub_e706h
	ld hl,(0x3909)
	ld (0x3903),hl
	ld hl,(0x3907)
	ld (0x3905),hl
	pop hl
	pop de
	pop bc
	ret
le6dch:
	push hl
	ld hl,(0x3905)
	push hl
	push de
	ex de,hl
	call le6c6h
	pop hl
	ld (0x3905),hl
	ex de,hl
	call le6c6h
	pop hl
	ld (0x3905),hl
	ld hl,(0x3903)
	push bc
	ld b,h
	ld c,l
	call le6c6h
	pop hl
	ld (0x3903),hl
	ld b,h
	ld c,l
	call le6c6h
	pop hl
	ret
sub_e706h:
	call sub_eeb6h
	call sub_e663h
	call sub_eeb6h
	call sub_e64eh
	call c,sub_e663h
	push de
	push hl
	call sub_e63ch
	ex de,hl
	ld hl,#lef66h
	jr nc,le723h
	ld hl,#lef5ch
le723h:
	ex (sp),hl
	rst 0x20
	jr nc,le737h
	ld (0x3913),hl
	pop hl
	ld (0x390c),hl
	ld hl,#sub_ef48h
	ld (0x390f),hl
	ex de,hl
	jr le746h
le737h:
	ex (sp),hl
	ld (0x390f),hl
	ld hl,#sub_ef48h
	ld (0x390c),hl
	ex de,hl
	ld (0x3913),hl
	pop hl
le746h:
	pop de
	push hl
	call sub_e646h
	ld (0x3911),hl
	call sub_ee88h
	pop de
	push de
	call sub_e77bh
	pop bc
	inc bc
	jr le761h
le75ah:
	pop hl
	ld a,b
	or c
	ret z
le75eh:
	call 0x390b
le761h:
	call sub_ee24h
	dec bc
	push hl
	ld hl,(0x3913)
	add hl,de
	ex de,hl
	ld hl,(0x3911)
	add hl,de
	jr nc,le75ah
	ex de,hl
	pop hl
	ld a,b
	or c
	ret z
	call 0x390e
	jr le75eh
sub_e77bh:
	ld a,d
	or a
	rra
	ld d,a
	ld a,e
	rra
	ld e,a
	ret
le783h:
	pop hl
	ret
sub_e785h:
	ex de,hl
	call sub_e646h
	ex de,hl
	ret

CIRCLE:	call sub_e5d2h
	rst 8
	.db ','
	call sub_e3feh
	ld a,d
	or a
	jp m,0x697
	push hl
	ex de,hl
	ld (0x3903),hl
	call sub_e3abh
	call 0x975
	ld bc,#0x8035
	ld de,#0x4f3
	call 0x13cb
	call sub_e38fh
	ld (0x391a),hl
	xor a
	ld (0x3919),a
	ld (0x3925),a
	pop hl
	call sub_e61bh
	ld c,#0x1
	ld de,#0
	call sub_e9b2h
	push de
	ld c,#0x80
	ld de,#0xffff
	call sub_e9b2h
	ex (sp),hl
	xor a
	ex de,hl
	rst 0x20
	ld a,#0
	jp nc,le7e6h
	dec a
	ex de,hl
	push af
	ld a,(0x3919)
	ld c,a
	rlca
	rlca
	or c
	rrca
	ld (0x3919),a
	pop af
le7e6h:
	ld (0x391c),a
	ex de,hl
	ld (0x3923),hl
	ex de,hl
	ld (0x3917),hl
	pop hl
	dec hl
	rst 0x10
	jp nz,le809h
	push hl
	call sub_edfah
	ld a,h
	or a
	jp z,le829h
	ld a,#0x1
	ld (0x3925),a
	ex de,hl
	jp le829h
le809h:
	rst 8
	.db ','
	call 0x985
	push hl
	call 0x975
	call sub_ea04h
	jp nz,le81fh
	inc a
	ld (0x3925),a
	call 0x142f
le81fh:
	ld hl,#0x38e7
	ld a,(hl)
	add a,#0x8
	ld (hl),a
	call sub_e38fh
le829h:
	ld (0x3915),hl
	ld de,#0
	ex de,hl
	ld (0x3921),hl
	ex de,hl
	ld hl,(0x3903)
	add hl,hl
le838h:
	call 0x1a25
	ld a,e
	rra
	jp c,le856h
	push de
	push hl
	inc hl
	ex de,hl
	call sub_e77bh
	ex de,hl
	inc de
	call sub_e77bh
	call sub_e887h
	pop de
	pop hl
	rst 0x20
	jp nc,le783h
	ex de,hl
le856h:
	ld b,h
	ld c,l
	ld hl,(0x3921)
	inc hl
	add hl,de
	add hl,de
	ld a,h
	add a,a
	jp c,le872h
	push de
	ex de,hl
	ld h,b
	ld l,c
	add hl,hl
	dec hl
	ex de,hl
	ld a,l
	sub e
	ld l,a
	ld a,h
	sbc a,d
	ld h,a
	dec bc
	pop de
le872h:
	ld (0x3921),hl
	ld h,b
	ld l,c
	inc de
	jp le838h
sub_e87bh:
	push de
	call sub_e982h
	pop hl
	ld a,(0x3925)
	or a
	ret z
	ex de,hl
	ret
sub_e887h:
	ex de,hl
	ld (0x391d),hl
	ex de,hl
	push hl
	ld hl,#0
	ld (0x391f),hl
	call sub_e87bh
	ld (0x3929),hl
	pop hl
	ex de,hl
	push hl
	call sub_e87bh
	ex de,hl
	ld (0x392b),hl
	ex de,hl
	pop de
	call sub_e785h
	call sub_e8cfh
	push hl
	push de
	ld hl,(0x391a)
	ld (0x391f),hl
	ex de,hl
	ld hl,(0x391d)
	ex de,hl
	ld a,l
	sub e
	ld l,a
	ld a,h
	sbc a,d
	ld h,a
	ld (0x391d),hl
	ld hl,(0x3929)
	call sub_e646h
	ld (0x3929),hl
	pop de
	pop hl
	call sub_e785h
sub_e8cfh:
	ld a,#0x4
le8d1h:
	push af
	push hl
	push de
	push hl
	push de
	ex de,hl
	ld hl,(0x391f)
	ex de,hl
	ld hl,(0x391a)
	add hl,hl
	add hl,de
	ld (0x391f),hl
	ld hl,(0x391d)
	add hl,de
	ex de,hl
	ld hl,(0x3923)
	rst 0x20
	jp z,le910h
	jp nc,le8fch
	ld hl,(0x3917)
	rst 0x20
	jp z,le906h
	jp nc,le922h
le8fch:
	ld a,(0x391c)
	or a
	jp nz,le92eh
	jp le929h
le906h:
	ld a,(0x3919)
	add a,a
	jp nc,le92eh
	jp le917h
le910h:
	ld a,(0x3919)
	rra
	jp nc,le92eh
le917h:
	pop de
	pop hl
	call sub_e972h
	call sub_e963h
	jp le93fh
le922h:
	ld a,(0x391c)
	or a
	jp z,le92eh
le929h:
	pop de
	pop hl
	jp le93fh
le92eh:
	pop de
	pop hl
	call sub_e972h
	call sub_eeb6h
	jp nc,le93fh
	call sub_ee88h
	call sub_ee24h
le93fh:
	pop de
	pop hl
	pop af
	dec a
	ret z
	push af
	push de
	ex de,hl
	ld hl,(0x3929)
	ex de,hl
	call sub_e785h
	ld (0x3929),hl
	ex de,hl
	pop de
	push hl
	ld hl,(0x392b)
	ex de,hl
	ld (0x392b),hl
	call sub_e785h
	pop hl
	pop af
	jp le8d1h
sub_e963h:
	ld hl,(0x3909)
	ld (0x3903),hl
	ld hl,(0x3907)
	ld (0x3905),hl
	jp sub_e706h
sub_e972h:
	push de
	ex de,hl
	ld hl,(0x3909)
	ex de,hl
	add hl,de
	ld b,h
	ld c,l
	pop de
	ld hl,(0x3907)
	add hl,de
	ex de,hl
	ret
sub_e982h:
	ld hl,(0x3915)
	ld a,l
le986h:
	or a
	jp nz,le98eh
	or h
	ret nz
	ex de,hl
	ret
le98eh:
	ld c,d
	ld d,#0
	push af
	call sub_e9a2h
	ld e,#0x80
	add hl,de
	ld e,c
	ld c,h
	pop af
	call sub_e9a2h
	ld e,c
	add hl,de
	ex de,hl
	ret
sub_e9a2h:
	ld b,#0x8
	ld hl,#0
le9a7h:
	add hl,hl
	add a,a
	jp nc,le9adh
	add hl,de
le9adh:
	dec b
	jp nz,le9a7h
	ret
sub_e9b2h:
	dec hl
	rst 0x10
	ret z
	rst 8
	.db ','
	cp #','
	ret z
	push bc
	call 0x985
	ex (sp),hl
	push hl
	call 0x975
	pop bc
	ld hl,#0x38e7
	ld a,(hl)
	or a
	jp z,le9dbh
	dec hl
	ld a,(hl)
	or a
	jp p,le9dbh
	and #0x7f
	ld (hl),a
	ld hl,#0x3919
	ld a,(hl)
	or c
	ld (hl),a
le9dbh:
	ld bc,#0x7e22
	ld de,#0xf983
	call 0x13cb
	call sub_ea04h
	jp z,0x697
	call 0x1513
	ld hl,(0x391a)
	add hl,hl
	add hl,hl
	add hl,hl
	call sub_e3abh
	call 0x975
	pop bc
	pop de
	call 0x13cb
	call sub_e38fh
	pop de
	ex de,hl
	ret
sub_ea04h:
	ld bc,#0x8100
	ld de,#0
	call 0x155b
	dec a
	ret
lea0fh:
	ld (0x393b),a
	push af
	rst 8
	.db '('
	dec hl
	call sub_e5d2h
	call sub_ec9dh
	pop af
	or a
	jr nz,lea8ch
	rst 8
	.db 0xa9
	push bc
	push de
	call sub_e5e1h
	call sub_ec9dh
	pop de
	pop bc
	push hl
	call sub_e64eh
	call c,sub_e659h
	inc hl
	ld (0x3913),hl
	call sub_e63ch
	call c,sub_e666h
	inc hl
	ld (0x3911),hl
lea41h:
	call sub_ee78h
	pop hl
	call sub_eb02h
	push hl
	push de
	push bc
	push de
	ex de,hl
	ld hl,(0x3911)
	ld c,l
	ld b,h
	ex de,hl
	sla e
	rl d
	ld hl,(0x3913)
	push bc
	ld b,h
	ld c,l
	call 0x15ca
	pop bc
	ld de,#0x4
	add hl,de
	pop de
	add hl,de
	ex de,hl
	pop hl
	rst 0x20
	jp c,0x697
	pop hl
	rst 0x20
	jp nc,0x697
	ld (hl),c
	inc hl
	ld (hl),b
	inc hl
	ex de,hl
	ld hl,(0x3913)
	ex de,hl
	ld (hl),e
	inc hl
	ld (hl),d
	inc hl
	or a
	jr lead9h
	push af
	ld hl,#0
lea86h:
	add hl,de
	dec a
	jr nz,lea86h
	pop af
	ret
lea8ch:
	push hl
	call sub_ee78h
	pop hl
	call sub_eb02h
	push de
	dec hl
	rst 0x10
	ld b,#0x5
	jr z,leaadh
	rst 8
	.db ','
	ex de,hl
	ld hl,#leb27h
leaa1h:
	cp (hl)
	jr z,leaabh
	dec hl
	dec b
	jr nz,leaa1h
	ex de,hl
	pop de
	ret
leaabh:
	ex de,hl
	rst 0x10
leaadh:
	dec b
	ld a,b
	ex (sp),hl
	push af
	ld e,(hl)
	inc hl
	ld d,(hl)
	inc hl
	push de
	push hl
	dec de
	ld hl,(0x3903)
	ex de,hl
	add hl,de
	jr c,leacdh
	ld b,h
	ld c,l
	pop hl
	ld e,(hl)
	inc hl
	ld d,(hl)
	inc hl
	push de
	push hl
	ld hl,(0x3905)
	dec de
	add hl,de
leacdh:
	jp c,0x697
	ex de,hl
	pop hl
	call sub_ec9dh
	pop de
	pop bc
	pop af
	scf
lead9h:
	push de
	call sub_ef8bh
	pop de
leadeh:
	push de
	call sub_ee08h
	push hl
	push af
	ld a,(0x393b)
	or a
	jr nz,leaefh
	call sub_efadh
	jr leaf2h
leaefh:
	call sub_efc4h
leaf2h:
	pop af
	pop hl
	call sub_ee01h
	call sub_ef3ch
	pop de
	dec de
	ld a,d
	or e
	jr nz,leadeh
	pop hl
	ret
sub_eb02h:
	rst 8
	.db ','
	ld a,#0x1
	ld (0x38cb),a
	call 0x10d1
	jp nz,0x697
	ld (0x38cb),a
	push hl
	ld h,b
	ld l,c
	ex de,hl
	add hl,de
	push hl
	ld a,(bc)
	add a,a
	ld l,a
	ld h,#0
	inc bc
	add hl,bc
	ex de,hl
	pop bc
	pop hl
	ret

leb23h:	.db 0xae, 0xad, 0x9d, 0x9c
leb27h:	.db 0xcf

DRAW:	ld de,#leb32h
	xor a
	ld (0x3934),a
	jp lecafh
leb32h:
	push de
	ld h,b
	ex de,hl
	call nz,sub_eb63h
	call z,sub_eb69h
	jp nc,leb6ch
	ld c,l
	adc a,h
	ex de,hl
	push bc
	ld a,h
	ex de,hl
	add a,#0x77
	ex de,hl
	rst 0
	add a,h
	ex de,hl
	ret z
	ld (hl),h
	ex de,hl
	pop bc
	ld b,l
	call pe,#0x3d42
	call pe,#0x384e
	call pe,#0xbe58
	.db 0xed
	jp lec92h
	out (0x52),a
	call pe,#0xcd00
	add a,l
	rst 0x20
sub_eb63h:
	ld bc,#0
	jp lebb6h
sub_eb69h:
	call sub_e785h
leb6ch:
	ld b,d
	ld c,e
	ld de,#0
	jp lebb6h
	call sub_e785h
	ld b,d
	ld c,e
	jp lebb6h
	ld b,d
	ld c,e
leb7eh:
	call sub_e785h
	jp lebb6h
	call sub_e785h
	ld b,d
	ld c,e
	jp leb7eh
	call sub_ed1dh
	ld b,#0
	cp #0x2b
	jp z,leb9ch
	cp #0x2d
	jp z,leb9ch
	inc b
leb9ch:
	ld a,b
	push af
	call sub_ed40h
	call sub_ed4eh
	push de
	call sub_ed1dh
	cp #0x2c
	jp nz,0x697
	call sub_ed4eh
	pop bc
	pop af
	or a
	jp nz,lec12h
lebb6h:
	call sub_ec5fh
	push de
	ld d,b
	ld e,c
	call sub_ec5fh
	ex de,hl
	pop de
	xor a
	ld (0x3925),a
	ld a,(0x3935)
	rra
	jr nc,lec05h
	push af
	push de
	push hl
	call sub_edfah
	ld a,h
	or a
	jr z,lebdah
	ld a,#0x1
	ld (0x3925),a
lebdah:
	ex de,hl
	ld c,l
	pop hl
	ld a,(0x3925)
	or a
	jr z,lebe4h
	ex (sp),hl
lebe4h:
	ex de,hl
	push hl
	call sub_ec82h
	pop bc
	pop hl
	push de
	ex de,hl
	ld hl,#0
lebf0h:
	add hl,de
	djnz lebf0h
	push hl
	call sub_ec82h
	pop hl
	add hl,de
	pop de
	ld a,(0x3925)
	or a
	jr z,lec01h
	ex de,hl
lec01h:
	call sub_e785h
	pop af
lec05h:
	rra
	jp nc,lec0fh
	call sub_e646h
	call sub_e785h
lec0fh:
	call sub_e972h
lec12h:
	ld a,(0x3934)
	add a,a
	jp c,lec25h
	push af
	push bc
	push de
	call sub_eeb6h
	call sub_e963h
	pop de
	pop bc
	pop af
lec25h:
	add a,a
	jp c,lec33h
	ex de,hl
	ld (0x3907),hl
	ex de,hl
	ld h,b
	ld l,c
	ld (0x3909),hl
lec33h:
	xor a
	ld (0x3934),a
	ret
	ld a,#0x40
	jp lec3fh
	ld a,#0x80
lec3fh:
	ld hl,#0x3934
	or (hl)
	ld (hl),a
	ret
	jp nc,lec52h
	ld a,e
	cp #0x4
	jp nc,lec52h
	ld (0x3935),a
	ret
lec52h:
	jp nc,0x697
	ld a,d
	or a
	jp nz,0x697
	ld a,e
	ld (0x3933),a
	ret
sub_ec5fh:
	ld a,(0x3933)
	or a
	ret z
	ld hl,#0
lec67h:
	add hl,de
	dec a
	jp nz,lec67h
	ex de,hl
	ld a,d
	add a,a
	push af
	jp nc,lec74h
	dec de
lec74h:
	call sub_e77bh
	call sub_e77bh
	pop af
	ret nc
	ld a,d
	or #0xc0
	ld d,a
	inc de
	ret
sub_ec82h:
	ld a,d
	add a,a
	jp nc,lec8eh
	ld hl,#ledd1h
	push hl
	call sub_e785h
lec8eh:
	ld a,c
	jp le986h
lec92h:
	jp nc,lec52h
	ld a,e
	call sub_ee0fh
	jp c,0x697
	ret
sub_ec9dh:
	push hl
	call sub_eeaah
	jp nc,0x697
	pop hl
	ret

; Unused?
leca6:	push hl			;eca6	e5 	. 
	call sub_eeb6h		;eca7	cd b6 ee 	. . . 
	jp nc,0x697		;ecaa	d2 97 06 	. . . 
	pop hl			;ecad	e1 	. 
	ret			;ecae	c9 	.

lecafh:
	ex de,hl
	ld (0x3939),hl
	ex de,hl
	call 0x985
	push hl
	ld de,#0
	push de
	push af
lecbdh:
	call 0xfc6
	call 0x1531
	ld a,b
	or c
	jr z,leccdh
	ld a,e
	or a
	jr z,leccdh
	push bc
	push af
leccdh:
	pop af
	ld (0x3938),a
	pop hl
	ld a,h
	or l
	jr z,led3eh
	ld (0x3936),hl
lecd9h:
	call sub_ed23h
	jr z,leccdh
	add a,a
	ld c,a
	ld hl,(0x3939)
lece3h:
	ld a,(hl)
	add a,a
lece5h:
	call z,#0x697
	cp c
	jr z,lecf0h
	inc hl
	inc hl
	inc hl
	jr lece3h
lecf0h:
	ld bc,#lecd9h
	push bc
	ld a,(hl)
	ld c,a
	add a,a
	jr nc,led17h
	or a
	rra
	ld c,a
	push bc
	push hl
	call sub_ed23h
	ld de,#0x1
	jr z,led14h
	call 0xcc6
	jr nc,led11h
	call sub_ed51h
	scf
	jr led15h
led11h:
	call sub_ed40h
led14h:
	or a
led15h:
	pop hl
	pop bc
led17h:
	inc hl
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	jp (hl)
sub_ed1dh:
	call sub_ed23h
	jr z,lece5h
	ret

sub_ed23h:
	push hl
led24h:
	ld hl,#0x3938
	ld a,(hl)
	or a
	jr z,led3eh
	dec (hl)
	ld hl,(0x3936)
	ld a,(hl)
	inc hl
	ld (0x3936),hl
	cp #0x20
	jr z,led24h
	cp #0x60
	jr c,led3eh
	sub #0x20
led3eh:
	pop hl
	ret
sub_ed40h:
	push hl
	ld hl,#0x3938
	inc (hl)
	ld hl,(0x3936)
	dec hl
	ld (0x3936),hl
	pop hl
	ret
sub_ed4eh:
	call sub_ed1dh
sub_ed51h:
	cp #0x3d
	jr z,ledb6h
	cp #0x2b
	jr z,sub_ed4eh
	cp #0x2d
	jr nz,led63h
	ld de,#ledd1h
	push de
	jr sub_ed4eh
led63h:
	ld de,#0
	ld b,#0x4
led68h:
	cp #0x2c
	jr z,sub_ed40h
	cp #0x3b
	ret z
	cp #0x3a
	jr nc,sub_ed40h
	cp #0x30
	jr c,sub_ed40h
	ld l,e
	ld h,d
	add hl,hl
	add hl,hl
	add hl,de
	add hl,hl
	sub #0x30
	ld e,a
	ld d,#0
	add hl,de
	ex de,hl
	call sub_ed23h
	ret z
	dec b
	jr nz,led68h
	cp #0x30
	jr c,led68h
	cp #0x3a
	jr nc,led68h
	jr ledafh
sub_ed95h:
	call sub_ed1dh
	ld de,#0x3860
	push de
	ld b,#0x20
	call 0xcc6
	jr c,ledafh
leda3h:
	ld (de),a
	inc de
	cp #0x3b
	jr z,ledb2h
	call sub_ed1dh
	dec b
	jr nz,leda3h
ledafh:
	call 0x697
ledb2h:
	pop hl
	jp 0xa4e
ledb6h:
	call sub_ed95h
	call sub_e38fh
	ex de,hl
	ret

; unused?
ledbe:	call sub_ed95h		;edbe	cd 95 ed 	. . . 
	ld a,(0x3938)		;edc1	3a 38 39 	: 8 9 
	ld hl,(0x3936)		;edc4	2a 36 39 	* 6 9 
	ex (sp),hl			;edc7	e3 	. 
	push af			;edc8	f5 	. 
	ld c,#0x2		;edc9	0e 02 	. . 
	call 0xba0		;edcb	cd a0 0b 	. . . 
	jp lecbdh		;edce	c3 bd ec 	. . . 

ledd1h:
	xor a
	sub e
	ld e,a
	sbc a,d
	sub e
	ld d,a
	ret

sub_edd8h:
	push af
	push hl
	exx
	ld hl,(0x3801)
	ld a,(0x380d)
	ld (hl),a
	pop hl
	ld a,l
	add a,a
	add a,a
	add a,l
	ex de,hl
	ld e,d
	ld d,#0
	ld h,d
	ld l,a
	ld a,e
	dec a
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,de
	ld de,#0x3000
	add hl,de
	jp 0x1de7
sub_edfah:
	ld de,#0xcc
	ld hl,#0x13e
	ret
sub_ee01h:
	ld (0x3928),a
	ld (0x3926),hl
	ret
sub_ee08h:
	ld a,(0x3928)
	ld hl,(0x3926)
	ret
sub_ee0fh:
	cp #0x10
	ccf
	ret c
	ld (0x3902),a
	ret
sub_ee17h:
	ld de,#0x1aca
	ld a,(0x3928)
	add a,e
	ld e,a
	jr nc,lee22h
	inc d
lee22h:
	ld a,(de)
	ret
sub_ee24h:
	push hl
	push de
	ld hl,(0x3926)
	ld a,(hl)
	or #0xa0
	xor (hl)
	jr z,lee31h
	ld (hl),#0xa0
lee31h:
	call sub_ee17h
	or (hl)
	ld (hl),a
	ld de,#0x400
	add hl,de
	ld a,(0x3902)
	add a,a
	add a,a
	add a,a
	add a,a
	ld d,a
	ld e,(hl)
	ld a,#0xf
	and e
	or d
	ld (hl),a
	pop de
	pop hl
	ret
sub_ee4bh:
	ld a,l
	or h
	ret z
	call sub_ee24h
	call lef66h
	dec hl
	jr sub_ee4bh
	push hl
	push de
	ld hl,(0x3926)
	ld a,(hl)
	or #0xa0
	xor (hl)
	jr z,lee72h
lee62h:
	ld de,#0x400
	add hl,de
	ld a,(hl)
	jr z,lee6dh
	rrca
	rrca
	rrca
	rrca
lee6dh:
	and #0xf
	pop de
	pop hl
	ret
lee72h:
	call sub_ee17h
	and (hl)
	jr lee62h
sub_ee78h:
	push hl
	push de
	ld hl,#0x3028
	ld a,e
	ld de,#0x28
	inc a
lee82h:
	dec a
	jr z,leea0h
	add hl,de
	jr lee82h
sub_ee88h:
	push hl
	push de
	ld hl,#0x3028
	ld a,e
	ld de,#0x28
lee91h:
	sub #0x3
	jr c,lee98h
	add hl,de
	jr lee91h
lee98h:
	add a,#0x3
	add a,a
	sra c
	jr nc,leea0h
	inc a
leea0h:
	add hl,bc
	ld (0x3928),a
	ld (0x3926),hl
	pop de
	pop hl
	ret
sub_eeaah:
	ld a,#0x17
	ld (0x3941),a
	ld a,#0x27
	ld (0x3942),a
	jr leec0h
sub_eeb6h:
	ld a,#0x47
	ld (0x3941),a
	ld a,#0x4f
	ld (0x3942),a
leec0h:
	push hl
	push bc
	push de
	ld a,(0x3941)
	ld h,#0
	ld l,a
	bit 7,d
	jr nz,leeech
	rst 0x20
leeceh:
	ld d,b
	ld e,c
	ld b,#0
	jr nc,leed6h
	ex (sp),hl
	inc b
leed6h:
	ld a,(0x3942)
	ld h,#0
	ld l,a
	bit 7,d
	jr nz,leef1h
	rst 0x20
leee1h:
	pop de
	jr c,.+5
	rr b
	ld b,#0xe3
	pop bc
	ccf
	pop hl
	ret
leeech:
	ld l,#0
	scf
	jr leeceh
leef1h:
	ld l,#0
	scf
	jr leee1h
	cp #0x2
	jr nc,lef01h
	push de
	ld de,#0x3029
	rst 0x20
	pop de
	ret c
lef01h:
	dec a
	dec a
	ret nc
	add a,#0x6
	push de
	ld de,#0xffd8
	add hl,de
	pop de
	or a
	ret
sub_ef0eh:
	cp #0x4
	jr c,lef1ah
	push de
	ld de,#0x33c0
	rst 0x20
	pop de
	ccf
	ret c
lef1ah:
	inc a
	inc a
	cp #0x6
	ccf
	ret nc
	sub #0x6
	push de
	ld de,#0x28
	add hl,de
	pop de
	or a
	ret
sub_ef2ah:
	dec a
	bit 0,a
	ret z
	inc a
	inc a
	dec hl
	or a
	ret
sub_ef33h:
	inc a
	bit 0,a
	ret nz
	dec a
	dec a
	inc hl
	or a
	ret
sub_ef3ch:
	push af
	push hl
	call sub_ee08h
	ld de,#0x28
	add hl,de
	jp lef82h
sub_ef48h:
	push af
	push hl
	call sub_ee08h
	call lef1ah
	jr lef82h
	push af
	push hl
	call sub_ee08h
	call lef01h
	jr lef82h
lef5ch:
	push af
	push hl
	call sub_ee08h
	call sub_ef2ah
	jr lef82h
lef66h:
	push af
	push hl
	call sub_ee08h
	call sub_ef33h
	jr lef82h
	push af
	push hl
	call sub_ee08h
	call sub_ef0eh
	jr lef82h
	push af
	push hl
	call sub_ee08h
	call sub_ef0eh
lef82h:
	ld (0x3928),a
	ld (0x3926),hl
	pop hl
	pop af
	ret
sub_ef8bh:
	ld (0x393c),hl
	ld h,b
	ld l,c
	ld (0x3911),hl
	add a,a
	ld c,a
	ld b,#0
	ld hl,#lefa3h
	add hl,bc
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	ld (0x393f),hl
	ret

lefa3h:
	.db 0xf7,0xef,0xf9,0xef,0xfd,0xef,0xfe,0xef
	.db 0xfb,0xef

sub_efadh:
	call sub_efeah
lefb0h:
	ld b,(hl)
	ld de,#0x400
	ex de,hl
	add hl,de
	ld c,(hl)
	pop hl
	ld (hl),b
	inc hl
	ld (hl),c
	inc hl
	push hl
	ex de,hl
	inc hl
	dec a
	jr nz,lefb0h
	jr lefe5h
sub_efc4h:
	call sub_efeah
lefc7h:
	ex de,hl
	ld hl,#0x400
	add hl,de
	ld c,(hl)
	pop hl
	ld b,(hl)
	inc hl
	ex af,af'
	ld a,(hl)
	inc hl
	call 0x393e
	push hl
	ex de,hl
lffd8h:	ld (hl),b
	ld de,#0x400
	ex de,hl
	add hl,de
	ld (hl),a
	ex de,hl
	inc hl
	ex af,af'
	dec a
	jr nz,lefc7h
lefe5h:
	pop hl
	ld (0x393c),hl
	ret
sub_efeah:
	ld hl,(0x3911)
	ld a,l
	ld hl,(0x393c)
	ex (sp),hl
	push hl
	ld hl,(0x3926)
	ret

; Unused?
leff7:	.db 0xb1,0xc9,0xa1,0xc9,0xa9,0xc9,0x2f,0xc9,0x23
