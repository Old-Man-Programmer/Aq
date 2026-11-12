; Wolf whistle
; From General Instruments AY-3-8910/8912 DATA MANUAL Fig 32
.area	code1	(abs)
.org    32000

.include "aquarius.mac"

start:	AYREG	6,1	  ; Noise Generator Period
	AYREG	7,0o56	  ; Mix A tone, B noise
	AYREG	8,15	  ; Volume A max
	AYREG	9,9	  ; Volume B ~half
	AYREG	1,0	  ; Period A high (leaves 0 in A)
	OUT	(AYREG),A ; loop sets Period A low
	LD	H,#64	  ; 1748..3496Hz
	LD	L,#33
L1:	LD	A,H	  ; 4
	OUT	(AYDAT),A  ; 11
	LDELAY  12,1000,35
	DEC	H	  ; 4
	DEC	L	  ; 4
	JR	NZ,L1	  ; 12/7
	LDELAY	150-12-25,1000,0 ; 150ms-12(last)-25(next)
	LD	H,#64	  ; 1748..2331Hz
	LD	L,#17
L2:	LD	A,H	   ; 4
	OUT	(AYDAT),A  ; 11
	LDELAY	25,1000,35
	DEC	H	  ; 4
	DEC	L	  ; 4
	JR	NZ,L2	  ; 12/7
	LD L,#104-48+1	  ; 2331..1075Hz (H should be 48)
L3:	LD	A,H	  ; 4
	OUT	(AYDAT),A  ; 11
	LDELAY	6,1000,35
	INC	H	  ; 4
	DEC	L	  ; 4
	JR	NZ,L3	  ; 12/7
	AYREG	8,0	  ; Volume A disable
	AYREG	9,0	  ; Volume B disable
	RET		  ; Return to BASIC

	LDELAY_SUB
