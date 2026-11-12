; Gunshot sample
; From General Instruments AY-3-8910/8912 DATA MANUAL Fig 28
.area   code1  (abs)
.org    32000

.include "aquarius.mac"

	ENVT	ENVP,586,1000

start:	AYREG	6,15	  ; Noise Generator Period
	AYREG	7,0o07	  ; Mix All channels noise
	AYREG	8,0x10	  ; Volume A Envelope
	AYREG	9,0x10	  ; Volume B Envelope
	AYREG	10,0x10	  ; Volume C Envelope
	AYREG16	11,ENVP	  ; Envelope Period
	AYREG	13,0b0000 ; Env Ctl Oneshot Decay Only Done
	RET		  ; Return to BASIC
