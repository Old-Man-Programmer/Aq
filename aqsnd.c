/* $Copyright: $
 * Copyright (c) 2022-2025 by Steve Baker (steve.baker.llc@gmail.com)
 *   and Thomas Moore (darktjm@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Aq.h"
#include <portaudio.h>
//#include <pa_linux_alsa.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

int _is_tape = 0; // true if currently doing tape output (auto-mute, turbo)

/* There really isn't a need to process sound at SND_CLOCK */
/* Set this to the number of samples to process at once. */
/* While response time to the sound chip is probably lenient, respone time
 * to the bit banging interface is affected as well, and may be more critical */
/* 4 is slightly over 20khz, and seems reasonable */
#define PROCESS_RATE 4

/* Buffer for abit changes.  Probably only need 4. */
/* On the other hand, if using 3500 CPU cycles per wait, it's
 * possible to gather quite a few changes almost instantly. */
/* Must be even (even entries set abit high; odd entries set it low) */
#define ABIT_BUF 256

/* Use internal frequency conversion rather than PortAudio/ALSA. */
//#define INTERNAL_RESAMPLE

/* Tape drive slop.  Allow pulse widths to vary by +/- given %. */
#define TAPE_SLOP 20

/* Debugging:  write audio to this file (will cause timeouts, most likely) */
//#define WRITE_AUDIO "/tmp/snd.out"

/* Here are some things that are undocumented/hard to simulate: */
/*  When does the envelope start?
 *    After a particular register write?  low env per, high env per, env shape */
/*  What happens when you write to the tape drive?   Is there some sort
 *    of switch that turns off the sound output temporarily? */
/*  What happens if period = 0? */
/*  When does a register write take effect?
 *    Next period, next SND_CLOCK, or immediately?
 *    Presumably never immediately, but only on next SND_CLOCK.
 *   Period:  Presumably after end of period (count).
 *   Mix:  presumably next SND_CLOCK/immediately
 *   enven/volume:  presumably next SND_CLOCK/immediately
 *   env shape:  presumably after end of env period (count) */
/*  How does the noise generator work? (rng algorithm, freq range)
 *     For now, I copied the algorithm from ym2149.sv.   No idea what its
 *     information source is. */
/*  How does the sound output circuitry mangle the waveform?
 *    I output abrupt volume changes, which will of course be modified by
 *    your computer's audio output circuitry */

/* FIXME:  instead of static state, maybe support passing state in */

/* For accessing/modifying abit state both in emulator and in sound callback */
static pthread_mutex_t abit_lock = PTHREAD_MUTEX_INITIALIZER;

static /* volatile */ struct snd_state {
#ifdef INTERNAL_RESAMPLE
    double rem; /* remaining sample time from last sample period */
#endif

    /* bit banging interface */
    /* Buffer for abit cycles.  Probably only need 4 */
    unsigned long abit_start[ABIT_BUF], abit_curtime;
    int abit_len, abit_off;
    /* tape drive */
    FILE *tape_file; // currently reading/wriing from this
    unsigned long last_tapeclk;
    int bit_stat; // bit 0: value; bits 1-2: how many
    uint8_t tape_byte;
    int tape_bitno; // offset into tape_byte (- 1 for start)
    int is_saving; // currently in write mode

    /* sound chip interface */
    uint8_t addr, reg[16]; /* actual registers */
    uint16_t per[5], cnt[5]; /* 0..2=A..C; 3=noise; 4=env */
    uint32_t noise; /* random number generator state */
    uint8_t mix; /* CBACBA NNNTTT 0=enable */ /* probably not needed */
    uint8_t envctl; /* see flags below */ /* probably only need RUN and INC */
    uint8_t evol; /* current env vol */
} snd_state = {/* 0, {[14] = 0xff, [15] = 0xff}} */};
/* instead of making me use non-portable array initializers, just call reset() */

#define ENV_RUN  32 /* is env timer even running? */
#define ENV_INC  16 /* combination of ATT+ALT */
#define ENV_CONT 8 /* 0 = set vol to 0 after first attack/decay */
#define ENV_ATT  4 /* 0 = decay first, 1 = attack first */
#define ENV_ALT  2 /* 1 = alternate attack/decay */
#define ENV_HOLD 1 /* after one attack/decay, hold at current */

#define COLOR_CLOCK 3579545  /* NTSC color burst */
#define DEV_CLOCK (COLOR_CLOCK/2)  /* recommended/probable "cheap" design */
#define SND_CLOCK (DEV_CLOCK/16)  /* env runs at 1/16 this */

#define TAPE_ZERO (1200L*COLOR_CLOCK/1000000)
#define TAPE_ONE (TAPE_ZERO/2)

static void enable_sound(void);

void aqsnd_write_f7(uint8_t val)
{
    snd_state.addr = val;
}

uint8_t aqsnd_read_f7(void)
{
    return snd_state.addr;
}

static const uint8_t mask[] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff
};

void aqsnd_write_f6(uint8_t val)
{
    uint8_t raddr = snd_state.addr & 15;
    uint8_t prev = snd_state.reg[raddr];
    val &= mask[raddr];
    snd_state.reg[raddr] = val;
    switch(raddr) {
      case 0:
      case 2:
      case 4:
      case 6:
	/* Does writing here immediately reset cnt? */
	snd_state.per[raddr / 2] &= 0xf00;
	snd_state.per[raddr / 2] |= val;
	break;
      case 1:
      case 3:
      case 5:
	/* Does writing here immediately reset cnt? */
	snd_state.per[raddr / 2] &= 0xff;
	snd_state.per[raddr / 2] |= (uint16_t)val << 8;
	break;
      case 7:
	/* setting this to 1 doesn't "disable" a channel */
	/* I guess that means the clocks continue, but no sound output */
	val &= 077;
	prev &= 077;
	snd_state.mix = val;
	break;
      case 8:
      case 9:
      case 10:
	/* technically, writing 0 here is the way to disable a channel */
	/* presumably this means counters need to stop/go to 0 */
	/* but it may also just mean something internal to the chip */
	if(val & 0x10) {
	    val = 0x10;
	    prev &= 0x10;
	}
	break;
      case 11:
	/* Does writing here immediately set/start env? */
	/* Does writing here immediately reset cnt? */
	snd_state.per[4] &= 0xff00;
	snd_state.per[4] |= val;
	break;
      case 12:
	/* Does writing here immediately set/start env? */
	/* Does writing here immediately reset cnt? */
	snd_state.per[4] &= 0xff;
	snd_state.per[4] |= (uint16_t)val << 8;
	break;
      case 13:
	/* Does writing here immediately set/start env? */
	/* I think so, given that the examples do this "last" */
	/* so go ahead and prepare/reset immediately */
	snd_state.envctl = val | ((val & ENV_ATT)<<2) | ENV_RUN;
	snd_state.cnt[4] = snd_state.per[4];
	snd_state.evol = val & ENV_ATT ? 0 : 15;
	break;
      case 14:
      case 15:
	val = prev = snd_state.reg[raddr] = 0xFF;
    }
    if(val != prev)
	enable_sound();
}

uint8_t aqsnd_read_f6(void)
{
    uint8_t raddr = snd_state.addr & 15;

    return snd_state.reg[raddr];
}

static void end_tape(void);

/* At any one time, a channel is either on or off.  When it's on, its magnitude
 * is equal to the volume.  Here's a precomputed volume scaler.
 * Since there are 4 channels (including the basic 1-bit banger), and the
 * signed 16-bit format is used, these are 0..(2^13-1). */
static const uint16_t logvol[] = {
/* generated by the following calc script (slightly hand-edited):
 *   for(i=1; i<16; i++) printf("%d, ", round((sqrt(2)/2)^(15-i)*(2^13-1))) */
    0, 64, 90, 128, 181, 256, 362, 512,
    724, 1024, 1448, 2048, 2896, 4095, 5792, 8191
};

static int pa_init = 0;

static PaStream *stream = NULL;

static double sample_rate;

static void sound_sample(int16_t *out)
{
    /* produces next clock period output */
    /* therefore probably gets called @ SND_CLOCK freq. */
    /* that's actually faster than usually necessary, but I see no other
     * way to deal with sudden changes */

    /* update clock dividers */
    /* env supposedly updates 1/16 as fast, but that's actually a side
     * effect of not doing anything until the 4-bit env vol counter ticks */
    int ticked[5];
    for(int i = 0; i < 5; i++) {
	if((ticked[i] = !snd_state.cnt[i])) {
	    int per = snd_state.per[i];
	    if(!per)
		per = i < 3 ? 0x1000 : i == 4 ? 0x20 : 0x10000;
	    snd_state.cnt[i] = snd_state.per[i];
	} else
	    snd_state.cnt[i]--;
    }

    /* update noise.  From ym2149.ys; no idea what its source is */
    int noise = snd_state.noise; 
    if(ticked[3]) {
	// noise algorithm, from ym2149.vs
	// no idea where this comes from.
	int noiseh = (noise ^ (noise >> 2) ^ !noise) & 1;
	snd_state.noise = (noise >> 1) |  (noiseh << 16);
    }
    noise &= 1;

    /* update envelope */
    uint8_t evol = snd_state.evol, envctl = snd_state.envctl;
    if((envctl & ENV_RUN) && ticked[4]) {
	int vtick;
	if(envctl & ENV_INC) {
	    vtick = evol == 15;
	    evol++;
	} else {
	    vtick = !evol;
	    evol--;
	}
	snd_state.evol = evol = evol & 15;
	if(vtick) {
	    uint8_t env = snd_state.envctl;

	    // ENV_ATT handled by init code
	    if(env & ENV_ALT) {
		snd_state.envctl ^= ENV_INC;
		if(env & ENV_HOLD)
		    snd_state.evol = evol = evol ^ 15;
	    } else if(!(env & ENV_HOLD))
		snd_state.evol = evol = evol ^ 15;
	    if(!(env & ENV_CONT) || (env & ENV_HOLD))
		snd_state.envctl &= ~ENV_RUN;
	    if(!(env & ENV_CONT))
		snd_state.evol = evol = 0;
	}
    }

    /* compute bit banging output */
    int abit = 0;
    if(snd_state.abit_len) {
	/* FIXME:  deal with rollover (not likely w/ 64-bit ulong) */
	snd_state.abit_curtime += 32; /* snd = cpu/2/16 */
	pthread_mutex_lock(&abit_lock);
	int abit_next = (snd_state.abit_off + 1) % ABIT_BUF;
	while(snd_state.abit_len > 1 &&
	   snd_state.abit_curtime >= snd_state.abit_start[abit_next]) {
	    snd_state.abit_len--;
	    snd_state.abit_off = abit_next;
	    abit_next = (snd_state.abit_off + 1) % ABIT_BUF;
	}
	if(snd_state.abit_len == 1 && snd_state.abit_off % 2) {
	    unsigned long ztime = snd_state.abit_curtime -
		                  snd_state.abit_start[snd_state.abit_off];
	    /* only let it be 0 for 1/4 sec before shutting down */
	    if(ztime > 27965)
		snd_state.abit_off = snd_state.abit_len = 0;
	}
	abit = !(snd_state.abit_off & 1) && snd_state.abit_len && !_is_tape;
	pthread_mutex_unlock(&abit_lock);
    }

    /* finally, compute output (2 whole samples!) */
    int16_t *output = out;
    output[0] = output[1] = abit ? logvol[15] : 0; /* 1-bit sound */
    uint8_t mix = snd_state.mix;
    for(int i = 0; i < 3; i++, mix >>= 1) {
	uint16_t vol = snd_state.reg[8+i];
	if(vol & 0x10)
	    vol = evol;
	if(!vol)
	    continue;
	int on[2] = {};
	/* I used to capture per/2 into hper when loading cnt to decide when
	 * to switch but per shouldn't change frequently enough for glitches
	 * to matter */
	if(!(mix & 1)) {
	    on[0] = snd_state.cnt[i] >= snd_state.per[i] / 2;
	    on[1] = snd_state.cnt[i] > snd_state.per[i] / 2;
	}
	/* I suspect mixing means OR (+ w/ saturation).  The verilog seems
	 * to confirm this. */
	if(!(mix & 8)) {
	    on[0] |= noise;
	    on[1] |= noise;
	}
	vol = logvol[vol];
	/* Mixing on the output is done with pure addition, though */
	output[0] += on[0] ? vol : 0;
	output[1] += on[1] ? vol : 0;
    }
}

static int process_sound(_U const void *in,
			 void *out, unsigned long buflen,
			 _U const PaStreamCallbackTimeInfo *timeinfo,
			 _U PaStreamCallbackFlags flags,
			 _U void *user)
{
#ifdef WRITE_AUDIO
    static FILE *f = NULL;
    if(!f)
	f = fopen(WRITE_AUDIO, "w");
#endif
#ifdef INTERNAL_RESAMPLE
    double ooff = 0, ioff = snd_state.rem;
    int tot, num;
    for(unsigned long i = 0; i < buflen; i++) {
	ooff += 1.0/sample_rate;
	tot = num = 0;
	while(ioff < ooff) {
	    int16_t buf[2];
	    sound_sample(buf);
	    tot += buf[0] + buf[1];
	    num += 2;
	    ioff += 1.0/SND_CLOCK;
	}
	((int16_t *)out)[i] = tot / num;
    }
    snd_state.rem = ooff - ioff;
#else
    for(unsigned long i = 0; i < buflen; i++) {
	int16_t buf[2];
	sound_sample(buf);
	((int16_t *)out)[i] = ((int)buf[0] + (int)buf[1]) / 2;
    }
#endif
#ifdef WRITE_AUDIO
    fwrite(out, 2, buflen, f);
#endif
    /* FIXME:  stop the stream if all 4 channels off for a while */
    return paContinue;
}

void auto_end_tape(void)
{
    if(_is_tape && snd_state.last_tapeclk < _totclk - TAPE_ZERO * 8)
	end_tape();
}

void enable_sound(void)
{
    if(!pa_init) {
	pa_init = 2; /* error */
	PaError err = Pa_Initialize();
	atexit((void (*)(void))Pa_Terminate);
	if(err != paNoError) {
	    fprintf(stderr, "PortAudio failed to initialize (%d): %s\n",
		    (int)err, Pa_GetErrorText(err));
	    return;
	}
	static PaStreamParameters stream_desc = {};

	if((stream_desc.device = Pa_GetDefaultOutputDevice()) == paNoDevice) {
	    fputs("No default PortAudio output device\n", stderr);
	    return;
	}
	stream_desc.channelCount = 1; /* mono */
	stream_desc.sampleFormat = paInt16;
	stream_desc.suggestedLatency =
	    Pa_GetDeviceInfo(stream_desc.device)->defaultLowOutputLatency;
#ifdef INTERNAL_RESAMPLE
	sample_rate = Pa_GetDeviceInfo(stream_desc.device)->defaultSampleRate;
#else
	sample_rate = SND_CLOCK;
#endif
	err = Pa_OpenStream(&stream, NULL, &stream_desc, sample_rate,
			    PROCESS_RATE, paClipOff, process_sound, NULL);
	if(err != paNoError) {
	    fprintf(stderr, "PortAudio failed to open stream (%d): %s\n",
		    (int)err, Pa_GetErrorText(err));
	    return;
	}
//	PaAlsa_EnableRealtimeScheduling(stream, 1);
	pa_init = 1;
    }
    if(pa_init == 2)
	return;
    Pa_StartStream(stream);
}

static void end_tape(void)
{
    snd_state.tape_bitno = snd_state.bit_stat = 0;
    _is_tape = 0;
    snd_state.last_tapeclk = 0;
    snd_state.bit_stat = snd_state.tape_bitno = 0;
    if(snd_state.tape_file && snd_state.is_saving) {
	fclose(snd_state.tape_file);
	snd_state.tape_file = NULL;
    }
}

void aqsnd_write_fc(uint8_t val)
{
    pthread_mutex_lock(&abit_lock);
    int abit = val & 1, oabit = (snd_state.abit_off + snd_state.abit_len) % 2;
    if(abit == oabit) {
	pthread_mutex_unlock(&abit_lock);
	return;
    }
    if(_is_tape && snd_state.abit_len > 1)
	snd_state.abit_len-= 2;
    if(snd_state.abit_len == ABIT_BUF) {
	pthread_mutex_unlock(&abit_lock);
	fputs("dropped a bit banging audio cycle\n", stderr);
	return;
    }
    if(!snd_state.abit_len)
	snd_state.abit_curtime = _totclk;
    snd_state.abit_start[(snd_state.abit_off + snd_state.abit_len) % ABIT_BUF] = _totclk;
    snd_state.abit_len++;
    pthread_mutex_unlock(&abit_lock);
    if(abit && !_is_tape)
	enable_sound();

    /* Tape.  In order to avoid counting silence, only high half checked */
    /* This assumes 50% duty cycle (approx.) */
    if(!abit && _tape) {
	unsigned long cur_t;
	int bit;
	/* Compute bit for this half-period. */
	cur_t = _totclk - snd_state.last_tapeclk;
#define iabs(a) ((a) < 0 ? -(a) : (a))
#define pdiff(a, b) iabs(((long)(a) - (long)(b)) * 100 / (long)(b))
	if(pdiff(cur_t, TAPE_ZERO / 2) <= TAPE_SLOP)
	    bit = 0;
	else if(pdiff(cur_t, TAPE_ONE / 2) <= TAPE_SLOP)
	    bit = 1;
	else
	    bit = -1;
	if(!snd_state.bit_stat || (snd_state.bit_stat & 1) != bit) {
	    /* not the same as previous */
	    if(snd_state.bit_stat || (bit < 0 && (cur_t > TAPE_ZERO / 2 ||
						  snd_state.tape_bitno ||
						  snd_state.bit_stat)))
		/* terminate if invalid */
		end_tape();
	    snd_state.bit_stat = bit >= 0 ? bit + 2 : 0;
	} else {
	    /* Got a valid bit (two of the same values in a row) */
	    snd_state.bit_stat = 0;
	    int bitno = snd_state.tape_bitno++;
	    if((!bitno && bit) || (bitno > 8 && !bit))
		// No, I'm not going to back up the prev. 0 and restart
		end_tape();
	    else {
		if(!bitno)
		    snd_state.tape_byte = 0;
		else if(bitno <= 8) {
		    snd_state.tape_byte <<= 1;
		    snd_state.tape_byte |= bit;
		} else if(bitno == 10) { /* done! */
		    snd_state.tape_bitno = 0;
		    if(snd_state.tape_file && !snd_state.is_saving) {
			fclose(snd_state.tape_file);
			snd_state.tape_file = NULL;
		    }
		    if(!snd_state.tape_file) {
			snd_state.tape_file = fopen(_tape, "a");
			if(snd_state.tape_file)
			    /* needs a byte of padding or it things it's bad */
			    putc(0x00, snd_state.tape_file);
			snd_state.is_saving = 1;
			_is_tape = 1;
		    }
		    if(!snd_state.tape_file) {
			perror(_tape);
			_tape = NULL;
		    } else
			putc(snd_state.tape_byte, snd_state.tape_file);
		}
	    }
	}
    }
    snd_state.last_tapeclk = _totclk;
}

uint8_t aqsnd_read_fc(void)
{
    if(!_tape)
	return 0;
    if(snd_state.tape_file && snd_state.is_saving)
	end_tape();
    if(!snd_state.tape_file) {
	snd_state.tape_file = fopen(_tape, "r");
	if(!snd_state.tape_file)
	    snd_state.tape_file = fopen("/dev/zero", "r");
	if(!snd_state.tape_file) {
	    _tape = NULL;
	    return 0;
	}
    }
    _is_tape = 1;
    if(!snd_state.bit_stat) {
	snd_state.bit_stat = 5;
	snd_state.tape_bitno = 10;
	snd_state.last_tapeclk = _totclk;
    }
    if(snd_state.last_tapeclk > _totclk)
	return snd_state.bit_stat & 1;
    int st = ++snd_state.bit_stat;
    if(st == 6) {
	if(++snd_state.tape_bitno == 11) {
	    int c = getc(snd_state.tape_file);
	    if(c < 0) {
		fclose(snd_state.tape_file);
		snd_state.tape_file = NULL;
		c = 0;
	    }
	    snd_state.tape_byte = c;
	    snd_state.tape_bitno = 0;
	} else if(snd_state.tape_bitno > 1) {
	    snd_state.tape_byte <<= 1;
	    snd_state.tape_byte |= 1; // stop bits
	}
	snd_state.bit_stat = 2;
    }
    if(!snd_state.tape_bitno ||
       !(snd_state.tape_byte & 0x80))
	snd_state.last_tapeclk += TAPE_ZERO / 2;
    else
	snd_state.last_tapeclk += TAPE_ONE / 2;
    return st & 1;
}

void aqsnd_reset(void)
{
    end_tape();
    memset((void *)&snd_state, 0, sizeof(snd_state));
    /* note: verilog ym2149 sets reg7 = 0xff, but official docs say 0 */
    /* doesn't matter much, since setting reg9..11 to 0 disables output */

    /* in case there's no separate controller support */
    snd_state.reg[14] = snd_state.reg[15] = 0xff;
    // pthread_mutex_init(&abit_lock); // cancel current locks
//    enable_sound();
}
