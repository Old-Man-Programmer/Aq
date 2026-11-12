; 440hz square wave from bit banging for 2 seconds
.area	code1	(abs)
.org	32000

.include "aquarius.mac"

        LD      HL,#440*2*2
	LD      DE,#1
LOOP:	LD      A,L       ; 4
	OUT     (AQSND),A   ; 11
	SDELAY	1000000/880,1000000,42,t
        SBC     HL,DE     ; 15
	JR      NZ,LOOP   ; 12/7
        RET

	SDELAY_SUB
