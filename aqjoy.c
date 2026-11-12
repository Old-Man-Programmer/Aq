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

/*
 * Since SDL2 refuses to work for me, I have written this using the
 * Linux event driver.  I have no intention of ever supporting raw
 * HID devices.  I continue to not want to write a configuration
 * file for this, so if you want different behavior, use an external
 * remapper utility (like my joy-remap).
 * 1) Only the first controller and second controller it finds are used.
 *    Since I use analog sticks for directions, I check for the presence of
 *    ABS_X, ABS_Y, and BTN_A, and nothing else.  This means you'll need
 *    a remapper for joysticks that give BTN_TRIGGER, for example.
 * 2) The mapping is hard-coded to event codes (ABS_* for axes, BTN_* for buttons):
 *     Stick #1:
 *       dir = X/Y
 *       buttons: 1=Y 2=X 3=TR (Linux has X/Y reversed from SDL/xbox)
 *                4=A 5=B 6=TR2/axRZ
 *     Stick #2:
 *       dir = RX/RY
 *       buttons: 1=HAT0X- 2=HAT0Y+ 3=TL
 *                4=HAT0Y- 5=HAT0X+ 6=TR/axZ
 *    If there is a second controller, the sticks are reversed.
 * 3) Hot plugging is not supported.  Sorry about that if you have a flaky
 *    connection (e.g. some BT devices disconnect occasionally, and Linux
 *    is poorly equipped to handle that).  It only checks for/opens the
 *    devices once, on startup.
 */

/* I thought I could just read events whenever Aq needed them, but I
 * seem to be losing events that way.  For now, I use a thread to poll
 * continuously.  I suppose I could use a SIGIO handler instead, but
 * this is easier, and probably more reliable as well, given my lack
 * of success with SIGIO in the deep, dark past.
 */
#define USE_THREADS 1

#include "Aq.h"
#include <linux/input.h>
#include <math.h>
#if USE_THREADS
#include <pthread.h>
#include <poll.h>
#endif

/* lifted from joy-remap.c */
/* macros for accessing bits in arrays of unsigned longs */
/* stupid kernel doesn't export its bitops, so everybody has to reimplement */
/* these probably only work on little-endian, but that's ok for now */
#define ULBITS (sizeof(unsigned long)*8)
#define MINBITS(x) (((x) + ULBITS - 1)/ULBITS)
#define ULSET(bits, bit) do { \
    int _b = bit; \
    (bits)[_b/ULBITS] |= 1UL << _b % ULBITS; \
} while(0)
#define ULCLR(bits, bit) do { \
    int _b = bit; \
    (bits)[_b/ULBITS] &= ~(1UL << _b % ULBITS); \
} while(0)
#define ULISSET(bits, bit) ((bits)[(bit)/ULBITS] & (1UL << (bit) % ULBITS))

static int read_abs_init(int fd, uint16_t code, int *add, int *mul, int *val);
static uint8_t chkax(int x, int y);

static int padfd[2];
static unsigned long keymask[MINBITS(KEY_MAX)];
static unsigned long absmask[MINBITS(ABS_MAX)];
static int absadd[2*6], absmul[2*6], absval[2*6], hatadd[2], hatmul[2];
static uint16_t keyval[2];

// Vritual hat buttons
#define BTN_HSOUTH  (BTN_TR2 + 1)
#define BTN_HEAST   (BTN_TR2 + 2)
#define BTN_HNORTH  (BTN_TR2 + 3)
#define BTN_HWEST   (BTN_TR2 + 4)

static void setkey(int n, uint16_t k, int v)
{
    uint16_t m = 1 << (k - BTN_A);
    if(v)
	keyval[n] |= m;
    else
	keyval[n] &= ~m;
}

static void hatkey(int n, uint16_t code, int val)
{
    if(val > 16384)
	setkey(n, code == ABS_HAT0X ? BTN_HEAST : BTN_HSOUTH, 1);
    else if(val < -16384)
	setkey(n, code == ABS_HAT0X ? BTN_HWEST : BTN_HNORTH, 1);
    else
	return;
}

static int proc_events(int n)
{
    static struct input_event ev[10];
    while(1) {
	int nr = 0;
	char *buf = (char *)&ev;
	while(nr < (int)sizeof(ev)) {
	    int r = read(padfd[n], buf, sizeof(ev) - nr);
	    if(!r || (r < 0 && errno == EAGAIN))
		break;
	    if(r < 0) {
		perror("reading events");
		return 0;
	    }
	    nr += r;
	    buf += r;
	}
	if(!nr)
	    return 1;
	for(int i = 0; i < nr / (int)sizeof(ev[0]); i++) {
	    uint16_t code = ev[i].code;
	    if(ev[i].type == EV_KEY) {
		if(code >= BTN_A && code <= BTN_TR2) // $130..$139
		    setkey(n, code, ev[i].value);
	    } else if(ev[i].type == EV_ABS) {
		if(/* code >= ABS_X && */ code <= ABS_RZ) {  // 0 .. 5
		    int val = (ev[i].value + absadd[code + n * 6]) *
			      absmul[code + n * 6];
		    absval[code + n * 6] = val;
		    if(code == ABS_Z || code == ABS_RZ)
			setkey(n, (code == ABS_Z) + BTN_TL2, val > 16384);
		} else if(code == ABS_HAT0X || code == ABS_HAT0Y) {
		    keyval[n] &= ~(15 << (BTN_HSOUTH - BTN_A));
		    hatkey(n, code, (ev[i].value + hatadd[n]) * hatmul[n]);
		}
	    }
	}
    }
}

#if USE_THREADS
static pthread_mutex_t readjoy_lock = PTHREAD_MUTEX_INITIALIZER;

static void *event_task(_U void *udata)
{
    static struct pollfd p[2];
    p[0].fd = padfd[0];
    p[1].fd = padfd[1];
    p[0].events = p[1].events = POLLIN | POLLHUP;
    while(1) {
	pthread_mutex_lock(&readjoy_lock);
	if(!proc_events(0) || (padfd[1] >= 0 && !proc_events(1))) {
	    pthread_mutex_unlock(&readjoy_lock);
	    close(padfd[0]);
	    if(padfd[1] >= 0)
		close(padfd[1]);
	    padfd[0] = -1;
	    return NULL;
	}
	pthread_mutex_unlock(&readjoy_lock);
	poll(p, padfd[1] < 0 ? 1 : 2, -1);
    }
}
#endif

uint8_t aqjoy_read(uint8_t port)
{
    if(port < 14 || port > 15)
	return 0;
    static int init = 0;
    if(!init) {
	padfd[0] = padfd[1] = -1;
	memset(&keyval, 0, sizeof(keyval));
	memset(&absadd, 0, sizeof(absadd));
	memset(&absmul, 0, sizeof(absmul));
	memset(&absval, 0, sizeof(absval));
	memset(&hatadd, 0, sizeof(hatadd));
	for(int i = 0; i < 32; i++) {
	    char ev[20];
	    sprintf(ev, "/dev/input/event%d", i);
	    int fd = open(ev, O_RDONLY | O_NONBLOCK);
	    if(fd < 0)
		continue;
	    if(ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absmask)), absmask) < 0 ||
	       ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keymask)), keymask) < 0) {
		close(fd);
		continue;
	    }
	    if(!ULISSET(absmask, ABS_X) || !ULISSET(absmask, ABS_Y) ||
	       !ULISSET(keymask, BTN_SOUTH)) {
		close(fd);
		continue;
	    }
	    int n = padfd[0] >= 0;
	    /* Grab initial state of buttons */
	    /* Assumes that 0 is returned for keys that don't exist */
	    if(ioctl(fd, EVIOCGKEY(sizeof(keymask)), keymask) < 0) {
		perror("getting initial state");
		close(fd);
		continue;
	    }
	    for(int k = BTN_SOUTH; k <= BTN_TR2; k++)
		if(ULISSET(keymask, k))
		    keyval[n] |= 1 << (k - BTN_SOUTH);
	    /* Grab initial state and adjustmment factors of axes */
	    int a;
	    for(a = ABS_X; a <= ABS_RZ; a++) {
		if(!read_abs_init(fd, a, &absadd[a + n * 6],
				  &absmul[a + n * 6], &absval[a + n * 6]))
		    break;
		if(a == ABS_Z || a == ABS_RZ) {
		    int val = absval[a + n * 6];
		    setkey(n, (a == ABS_Z) + BTN_TL2, val > 16384);
		}
	    }
	    if(a <= ABS_RZ) {
		perror("getting axis limits");
		close(fd);
		continue;
	    }
	    for(a = ABS_HAT0X; a <= ABS_HAT0Y; a++) {
		int val;
		/* assumes hatX and hatY have same adj. factors */
		if(!read_abs_init(fd, a, &hatadd[n], &hatmul[n], &val))
		    break;
		hatkey(n, a, val);
	    }
	    if(_ilog) fprintf(_ilog, "Found controller %s\n", ev);
	    if(padfd[0] < 0)
		padfd[0] = fd;
	    else {
		padfd[1] = fd;
		break;
	    }
	}
#if USE_THREADS
	pthread_t thr;
	static pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if(pthread_create(&thr, NULL, event_task, NULL)) {
	    perror("js poll thread");
	    close(padfd[0]);
	    if(padfd[1] >= 0)
		close(padfd[1]);
	    padfd[0] = padfd[1] = -1;
	}
#endif
	init = 1;
    }
    if(padfd[0] < 0)
	return 0xff;
#if USE_THREADS // see comment above about losing events
    pthread_mutex_lock(&readjoy_lock);
#else
    if(!proc_events(0) || (padfd[1] >= 0 && !proc_events(1))) {
	close(padfd[0]);
	if(padfd[1] >= 0)
	    close(padfd[1]);
	init = 0; // this will probably fail to reopen: not exactly hotplug
	return 0xff;
    }
#endif
    uint8_t ret1 = 0xff, ret2 = 0xff;
    const uint8_t butmask[] = {
	0xbf, 0x7b, 0x5f, 0xdf, 0x7d, 0x7e
    };
    const unsigned butmap[] = {
	BTN_WEST,   BTN_NORTH,  BTN_TR, BTN_SOUTH,  BTN_EAST,   BTN_TR2,
	BTN_HWEST,  BTN_HNORTH, BTN_TL, BTN_HSOUTH, BTN_HEAST,  BTN_TL2
    };
    for(int i = 0; i < 6; i++) {
 #define bm(n, x) (keyval[n] & (1 << (butmap[x] - BTN_SOUTH)) ? butmask[x % 6]: 0xff)
	ret1 &= bm(0, i) & bm(1, i + 6);
	ret2 &= bm(1, i) & bm(0, i + 6);
    }
    int x1, y1, x2 = 0, y2 = 0;
    x1 = absval[ABS_X];
    y1 = absval[ABS_Y];
    if(padfd[1] >= 0) {
	x2 = absval[ABS_X + 6];
	y2 = absval[ABS_Y + 6];
	if(!x1 && !y1) {
	    x1 = absval[ABS_RX + 6];
	    y1 = absval[ABS_RY + 6];
	}
    }
    if(!x2 && !y2) {
	x2 = absval[ABS_RX];
	y2 = absval[ABS_RY];
    }
    ret1 &= chkax(x1, y1);
    ret2 &= chkax(x2, y2);
    pthread_mutex_unlock(&readjoy_lock);
    return port == 14 ? ret1 : ret2;
}

static int read_abs_init(int fd, uint16_t code, int *add, int *mul, int *val)
{
    static struct input_absinfo ai;
    if(!ULISSET(absmask, code))
	return 1;
    if(ioctl(fd, EVIOCGABS(code), &ai) < 0)
	return 0;
    if(ai.minimum == ai.maximum)
	return 1;
    *mul = 65536 / (ai.maximum - ai.minimum);
    *add = -ai.minimum;
    if(code != ABS_Z && code != ABS_RZ)
	*add -= (ai.maximum - ai.minimum) / 2;
    else if(ai.value != ai.minimum && ai.value != ai.maximum)
	/* I'll only recognize as a trigger if it's currently
	 * unpressed or fully depressed.  If it's "centered"
	 * it's probably not a trigger */
	*add = *mul = 0;
    *val = (ai.value + *add) * *mul;
    return 1;
}

static uint8_t chkax(int x, int y)
{
    const uint8_t axmap[] = { // E ESE ... ENE
	0xfd, 0xed, 0xec, 0xfc, 0xfe, 0xee, 0xe6, 0xf6,
	0xf7, 0xe7, 0xe3, 0xf3, 0xf8, 0xeb, 0xe9, 0xf9
    };
    if(x > -8192 && x < 8192 && y > -8192 && y < 8192)
	return 0xff;
    /* neg == left/up, so y -> -y */
    double th = x ? atan((double)-y/x) / M_PI * 16 : -y < 0 ? -8.0 : 8.0;
    if(x < 0)
	th = 16.0 + th; // -8 .. 24
//    double th = acos((double)x/sqrt(x*x+y*y)) / M_PI * 16; // 0 .. 16
//    if(-y < 0)
//	th = 32.0 - th; // 0 .. 32
    int ith = (int)th;
    ith = ((ith + 1) / 2) & 15; // 0 .. 15
//  if(_ilog)  fprintf(_ilog, "stick x=%d, y=%d angle=%d\n", x, y, ith);
    return axmap[ith];
}
