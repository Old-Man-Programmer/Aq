; Low C sample from
; https://www.vdsteenoven.com/aquarius/psgprog.html
.area   code1  (abs)
.org       32000
; Changed to use EQUs and slightly better comments
; Also fixed bug: LD was being used instead of 2nd to last OUT
; Later: changed to use aqsnd macros

.include "aquarius.mac"

	FREQ	LOWC,32703,1000 ; 1-C [sample used 3421; I compute 3420.]

start:  AYREG	  8,15		; Volume channel A full
	AYREG16	  0,LOWC	; Channel A period LOWC
	AYREG	  7,0o76	; Mix: Tone A only
	RET			; Return to BASIC
