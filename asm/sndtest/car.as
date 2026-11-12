; Race car engine
; From General Instruments AY-3-8910/8912 DATA MANUAL Fig 33
.area	code1	(abs)
.org    32000

.include "aquarius.mac"

	FREQ	BSND,29

start:	AYREG16	2,BSND	  ; Period B
	AYREG	7,0o74	  ; Mix: ChannelA+B Tone
	AYREG	8,15	  ; Volume A max
	AYREG	9,10	  ; Volume B lower
	LD	HL,#11*256
	LD	DE,#(11*256-4*256+1)
1$:	AYREG16R 0,H,L	  ; Period A
	LDELAY	3,1000,64
	DEC	HL
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,1$
	LD	HL,#9*256
	LD	DE,#(9*256-3*256+1)
2$:	AYREG16R 0,H,L	  ; Period A
	LDELAY	3,1000,64
	DEC	HL
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,2$
	LD	HL,#6*256
	LD	DE,#(6*256-256+1)
3$:	AYREG16R 0,H,L	  ; Period A
	LDELAY	6,1000,64
	DEC	HL
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,3$
	AYREG	8,0	  ; Volume A disable
	AYREG	9,0	  ; Volume B disable
	RET

	LDELAY_SUB
