; European Siren, 3.5 sec.
; From General Instruments AY-3-8910/8912 DATA MANUAL Fig. 27
.area	code1	(abs)
.org 32000

.include "aquarius.mac"

	FREQ	HIGH,440 ; 4-A
; what the comment says:  0x256
	FREQ	LOW,187 ; 3-G?
; what the load value says: 0x156
;	FREQ	LOW,327	; 4-E?
; I'll trust the comment because it states its case twice (Hz and ms)

	AYREG16	0,HIGH	; Period A
	AYREG	7,0o76	; Mix: Tone A only
	AYREG	8,15	; Volume A full
	LD	L,#5
LOOP:	LDELAY	35,100,0; close enough; don't fret the loop overhead
	AYREG16	0,LOW	; Period A
	LDELAY	34,100,0; close enough
	AYREG16	0,HIGH	; Period A
	DEC	L
	JR	NZ,LOOP
	AYREG	8,0	; Volume A disable
	RET

	LDELAY_SUB
