; Gunshot sample from
; https://www.vdsteenoven.com/aquarius/psgprog.html
.area   code1  (abs)
.org    32000
; Edited by me to use definitions and better comments.
; Edited by me to use aqsnd macros

.include "aquarius.mac"

	ENVT	ENVP,572,1000 ; sample uses 0xFA0; I use 0xF9F.  Close enough.

start:	AYREG16	11,ENVP    ; Env Period
	AYREG	6,0x0F	   ; Noise Generator Period
	AYREG	7,0o67	   ; Mix: Noise A only
	AYREG	13,0b0000  ; Env Ctl: Oneshot Decay only Done
	AYREG	8,0x10	   ; Volume A Envelope
	RET		   ; Return to BASIC
