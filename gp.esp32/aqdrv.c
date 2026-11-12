/* $Copyright: $
 * Copyright (c) 2022-2025 by Thomas Moore (darktjm@gmail.com)
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
 * uinput driver for Aquarius controller on serial port
 * copied and nearly rewritten from my old indrv remapper
 * Written and placed in the Public Domain 2022 by Thomas J. Moore
 *
 * Protocol supported is "cooked".  Originally it was going to be
 * "raw", but may as well use the ESP32's CPU to do the translation.
 *
 * Protocol:
 *
 * 'a' -> produce the string "\r\nAquarius\r\n"
 * 'r' -> produce the string "\r=dd", where dd is the hex value on the port
 * 'c' -> produce the string "\rxXXyYYBBBBBB", where XX/YY is hex value for
 *        direction (00..FF, 80=center) and B is 1/2/3/4/5/6 or space in
 *        given position to indicate button presses.
 *
 * Command-line parameters: type and/or connection string.
 *   Type a = map to Aq emulator port A
 *     X Y / X Y TR A B TR2  (left stick, right buttons)
 *   Type b = map to Aq emulator port B
 *     Z Rz / DLeft DUp TL DDown DRight TL2 (right stick, left "buttons")
 *   No type (default) is to map to 1st 2 axes and 1st 6 buttons.
 *     X Y / A B C X Y Z
 *   Connection string is device name or host[:port].  If device name can't
 *   be opened, it's host[:port].  Device name will have /dev/ and /dev/tty
 *   prepended before giving up.  Port is 28010 if not specified.  No
 *   connection string uses /dev/ttyACM0.  A connection string of - uses
 *   stdio.  If connection not available (non-existent or aborted), retries
 *   every 5 seconds (unless stdio).
 *
 * TODO:
 *   Support * at end of device name to scan all devices.  Need a way to
 *     filter other than "can I open it, and does it respond to 'a'?"
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>

#include <linux/input.h>
#include <linux/uinput.h>

#define wioctl(a, b, c) if(ioctl(a, b, c) < 0) { perror(#b); exit(1); }

static const int mapto[3 * 6] = {
    BTN_A, BTN_B, BTN_C,  BTN_X, BTN_Y, BTN_Z,   /* default mapping */
    BTN_X, BTN_Y, BTN_TR, BTN_A, BTN_B, BTN_TR2, /* Aq port A */
    -1,    -2,    BTN_TL, -3,    -4,    BTN_TL2  /* Aq port B (-x == hat) */
};

static int ifd, ofd;

static void writec(char c)
{
    while(1) {
	int len = write(ofd, &c, 1);
	if(len == 1)
	    return;
	if(errno != EINTR && errno != EAGAIN) {
	    perror("write");
	    exit(1);
	}
    }
}

static int readc(void)
{
    while(1) {
	char ibuf;
	fd_set fds;
	struct timeval to = { 1, 0 };
	FD_SET(ifd, &fds);
	int sel = select(ifd + 1, &fds, NULL, NULL, &to);
	if(sel < 0) {
	    if(errno == EINTR || errno == EAGAIN)
		continue;
	    perror("read");
	    exit(1);
	}
	if(!sel)
	    return -1;
	if(read(ifd, &ibuf, 1) < 0) {
	    perror("read");
	    exit(1);
	}
	return (int)(unsigned char)ibuf;
    }
}

static void cmd_expectl(char cmd, const char *str, int len)
{
    const char *ip = str;
    writec(cmd);
    while(ip != str + len) {
	int c = readc();
	if(c < 0) {
	    /* I'd abort after 5 tries or so, but I'm lazy */
	    /* Just abort w/ ctrl-C or kill */
	    fputs("Timeout wating for Aquarius ID\n", stderr);
	    writec(cmd);
	    continue;
	}
	if(c == *ip)
	    ip++;
	else if(c == '\r' && *ip == '\n')
	    /* The default newlib UART inserts \r */
	    /* NEWLINE_STDOUT_LINE_ENDING fixes this */
	    /* Disabling this behavior screws up log messages */
	    ;
	else
	    ip = c == *str ? str + 1 : str;
    }
}
#define cmd_expect(c, s) cmd_expectl(c, s, sizeof(s) - 1)

static void write_ev(int t, int fd, int ev, int val)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct input_event iev[2] = {
	{ .time = tv, .type = t, .code = ev, .value = val },
	{ .time = tv, .type = EV_SYN, .code = SYN_REPORT, .value = 0 }
    };
    int wlen;
    while((wlen = write(fd, &iev, sizeof(iev))) <= 0 &&
	  (errno == EINTR || errno == EAGAIN));
    if(wlen != sizeof(iev)) {
	perror("event");
	exit(1);
    }
}

#define write_abs(fd, ev, val) write_ev(EV_ABS, fd, ev, val)
#define write_key(fd, ev, val) write_ev(EV_KEY, fd, ev, !!(val))

static const char *prog;

static void help(void)
{
    fprintf(stderr, "%s: Linux uinput driver for esp32-aqctl\n"
	    "Usage: %s [a|b] [connect]\n"
	    " Input mapping is either:\n"
	    "   Dir = X/Y, Buttons = A B C X Y Z (default)\n"
	    "   Dir = X/Y, Buttons = X Y RT A B RT2 (A) (Aq emulator port A)\n"
	    "   Dir = Z/RZ (right stick), Buttons = HL HU LT HD HR LT2 (B) (Aq emulator port B)\n"
	    "         H = Hat (digital direction).\n"
	    "  Connect string is either a device name or host:port\n"
	    "    First, a device is tried, using the raw string, or the\n"
	    "    raw string with /dev/ or /dev/tty prepended.  If no such\n"
	    "    device exisits, the string is assumed to be a host/port.\n"
	    "    Use - for stdin+stdout.\n",
	    prog, prog);
    exit(1);
}

int main(int argc, const char * const * argv)
{
    int type = 0;
    prog = argv[0];
    const char *dev = NULL;
    while(argc > 1) {
	if(!**++argv)
	    help();
	argc--;
	if(!(*argv)[1]) {
	    char c = tolower(**argv);
	    if(c == 'a' || c == 'b') {
		type = c - 'a' + 1;
		continue;
	    }
	}
	if(dev)
	    help();
	dev = *argv;
    }
    do {
	static char dbuf[1024];
	if(!dev)
	    ifd = ofd = open("/dev/ttyACM0", O_RDWR);
	else if(dev[0] == '-' && !dev[1]) {
	    ifd = 0;
	    ofd = 1;
	} else {
	    ifd = ofd = open(dev, O_RDWR);
	    if(ifd < 0) {
		strcpy(dbuf, "/dev/");
		strcpy(dbuf + 5, dev);
		ifd = ofd = open(dbuf, O_RDWR);
	    }
	    if(ifd < 0) {
		strcpy(dbuf + 5, "tty");
		strcpy(dbuf + 8, dev);
		ifd = ofd = open(dbuf, O_RDWR);
	    }
	    if(ifd < 0) {
		uint16_t port = 28010;
		strcpy(dbuf, dev);
		char *p = strrchr(dbuf, ':');
		if(p) {
		    *p++ = 0;
		    port = atoi(p);
		}
		struct hostent *he = gethostbyname(dbuf);
		if(he && he->h_addr_list[0] && he->h_addrtype == AF_INET) {
		    unsigned long opt = -1;
		    ifd = ofd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		    if(ifd < 0 ||
		       setsockopt(ifd, SOL_SOCKET, SO_REUSEADDR,
				  (void *)&opt, sizeof(opt))) {
			perror("socket");
			exit(1);
		    }
		    struct sockaddr_in addr = {
			.sin_family = AF_INET, .sin_port = htons(port)
		    };
		    char **a;
		    for(a = he->h_addr_list; *a; a++) {
			memcpy(&addr.sin_addr, *a, he->h_length);
			if(!connect(ifd, (struct sockaddr *)&addr, sizeof(addr)))
			    break;
		    }
		    if(!*a) {
			close(ifd);
			ifd = ofd = -1;
		    }
		}
	    }
	}
	if(ifd < 0) {
	    sleep(5);
	    continue;
	}
	/* FIXME: probably need to save for stdin/stdout to restore on exit */
	struct termios tinfo;
	if(!tcgetattr(ifd, &tinfo)) {
	    tinfo.c_iflag = IGNBRK;
	    tinfo.c_oflag = 0;
	    tinfo.c_cflag = CS8 | CREAD | CLOCAL;
	    tinfo.c_lflag = 0;
	    memset(tinfo.c_cc, 0, sizeof(tinfo.c_cc));
	    /* Note: B115200 is non-POSIX.  Oh well. */
	    /* Technically, this should be ignored by rfcomm and socket */
	    cfsetispeed(&tinfo, B115200);
	    cfsetospeed(&tinfo, B115200);
	    tcsetattr(ifd, TCSANOW, &tinfo);
	    tcsetattr(ofd, TCSANOW, &tinfo);
	}

	cmd_expect('a', "\r\nAquarius\r\n");

	int idev = open("/dev/input/uinput", O_RDWR);
	if(idev < 0 && errno == ENOENT)
	    idev = open("/dev/uinput", O_RDWR);
	if(idev < 0) {
	    perror("uinput");
	    exit(1);
	}
	struct uinput_setup is = {};
	strcpy(is.name, "Aquarius Controller");
	is.id.bustype = BUS_USB;
	/* I am not a vendor and have no product.  Just use X-Box Controller. */
	is.id.vendor = 0x16C0;
	is.id.product = 0x05DF;
	is.id.version = 0x100;
	wioctl(idev, UI_DEV_SETUP, &is);
	wioctl(idev, UI_SET_EVBIT, EV_SYN);
	wioctl(idev, UI_SET_EVBIT, EV_KEY);
	wioctl(idev, UI_SET_EVBIT, EV_ABS);

	const int *bmap = mapto + 6 * type;
	for(int i = 0; i < 6; i++)
	    if(bmap[i] > 0)
		wioctl(idev, UI_SET_KEYBIT, bmap[i]);
	if(type == 2) /* SDL2 requires A button to recognize gamepad */
	    wioctl(idev, UI_SET_KEYBIT, BTN_A);
	struct uinput_abs_setup as = {
	    .absinfo = {
		.minimum = 1,
		.maximum = 255,
		.value = 128
	    }
	};
	if(type != 2) {
	    as.code = ABS_X;
	    wioctl(idev, UI_ABS_SETUP, &as);
	    as.code = ABS_Y;
	    wioctl(idev, UI_ABS_SETUP, &as);
	} else {
	    as.code = ABS_Z;
	    wioctl(idev, UI_ABS_SETUP, &as);
	    as.code = ABS_RZ;
	    wioctl(idev, UI_ABS_SETUP, &as);
	    memset(&as, 0, sizeof(as));
	    as.absinfo.minimum = -1;
	    as.absinfo.maximum = 1;
	    as.code = ABS_HAT0X;
	    wioctl(idev, UI_ABS_SETUP, &as);
	    as.code = ABS_HAT0Y;
	    wioctl(idev, UI_ABS_SETUP, &as);
	}
	wioctl(idev, UI_DEV_CREATE, 0);
	uint8_t ox = 0x80, oy = 0x80, ob = 0;
	while(1) {
	    char buf[12], *p = buf;
	    usleep(1000000/60);
	    cmd_expect('c', "\rx");
	    for(int i = 0; i < 12; i++, p++) {
		int c = readc();
		if(c < 0)
		    break;
		*p = c;
	    }
	    if(p != buf + 12 || !isxdigit(buf[0]) || !isxdigit(buf[1]) ||
	       buf[2] != 'y' || !isxdigit(buf[3]) || !isxdigit(buf[4]) ||
	       buf[5] != 'b') {
		fputs("read error 1\n", stderr);
		continue;
	    }
	    p = buf + 6;
	    uint8_t nb = 0;
	    for(int i = 0; i < 6; i++, p++) {
		if(*p != ' ' && *p != '1' + i)
		    break;
		if(*p != ' ')
		    nb |= 1 << i;
	    }
	    if(p != buf + 12) {
		fputs("read error 2\n", stderr);
		continue;
	    }
	    buf[2] = buf[5] = 0;
	    uint8_t nx = strtol(buf, NULL, 16),
	        ny = strtol(buf + 3, NULL, 16);
	    if(nx != ox)
		write_abs(idev, type == 2 ? ABS_Z : ABS_X, (ox = nx));
	    if(ny != oy)
		write_abs(idev, type == 2 ? ABS_RZ : ABS_Y, 256 - (oy = ny));
	    if(nb == ob)
		continue;
	    int is_hatx = 0, is_haty = 0, hatx = 0, haty = 0;
	    for(int i = 0; i < 6; i++) {
		int n = nb & (1<<i), o = ob & (1<<i);
		if(n == o)
		    continue;
		if(bmap[i] > 0)
		    write_key(idev, bmap[i], n);
		else {
		    if(i == 0 || i == 4) {
			is_hatx = 1;
			if(n)
			    hatx = i == 0 ? -1 : 1;
		    } else {
			is_haty = 1;
			if(n)
			    haty = i == 1 ? 1 : -1;
		    }
		}
	    }
	    if(is_hatx)
		write_abs(idev, ABS_HAT0X, hatx);
	    if(is_haty)
		write_abs(idev, ABS_HAT0Y, haty);
	    ob = nb;
	}
    } while(ifd); /* no retries if using stdin/stdout */
}
