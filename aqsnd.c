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
/****
 * Sound driver for Mattel Aquarius.
 * Implements bit-banging interaface (port FC) for audio out and tape I/O
 * Implements AY-3-8910 (ports F6/F7) from expansion module.
 * Output is via SDL2.
 * 
 * Note:  This does not handle controllers plugged into the expansion module,
 * even though they are managed by the AY-3-8910 as well.  It is intended
 * that the main I/O routine use either aqsnd_read_f6() or a gamepad reader
 * depending on the result of aqsnd_read_f7() (i.e., replace data read
 * depending on saved register #): 14-A, 15-B = controller; rest is aqsnd.
 * Technically register 7 also has DDR bits, but for controllers, these are
 * always set to input (0), so it's safe to ignore them.
 */
#include "Aq.h"
#include <SDL_audio.h>
#include <time.h>
#include <pthread.h>

/*
 * General overview:
 *   hw interface:
 *      aqsnd_reset() must be called at reset or startup
 *      aqsnd_write_{fc,f6,f7} -> port writes
 *      aqsnd_read_{fc,f6,f7} -> port reads
 *   other:
 *      aqsnd_process() must be called every so often.  It is called
 *         automatically when one of the above functions updates the
 *         sound state, but it also needs to be called to keep the sound
 *         chip running and to finish off tape access.
 *
 * All timing outside of the final push to the audio device is done using
 * the CPU clock (_totclk).  Calling aqsnd_process() updates a large
 * ring buffer (1 sample generating, 1 sample for generate overrrun protection,
 * OUTPUT_DELAY more for output and underrun protection), which is read by
 * the real-time audio output loop.  The frequency and size of this
 * buffer are controlled by SDL2 library defaults (determined by the
 * underlying hardware, and also affected by the env. variables
 * SDL_AUDIO_FREQUENCY and SDL_AUDIO_SAMPLES).  Sample rate conversion from
 * CPU clock speed to audio buffer speed is done using my own internal
 * resampling algorithm (invented on the spot, so probably not ideal,
 * but also probably better than my old internal resampling which used
 * floating point assumptions to align the data).
 * All sound chip updates are done one sound clock at a time using the
 * internal update_chip() funciton.  All tape updates are done directly
 * in the bit banging routines ({read,write}_fc()), except for the final
 * EOF timeout at the end.
 * When _turbo is set, audio is muted by forcing the sound output loop to
 * output zeroes, and forcing aqsnd_process() to no longer fill the ring
 * buffer.  Ring buffer fills resume once unmuted.  Note that aqsnd_process()
 * still updates the sound chip and checks for end-of-tape based on CPU
 * times.  Some emulators support unmuted fast forward with squeaky fast-
 * forward like sound, but I don't, and probably never will.
 *
 * Imported global defines:
 *    CLKRATE -> NTSC color clock (CPU speed)
 *    TURBO_TAPE -> _turbo flag for tape operation
 * Imported global variables:
 *    _totclk -> for timing.  Must monotonically increase starting at reset
 *    _tape -> file name for tape access (all tape stuff is disabled if NULL)
 *    _turbo -> TURBO_TAPE flag is set while tape is accessed
 *              If non-0 for any reason, audio output is muted.
 *     _ilog -> file for informative log messages (just the open message)
 */
/* Configuration options: */
/* Number of delays before starting sound output loop; at least 1.
 * This affects latency and ring buffer size (delay+2 sample buffers).
 */
#define OUTPUT_DELAY 2
/*
 * The options from the old code have been eliminated:
 *   The bit banging buffer is obsolete/merged into the output ring
 *   buffer (no more config option for buffer len).
 *   I no longer make internal resampling optional.
 *   Tape slop was never really configurable to begin with (it depends
 *   on the Aquarius ROM), so I moved that define down below.
 *   Timing is no longer controlled by the audio loop, so the number of
 *   samples processed by it is no longer relevant, and is, in any case,
 *   much, much larger than it ever could've been with the old code.
 *   The old code maxed out at 4 sound clocks, or 35us.  The new code
 *   seems to be on average 10ms in the sound loop.  This had to be fast
 *   before to respond to register changes quickly, but now register
 *   changes are handled outside of the audio loop in aqsnd_process(), and
 *   so are never dropped due to the sound loop running too slowly.
 */
/*
 * TODO:
 *   - Set _turbo when saving, even if _tape not set.  Turns saving into
 *     more of a NOOP.
 *   - Allow _tape to be a directory or pattern.  If -t is a file, it's
 *     just that file.  If it's a dir, it's a dir w/ no pattern
 *     restriction.  Otherwise, it's a dir with pattern restriction.
 *     If the pattern restriction is X, and X.dir exists, use that
 *     as a directory file.  If jsut a dir, and .dir exists, use that
 *     as a directory file.  Otherwise, all files in dir are matched against
 *     pattern and sorted in "C" locale order.  The file names are
 *     irrelevant for loading.  When saving, it saves as the file name,
 *     with a 3-digit sequence number prepended.  This sequence number is
 *     the total number of filtered files in the directory.  If the pattern
 *     is *{[^]*[]}, i.e. just a * followed by non-pattern text, this
 *     non-pattern text is appended to the output file name.  If a .dir
 *     file is present, the name is appended to the .dir file.  To support
 *     the same sort of auto-naming, I could allow rules in the .dir file.
 *     e.g. <name-pat>: <name> -> %r=rev,%s=seq,%n=name,%%=%.  If the
 *     file matches name-pat, it is sorted in C locale order on read, and
 *     the .dir file is not updaeted on write.  Note that if a file is
 *     overwritten, the old file gets removed from .dir as well.  I suppose
 *     it would also be nice to detect changes in .dir file and/or directory
 *     contents, or at least rescan on tape rewind/rollover/reset.
 *   - Support seeking in the tape.  On the command line, maybe
 *     -T <file> seeks to file, -T <file>++ seeks to just after file,
 *     -T <n> seeks to file/array#n.  Maybe also -T <file>#n to seek to
 *     nth file named file.  In GUI, forward-file/backward-file/full-rewind
 *     keys as well as a way to enter explicit position like -T.  I
 *     supose the -T option could also be merged with -t by separating
 *     file from position with '@': -t mytape@CLK2++.
 *   - Re-add PortAudio support.  Make it a config #define.  Should be
 *     trivial, since converting PortAudio to SDL2 was trivial.  Then again,
 *     it's impossible with PortAudio to determine the automatic sample
 *     length until the callback time, and according to docs, even then it
 *     can vary from call to call.  This makes PortAudio unsuitable for this
 *     application.
 */

/* Here are some things that are undocumented/hard to simulate.
 * At least for me.  Sombeody else must know the answers, because
 * other emulators for the sound chip and Aquarius do exist: */
/*  When does the envelope start?
 *    After a particular register write?  low env per, high env per, env shape */
/*  What happens if period = 0?
 *    Stop the clock?  Act like max+1? */
/*  When does a register write take effect?
 *    Next period, next SND_CLOCK, or immediately?
 *    Presumably never immediately, but only on next SND_CLOCK.
 *      That's what I now do, always, even for envctl.
 *   Period:  Presumably after end of period (count).
 *     I now load period into count whenever either byte is written.
 *     This should have little to no glitch effect due to the horrible
 *     register interface, because it's still fast enough to write both
 *     bytes before a period ends.  Or there will be a very short glitch.
 *   Mix:  presumably next SND_CLOCK/immediately
 *   enven/volume:  presumably next SND_CLOCK/immediately
 *   env shape:  presumably after end of env period (count) */
/*  How does the noise generator work? (rng algorithm, freq range)
 *     For now, I copied the algorithm from ym2149.sv.   No idea what its
 *     information source is. */
/*  How does the sound output circuitry mangle the waveform?
 *    I output abrupt volume changes, which will of course be modified by
 *    your computer's audio output circuitry */

/* PortAudio uses a double for sample_rate, but I don't want to support
 * fractional rates, or insanely high rates.  At 44.1/48Khz, fractional
 * differences amount to less than .001% distortion, and I can't even imagine
 * audio ever needing more than, say, 80khz (4x human hearing), so I'm not
 * even sure why I use unsigned long, other than avoiding casting when
 * dealing with sample_rate and CPU clocks in the same expression.  SDL2
 * uses integers, anyway, and I no longer support PortAudio. */
static unsigned long sample_rate;

/* For sample rate conversion, I use integer arithmetic.  This adds or
 * removes a clock to a sample every so often.  When depends on the
 * start time.  The pattern repeats every time the frequencies match
 * up, which is lcm(SND_CLOCK, sample_rate).  This is stored here so
 * that I can keep the numbers smaller by subtracting this out when the
 * time exceeds it. */
static unsigned long clk_sync;

/* I only support signed native-endian 16-bit audio output.  The audio
 * buffer is a ring buffer of size sample_data_len samples (not bytes). */
static int16_t *sample_data;
static unsigned sample_data_len;

/* The ring buffer is accessed via these two pointers.  In order
 * to detect underrun/overrun, separate tasks need to know what the other
 * pointer is, so they are volatile and a lock is provided to ensure
 * stability.  The signal eliminates reader underruns by allowing the
 * reader to sleep until enough data is available, although it might
 * lead to hardware underruns, instead.  Given the rather large buffer
 * between current time and play time (at least 1 whole sample length,
 * usually about 10ms), overruns should be rare.
 * I could add a "sample_written" signal to completely eliminate overruns
 * as well, but that's just inviting emulator lockup due to flukes in the
 * audio output.
 * Note that both pointers are offsets into the ring buffer, and as such
 * represent 1/sample_rate seconds.  The sound output loop always
 * advances by a full buffer length.  On muting, both pointers are set to
 * zero.  On unmuting, the reader doesn't start up until OUTPUT_DELAY whole
 * sample buffers later, so it shouldn't ever stall (FIXME: but in real life,
 * it does, quite often, even if I delay by 2 whole samples, resulting in
 * underruns, audio stuttering, pops, etc.).
 */
static volatile unsigned write_sample = 0, read_sample = 0;
pthread_mutex_t sample_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sample_ready = PTHREAD_COND_INITIALIZER;

#if 0 /* for debugging */
/* just lock this w/ sample_lock as well */
#define NUM_EVENTS 10*60*1000*10
static struct {
    unsigned long totclk;
    unsigned long curtime; /* real time */
    unsigned write_sample, read_sample;
    unsigned long ev_data; /* Low 4 bits identify event; rest are data */
} sevent[NUM_EVENTS];
static volatile unsigned cur_sevent = 0;
static struct timeval start_t;

#define EV_PROCESS 0
#define EV_SNDCB 1
#define EV_WRFC 2
#define EV_TURBO 8

static void write_event(unsigned long data)
{
    if(cur_sevent == NUM_EVENTS) {
	fputs("Out of event space\n", stderr);
	exit(1);
    }
    pthread_mutex_lock(&sample_lock); /* should be recursive */
    unsigned ev = cur_sevent;
    struct timeval ts;
    cur_sevent = ev + 1;
    sevent[ev].totclk = _totclk;
    sevent[ev].write_sample = write_sample;
    sevent[ev].read_sample = read_sample;
    sevent[ev].ev_data = data | (_turbo ? EV_TURBO : 0);
    gettimeofday(&ts, NULL);
    sevent[ev].curtime = (ts.tv_sec - start_t.tv_sec) * 1000000 + (ts.tv_usec - start_t.tv_usec);
    pthread_mutex_unlock(&sample_lock);
}

static void dump_events(void)
{
    FILE *f = fopen("sndev", "w");
    if(f) {
	fwrite(sevent, sizeof(sevent[0]), cur_sevent, f);
	fclose(f);
    }
}

static void ev_init(void)
{
    static int did_init = 0;
    if(!did_init) {
	did_init = 1;
	gettimeofday(&start_t, NULL);
	atexit(dump_events);
    }
}
#else
#define write_event(x)
#define ev_init()
#endif

/* If state saving is implemented, this is the state to save, I guess.
 * I wouldn't bother, though.  I'd just maybe save the sound chip reigisters,
 * using the high level write_f7() and read_f6() routines:
 *    uint8_t f7_state, f6_state[14]; // state to save
 *    f7_state = aqsnd_read_f7();
 *    for(int i = 0; i < 14; i++) {
 *      aqsnd_write_f7(i);
 *      f6_state[i] = aqsnd_read_f7();
 *    }
 * There may be minor sound glitches due to this, but I don't think it
 * matters enough to worry about it.  I'd also make sure to never allow
 * saving state while _turbo is active, even if it isn't due to tape access.
 * Well, maybe it's not such a great idea to save all the registers, since
 * that might cause possibly long sound effects to repeat (due to env being
 * reset).  I don't really care enough right now, but I guess saving the
 * internal, unadvertised envctl state would cure that.  I'd rather just
 * make the user live with it.
 * One other internal state that might be useful is the current seek pointer
 * in the tape, but maybe that is better dealt with by providing a way
 * to seek in the command line and interface (see TODO).
 */

static struct snd_state {
    unsigned long last_update; // 0 means no update yet since reset/_turbo
    unsigned long sample_start; // The start of time; adjusted on overruns
      // sample to write to is computed by (t-sample_start)*freq_adj.
    long partial_sample; // accumulator for sample rate conversion
      // holds one value for every CPU clock within the currently generating
      // sample.  E.g. if current sample starts at clock 131, and next sample
      // starts at clock 193, up to 62 samples will accumulate in
      // partial_sample, after which partial_sample/62 will become the
      // current sample.
    /* bit banging interface */
    int abit_on;
    /* tape drive */
    FILE *tape_file; // currently reading/writing from this
    unsigned long last_tapeclk; // timestamp of last/next tape event
    int tape_active; // non-0: active; negative: writing
    int bit_stat; // bit 0: value; bits 1-2: how many (10v = valid v)
    uint8_t tape_byte; // shift register for tape data
    int tape_bitno; // offset into tape_byte (-1 for start bit; >8 = stop)

    /* sound chip interface */
    unsigned long last_snd_clock; /* If 0, chip is inactive */
    int16_t chip_output; /* output as of last_snd_clock */
    uint8_t addr, reg[16]; /* actual registers */
    uint16_t per[5]; /* 0..2=A..C; 3=noise; 4=env (convenience) */
    uint16_t cnt[5]; /* 0..2=A..C; 3=noise; 4=env (active counters) */
    uint32_t noise; /* random number generator active state */
    uint8_t envctl; /* see flags below (active state+convenience/reg13) */
    uint8_t evol; /* current env vol (active count) */
} snd_state = {}; /* only tape_file needs zeroing; rest is done by reset() */

/* active state */
#define ENV_RUN  32 /* is env timer even running? */
#define ENV_INC  16 /* combination of ATT+ALT */
/* register 13 */
#define ENV_CONT 8 /* 0 = set vol to 0 after first attack/decay */
#define ENV_ATT  4 /* 0 = decay first, 1 = attack first */
#define ENV_ALT  2 /* 1 = alternate attack/decay */
#define ENV_HOLD 1 /* after one attack/decay, hold at current */

/***********  Read/write sound chip registers **************/
// #define DEV_CLOCK (CLKRATE/2)  /* recommended/probable "cheap" design */
// #define SND_CLOCK (DEV_CLOCK/16)

void aqsnd_write_f7(uint8_t val)
{
    snd_state.addr = val;
}

uint8_t aqsnd_read_f7(void)
{
    return snd_state.addr;
}

/* Valid bits in the sound chip */
/* All other bits always return 0 if read */
static const uint8_t mask[] = {
    0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
    0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0, 0 /* technically 0xff, 0xff */
};

void aqsnd_write_f6(uint8_t val)
{
    uint8_t raddr = snd_state.addr & 15;
    val &= mask[raddr];
    snd_state.reg[raddr] = val;
    uint8_t cnt = raddr / 2; /* A/B/C/noise reg# -> counter# */
#define b2w(h, l) (((uint16_t)(h) << 8) | (uint16_t)(l))
    switch(raddr) {
      case 11: // Env period low
	cnt = 4;
	/* Fall through */
      case 0:  // Period A low
      case 2:  // Period B low
      case 4:  // Period C low
	snd_state.per[cnt] = b2w(snd_state.reg[raddr + 1], val);
	/* is this needed?  car demo? */
	if(snd_state.cnt[cnt] > snd_state.per[cnt])
	    snd_state.cnt[cnt] = snd_state.per[cnt];
	break;
      case 6: // Random period; no high byte
	snd_state.per[cnt] = val;
	/* is this needed?  car demo? */
	if(snd_state.cnt[cnt] > snd_state.per[cnt])
	    snd_state.cnt[cnt] = snd_state.per[cnt];
	break;
      case 12: // Env period high
	cnt = 4;
	/* Fall through */
      case 1:  // Period A high
      case 3:  // Period B high
      case 5:  // Period C high
	snd_state.per[cnt] = b2w(val, snd_state.reg[raddr - 1]);
	if(snd_state.cnt[cnt] > snd_state.per[cnt])
	    snd_state.cnt[cnt] = snd_state.per[cnt];
	break;
      case 7: // Mix: DDcbaCBA ; lc=noise, uc=tone; DD=DDR (don't care)
	/* setting this to 1 doesn't "disable" a channel */
	/* I guess that means the clocks continue, but no sound output */
	/* Nothing special to do here */
	break;
      case 8:  // Volume A (xxxevvvv)
      case 9:  // Volume B
      case 10: // Volume C
	/* technically, writing 0 here is the way to disable a channel */
	/* I could probably set last_snd_clock = 0 if all 3 are 0 */
	/* But I'd like to keep the rng and env running */
	/* So I don't ever turn off unless reset */
	if(val && !snd_state.last_snd_clock) {
	    snd_state.last_snd_clock = _totclk;
	    if(!snd_state.last_update) { // probably never happens due to startup beep
//		enable_sound(); // done in reset() now
		snd_state.last_update = _totclk;
	    }
	}
	break;
      case 13: // Env ctrl
	/* Does writing here immediately set/start env? */
	/* I think so, given that the examples do this "last" */
	/* so go ahead and prepare/reset immediately */
	snd_state.envctl = val | ((val & ENV_ATT)<<2) | ENV_RUN;
	snd_state.cnt[4] = snd_state.per[4];
	snd_state.evol = val & ENV_ATT ? 0 : 15;
	// technically, only vol enables channels, but what enables env?
	// I'm just going to delay until channel active.  Probably OK
	// otherwise, enable chip as shown above.
	break;
      // ignore writes to 14: Port A, 15: Port B
    }
    aqsnd_process();
}

uint8_t aqsnd_read_f6(void)
{
    uint8_t raddr = snd_state.addr & 15;

    if(raddr < 14)
	return snd_state.reg[raddr];
    else
	return 0xff; /* pretend nothing pressed. */
}

/***********  Read/write bit banging sound/tape **************/

/* Tape timing numbers from https://www.vdsteenoven.com/aquarius/iomap.html */
#define TAPE_ZERO (1200L*CLKRATE/1000000)
#define TAPE_ONE (TAPE_ZERO/2)
/* How much the above numbers vary in real life (%) */
#define TAPE_SLOP 28
/* From actually reading the ROM, there is some variability:
 *   The Aquarius writes one bit at a time: low-high-low-high.  The time
 *   for this pattern is eactly 4204 clocks for a 1, and 8300 clocks for a 0.
 *   This translates to ONE=586us, ZERO=1159us.  Between bits, the output
 *   is left high, so the second high pulse is where all the TAPE_SLOP
 *   lies.  A byte is sent in 3 segments.  Start, data, and then stop.
 *   Within each segment, the inter-bit delay is 44 clocks (+2 if next bit
 *   is 0).  Between start and stop it's an additional 44 clocks.  Between
 *   data and stop it's an additional 42 clocks.   Between bytes the time
 *   varies wildly, so the very last 1 of the last stop bit should just be
 *   ignored.  On read, the Aquarius completely ignores the stop bits,
 *   anyway.  A byte is read as a start bit followed by 8 data bits.  The
 *   bit read procedure is:  ignore until 0, then ignore until 1, then
 *   measure width until 0, then measure width until 1 again.  The total
 *   measured width is kept in 8 bits, and is compared against a single
 *   value:  if short, then 1, else 0.  This cutoff is 2666 clocks (744us).
 *   It will overflow at 9218 clocks (2575us).  If I can rely on this, I can
 *   assume that the elongated ones will never be longer than 744 minus some
 *   safety margin.
 *   In summary:  ONE=586..730; ZERO=760..2575.
 *
 *   At a slightly higher level, data is stored as 1 start bit (0), 8 data
 *   bits MSB first, and 2 stop bits (1).  It is read as 1 start bit, 8 data
 *   bits.  The first stop bit helps to determine the end of the last data
 *   bit, but the stop bits are otherwise ignored.
 *
 *   At a higher level, files begin with a synchronization stream, to help
 *   align the start bit.  This is written as 12 0xFF data (01111111111),
 *   followed by a 00 data to verify/end the sync.  Presumably no data can
 *   ever have this pattern (hard to do with BASIC, but might be possible
 *   with arrays, but it probably doesn't matter much).  File lengths are
 *   not stored.   There are 3 file formats:  file name, BASIC, array.
 *   File names are always 6 bytes long (FIXME: how are they validated?
 *   for CLOAD"" it's obvious, but for CLOAD w/o name?).  They are always
 *   assumed to be followed immediately by a BASIC file.  Both BASIC and
 *   array files are terminated by a string of 16 zeroes, which are used as
 *   an EOF marker for BASIC and a skip mark for both (but only 10 bytes
 *   are read -- is this a bug?).  BASIC stops reading at the EOF marker,
 *   and arrays stop reading when the target array is full (regardless of
 *   whether or not there is more/less data available).  Arrays are also
 *   preceeded by 6 bytes of ###### to distinguish from actual files (but
 *   not sure what would happen if you'd save a file with that name).
 *
 *   So why does it sometimes hang on load?  Why did I feel the need to
 *   insert a 0 between files?  I should definetly drop that 0.  I might
 *   also want to make sure that the first read_fc bit is always 1.  Maybe
 *   store the "last file" marker, and back up to that if file aborted
 *   mid-read.  Shouldn't be needed, though, since tbe Aquarius should
 *   resynchronize.  I have no idea why it hangs sometimes.
 *   I could make the tape detection act more like the Aquarius, or by
 *   at least using TAPE_SLOP only on inter-bit gaps, but it saving seems
 *   to work well enough that I don't care. */

static void end_tape(void)
{
    if(snd_state.tape_file && snd_state.tape_active < 0) {
	fclose(snd_state.tape_file);
	snd_state.tape_file = NULL;
    }
    snd_state.tape_active = 0;
    _turbo &= ~TURBO_TAPE;
    snd_state.last_tapeclk = 0;
    snd_state.bit_stat = snd_state.tape_bitno = 0;
}

static void enable_sound(void);

void aqsnd_write_fc(uint8_t val)
{
    int abit = val & 1;
    if(abit == snd_state.abit_on)
	return;
    write_event(EV_WRFC + (abit << 4));
    if(!snd_state.last_update) {
//	enable_sound(); // done in reset() now
	snd_state.last_update = _totclk;
    }
    aqsnd_process();
    snd_state.abit_on = abit;

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
						  snd_state.tape_bitno)))
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
		} else if(bitno == 9) { /* got a byte with stop bit.  close enough. */
		    /* In fact, like the real machine, I should just
		     * ignore stop bits, but slop is too high for that.
		     * The real machine has the advantage of user action
		     * to initiate recording
		     */
		    if(snd_state.tape_file && snd_state.tape_active >= 0) {
			fclose(snd_state.tape_file);
			snd_state.tape_file = NULL;
		    }
		    if(!snd_state.tape_file) {
			snd_state.tape_file = fopen(_tape, "ab");
			snd_state.tape_active = -1;
			_turbo |= TURBO_TAPE;
		    }
		    if(!snd_state.tape_file) {
			perror(_tape);
			_tape = NULL;
		    } else
			putc(snd_state.tape_byte, snd_state.tape_file);
		} else if(bitno == 10) /* done! */
		    snd_state.tape_bitno = 0;
	    }
	}
    }
    snd_state.last_tapeclk = _totclk;
}

uint8_t aqsnd_read_fc(void)
{
    if(!_tape)
	return 0;
    if(snd_state.tape_file && snd_state.tape_active < 0)
	end_tape();
    if(!snd_state.tape_file) {
	snd_state.tape_file = fopen(_tape, "rb");
	if(!snd_state.tape_file) {
	    perror(_tape);
	    _tape = NULL;
	    return 0;
	}
    }
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
	    int c;
	    if(!snd_state.tape_active) {
		snd_state.tape_active = 1;
		_turbo |= TURBO_TAPE;
	    }
	    c = getc(snd_state.tape_file);
	    if(c < 0) {
		fclose(snd_state.tape_file);
		snd_state.tape_file = fopen(_tape, "rb");
		if(!snd_state.tape_file ||
		   (c = getc(snd_state.tape_file)) < 0) {
		    perror(_tape);
		    _tape = NULL;
		    end_tape();
		    return 0;
		}
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

/***********  Geneate sound buffer  **************/

/* At any one time, a channel is either on or off.  When it's on, its magnitude
 * is equal to the volume.  This is a 4-bit value corresponding to a graph
 * in the chip users' manual.  I have derived the following calc program
 * to match the graph (using 2^13-1 as the max, because I need to combine
 * 4 channels into a 16-bit signed value):
 *   for(i=1; i<16; i++) printf("%d, ", round((sqrt(2)/2)^(15-i)*(2^13-1))) */
/* Here's a precomputed volume scaler using the above calc output, slightly
 * hand-edited: */
static const uint16_t logvol[] = {
    0, 64, 90, 128, 181, 256, 362, 512,
    724, 1024, 1448, 2048, 2896, 4095, 5792, 8191
};

/* Update chip by 1 sound clock */
/* Just updates chip_output */
static void update_chip(void)
{
    /* update clock dividers */
    /* env supposedly updates 1/16 as fast, but that's actually a side
     * effect of not doing anything until the 4-bit env vol counter ticks */
    int ticked[5];
    for(int i = 0; i < 5; i++) {
	/* If channel "turned off", never tick; stop counting */
	if(!(ticked[i] = i < 3 ? snd_state.reg[8+i] : 1))
	    continue;
	/* I don't know what to do if per == 0 */
	/* For now, I'll assume "use max+1" */
	/* I used to assume "stop clock"; invert next to #ifs for that */
#if 1
	if((ticked[i] = !snd_state.cnt[i]))
#else
	if((ticked[i] = !snd_state.cnt[i] && snd_state.per[i]))
#endif
	{
	    int per = snd_state.per[i];
#if 1
	    /* convert period 0 -> per == max+1 */
	    if(!per)
		per = i < 3 ? 0x1000 : i == 3 ? 0x20 : 0x10000;
#endif
	    snd_state.cnt[i] = per;
	} else
	    snd_state.cnt[i]--;
    }

    /* update noise.  */
    int noise = snd_state.noise;
    if(ticked[3]) {
	// noise algorithm, from ym2149.sv
	// no idea where this comes from.
	// it'd be nice to have an official reference.
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

    /* finally, compute output sample for this SND_CLOCK period */
    int16_t output = 0;
    unsigned mix = snd_state.reg[7];
    for(int i = 0; i < 3; i++, mix >>= 1) {
	uint16_t vol = snd_state.reg[8+i];
	if(vol & 0x10)
	    vol = evol;
	if(!vol)
	    continue;
	int on = 0;
	/* I used to capture per/2 into hper when loading cnt to decide when
	 * to switch but per shouldn't change frequently enough for glitches
	 * to matter */
	/* I used to generate 2 samples to compensate for counter
	 * crossovers with odd periods happening mid-sample, but I doubt
	 * the original hardware cares, and at worst that's a 1 sound clock
	 * difference.  The main reason I did that is so setting period to
	 * 1 doesn't result in a continuous value, but 1 is way too fast for
	 * human hearing, anyway, so who cares? */
	if(!(mix & 1))
	    /* > vs. >= is 1 sound clock difference (5us).  Who cares? */
	    on = snd_state.cnt[i] >= snd_state.per[i] / 2;
	/* I suspect mixing means OR (+ w/ saturation).  The verilog seems
	 * to confirm this. */
	if(!(mix & 8))
	    on |= noise;
	/* Mixing on the output is done with pure addition, though */
	output += on ? logvol[vol] : 0;
    }
    snd_state.chip_output = output;
}

/* Sample rate conversion between CPU clock (CLKRATE) times and
 * sample_rate times.  Numbers are absolute offsets from beginning of time */
static unsigned long sample_to_clk(unsigned long sample)
{
    return (sample * CLKRATE + sample_rate - 1) / sample_rate; /* ceil */
}

/* len = total length, in CPU clocks, of this sample (varies by +/- 1) */
/* off = offset of this clock within the sample */
static unsigned long clk_to_sample(unsigned long clk, unsigned long *off,
				   unsigned long *len)
{
    unsigned long cur = clk * sample_rate / CLKRATE; /* floor */
    unsigned long clk0 = sample_to_clk(cur), clk1 = sample_to_clk(cur + 1);
    if(off)
	*off = clk - clk0;
    if(len)
	*len = clk1 - clk0;
    return cur;
}

int _vol_shift;

/* merge sample using current state */
/* constant value starts at last_update and lasts until to */
static void merge_sample(unsigned long to)
{
    if(_turbo) {
	/* Don't generate sound; just fast forward time */
	snd_state.last_update = to;
	/* And reset buffer pointers to 0 */
	if(snd_state.sample_start) {
	    snd_state.sample_start = 0;
	    pthread_mutex_lock(&sample_lock);
	    write_sample = read_sample = 0;
	    pthread_cond_signal(&sample_ready);
	    pthread_mutex_unlock(&sample_lock);
	}
	return;
    }
    /* The constant update value is sum of chip and bit banging */
    int16_t snd = snd_state.chip_output;
    if(snd_state.abit_on)
	snd += logvol[15]; // merge in bit bang
    // adjusted by global volume
    snd >>= _vol_shift;

    if(!snd_state.sample_start)
	snd_state.sample_start = snd_state.last_update;
    else if(snd_state.last_update - snd_state.sample_start > clk_sync)
	snd_state.sample_start += clk_sync;
    while(snd_state.last_update < to) { // repeat if sample crosses buffer boundary
	unsigned long start_clk = snd_state.last_update - snd_state.sample_start,
	              end_clk = to - snd_state.sample_start;
	unsigned long start_sample, start_part, start_part_len,
	              end_sample, end_part;
	start_sample = clk_to_sample(start_clk, &start_part, &start_part_len);
	end_sample = clk_to_sample(end_clk, &end_part, NULL);
	unsigned long ptr_adj = (start_sample / sample_data_len) * sample_data_len;
	start_sample -= ptr_adj;
	end_sample -= ptr_adj;
	if(end_sample >= sample_data_len) {
	    end_sample = sample_data_len;
	    end_part = 0;
	    end_clk = sample_to_clk(end_sample + ptr_adj);
	}
	snd_state.last_update += end_clk - start_clk;
	pthread_mutex_lock(&sample_lock);
	if(start_sample < read_sample && end_sample > read_sample) {
	    unsigned lost_clocks = (to - snd_state.sample_start) -
		                   sample_to_clk(ptr_adj + read_sample);
#if SHOW_UORUN
 	    overruns += lost_clocks;
	    static unsigned long last_overrun = 0;
	    if(overruns - last_overrun > CLKRATE / 100) {
		fputs("overrun 10ms\n", stderr);
		last_overrun += CLKRATE / 100;
	    }
#endif
	    /* For now, just drop data */
	    snd_state.sample_start -= lost_clocks;
	    snd_state.last_update = to;
	    end_sample = read_sample;
	    end_part = 0;
	}
	pthread_mutex_unlock(&sample_lock);
	// partial sample @ start
	// should only happen if there was a partial sample @prev. end.
	if(start_part) {
	    unsigned ps_end = end_sample > start_sample ? start_part_len : end_part;
	    snd_state.partial_sample += (ps_end - start_part) * snd;
	    if(ps_end == start_part_len)
		sample_data[start_sample++] = snd_state.partial_sample / start_part_len;
	    else
		end_part = 0; /* already took care of end part */
	}
	// full samples
	while(start_sample < end_sample)
	    sample_data[start_sample++] = snd;
	write_sample = start_sample;
	pthread_mutex_lock(&sample_lock);
	// FIXME: This should never be necessary, and should probably
	// send signals less often.
	if(snd_state.last_update == to &&
	   (write_sample - read_sample)%sample_data_len >
	    (sample_data_len / (OUTPUT_DELAY + 2)))
	    pthread_cond_signal(&sample_ready);
	pthread_mutex_unlock(&sample_lock);
	// partial sample @ end
	if(end_part)
	    snd_state.partial_sample = snd * end_part;
    }
}

/* Update sound state to _totclk */
/* Main loop; call repeatedly */
void aqsnd_process(void)
{
    write_event(EV_PROCESS + (snd_state.last_update << 4));
    if(snd_state.last_snd_clock) {
	while(snd_state.last_snd_clock <= _totclk) {
	    merge_sample(snd_state.last_snd_clock);
	    update_chip();
	    snd_state.last_snd_clock += 32; // 16 DEV_CLK/SND_CLK 2 CPU_CLK/DEV_CLK
	}
    }
    if(!snd_state.last_update) // no sound yet
	return;
    merge_sample(_totclk);
    if(snd_state.tape_active && snd_state.last_tapeclk < _totclk - TAPE_ZERO * 8)
	end_tape();
    write_event(EV_PROCESS + (snd_state.last_update << 4));
}

/***********  Reset/Init  **************/

static void disable_sound(void);

void aqsnd_reset(void)
{
    ev_init();
    end_tape(); // close files if open, before zeroing them
    memset((void *)&snd_state, 0, sizeof(snd_state));
    /* note: ym2149.sv sets reg7 = 0xff, but official docs say 0 */
    /* doesn't matter much, since setting reg9..11 to 0 disables output */
    pthread_mutex_lock(&sample_lock);
    write_sample = read_sample = 0;
    pthread_cond_signal(&sample_ready);
    pthread_mutex_unlock(&sample_lock);
    disable_sound();

    /* Aquarius always beeps on startup, so may as well start sound right away */
    /* Used to wait until first sound generated */
    enable_sound();
}

/***********  Play sound buffer  **************/

/* This should be the only code that is SDL dependent */

static int dev_init = 0;
SDL_AudioDeviceID devid;

static void send_sound(_U void *userdata, Uint8 *out, int len)
{
    int nsamp = len / sizeof(int16_t);
    write_event(EV_SNDCB | (nsamp << 4));

    /* PortAudio says buffer len can vary from call to call, but SDL2 makes
     * no such claim. */
    /* As a side effect of this check, samples never cross sample_data_len */
    if(sample_data_len % nsamp) {
	/* already printed buffer len to ilog */
	fprintf(stderr, "?? inconsistent sound buffer len %d\n", len);
	dev_init = 2;
    }
    if(dev_init != 1) {
	write_event(EV_SNDCB | (-1<<4));
	memset(out, 0, len);
	return;
    }
    /* Output all 0 if muted or sound not yet started */
    /* Continue for at least OUTPUT_DELAY buffer lengths after start/unmute */
    static int is_off = 0;
    int was_off = is_off;
    is_off = _turbo || !snd_state.last_update ? OUTPUT_DELAY : 0;
    if(!is_off && was_off)
	is_off = was_off - 1;
    if(is_off || was_off) {
	write_event(EV_SNDCB | (is_off << 4));
	memset(out, 0, len);
	return;
    }

    /* First, check if enough data available.  Underrun otherwise. */
    pthread_mutex_lock(&sample_lock);
    unsigned off = read_sample, eoff = off + nsamp;
#if SHOW_UORUN
    if(write_sample > off && eoff > write_sample)
	underruns += eoff - write_sample;
    static unsigned long last_underrun = 0;
    if(underruns - last_underrun > sample_rate / 100) {
	fputs("underrun 10ms\n", stderr);
	last_underrun += sample_rate / 100;
    }
#endif
    while(write_sample > off && eoff > write_sample) {
	if(_turbo || !snd_state.last_update) {
	    pthread_mutex_unlock(&sample_lock);
	    memset(out, 0, len);
	    write_event(EV_SNDCB);
	    return;
	}
	pthread_cond_wait(&sample_ready, &sample_lock);
	// in case read_sample was forced back to 0...
	off = read_sample;
	eoff = off + nsamp;
    }
    pthread_mutex_unlock(&sample_lock);
    memcpy(out, sample_data + off, (eoff - off) * sizeof(*sample_data));
    read_sample = eoff == sample_data_len ? 0 : eoff;
    write_event(EV_SNDCB | (nsamp*1000000/sample_rate << 4));
}

/* Euclid's algorithm -- why isn't this in libc? */
static unsigned long gcd(unsigned long x, unsigned long y)
{
  unsigned long r;

  while((r = x % y)) {
    x = y;
    y = r;
  }
  return y;
}

static void enable_sound(void)
{
    if(dev_init == 2) /* Don't retry on errors */
	return;
    if(!dev_init) {
	dev_init = 2; /* If I just return, there as an error */
	SDL_Init(SDL_INIT_AUDIO);
	static struct SDL_AudioSpec spec;
	memset(&spec, 0, sizeof(spec));
	spec.format = AUDIO_S16SYS;
	spec.channels = 1;
	spec.callback = send_sound;
	devid = SDL_OpenAudioDevice(NULL, 0, &spec, &spec,
				    SDL_AUDIO_ALLOW_FREQUENCY_CHANGE|
				    SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
	if(devid <= 0) {
	    fputs("Unable to open audio device.\n", stderr);
	    perror(SDL_GetError());
	    return;
	}
	const char *dname = SDL_GetAudioDeviceName(devid, 0);
	if(_ilog)
	    fprintf(_ilog, "Opened SDL audio %d (%s) rate %d samples %d (%d)\n",
		    devid, dname ? dname : "?", spec.freq, spec.samples, spec.size);

	/* This needs to be at least 3ms.  Given the SDL docs, I don't
	 * think it's possible to be less than 3ms, so I don't check. */
	sample_data_len = (OUTPUT_DELAY + 2) * spec.samples;
	sample_data = malloc(sample_data_len * sizeof(*sample_data));
	if(!sample_data) {
	    perror("sample data");
	    SDL_CloseAudioDevice(devid);
	    return;
	}
	sample_rate = spec.freq;
	clk_sync = sample_rate * CLKRATE / gcd(sample_rate, CLKRATE);
	dev_init = 1;
    }
    SDL_PauseAudioDevice(devid, 0);
}

static void disable_sound(void)
{
    if(dev_init > 0)
	SDL_PauseAudioDevice(devid, 1);
}
