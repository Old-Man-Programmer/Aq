; Sound scales
; From ATK's play.c
.area   code1  (abs)
.org	32000

.include "aquarius.mac"

; Assembly bit just sets chip registers and sleeps 1/2 sec.
; USR(x): x=0 = init; x<0 = finish; otherwise x=period
	FPSGN		; sign of faclo: +/-/0
	JR	NZ, NINIT
	; 0: init
	AYREG	7,0o76 ; Mix Only channel A tone
	AYREG	8,15 ; Vol A full
	RET
NINIT:  JP	M, END
	; Convert FACLO into 16-bit val in DE
	CALL	GETDE
	AYREG16R 0,D,E ; Period A -> DE
	; sleep 1/2 sec
	LDELAY	5,10,0
	RET
END:	AYREG	8,0 ; Vol A off
	RET

	LDELAY_SUB

; BASIC is used here in order to use floating point arithmetic.
; Not very accurate, but hopefully accurate enough.
; It's also easer to print messages from BASIC.
; Maybe in the future I will pre-generate the measly 85 entries (170
; bytes) using calc/bc/C for maximum accuracy.
;BASIC
;100 DATA"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
;101 x=2^(1/12):f=55/x^9
;REM x=1.05946309435929526455:f=32.70319566257482935116
;102 FORI=1TO84:O=INT((I-1)/12):N=I-12*O:O=O+1
;103 RESTORE100:FORJ=1TON:READN$:NEXTJ:PRINTO;"-";N$,f
;104 v=USR(111860.78125/f):f=f*x:NEXTI
;105 v=USR(-1)
