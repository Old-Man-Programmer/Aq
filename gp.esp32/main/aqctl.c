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
 * ESP32 Aquarius controller adapter
 * Written and placed in the Public Domain 2022 by Thomas J. Moore
 *
 * Compiled with ESP-IDF-5.0 (latest stable at time of writing).  See
 * README.md for further details; I'm not going to duplicate them here.
 *
 * Known issues:
 *   it takes way too long to check spiffs on startup, even w/ fresh/empty fs
 *   wifi connect() function doesn't wait for connection; need event handlers
 *   IP address endianness needs to be checked for all str<->num conversions
 *   Sometimes doesn't start up wifi.  Probably needs retry every 5s or so.
 *   Doesn't auto-connect BT initially.  This should actually "just work".
 */

/* needed for lots of things */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"

/* Standard C */
#include <string.h>
#include <stdint.h>
#include <ctype.h>
/* but apparently idf-5.0's ctype is broken.  Yay. */
#undef isdigit
#define isdigit(x) ((x)>='0' && (x)<='9')
#include <errno.h>

/***** GPIO *****/
/*
 * ESP32 board, USB connector up:
 * [3.3V]   [5V]
 * GND      GND
 * [2]cnf   13
 * [15]cnf  [12]cnf unless efuse
 * 4        14
 * 16       27
 * 17       26
 * [5]cnf   25
3* 18       33
 * 19       32
 * 21       [35]Pulldown input-only
 * [3]RX    [34]Pulldown input-only
 * [1]TX    [39]Pulldown input-only
 * 22       [36]Pulldown input-only
 * 23       [EN]
 */

#include "driver/gpio.h" /* High-level GPIO pin support */

/* My original plan was thwarted by accidentally mapping to GPIO1/3.  I moved
 * what had been there to GPIO 17/13.  All available GPIO goes to DB25 */
#define DB25_2  GPIO_NUM_17 /* Brown */
#define DB25_3  GPIO_NUM_22 /* Red */
#define DB25_4  GPIO_NUM_18 /* Orange */
#define DB25_5  GPIO_NUM_32 /* Yellow */
#define DB25_6  GPIO_NUM_19 /* Green */
#define DB25_7  GPIO_NUM_21 /* Black */
#define DB25_8  GPIO_NUM_23 /* Blue */
#define DB25_12 GPIO_NUM_26 /* Purple */
#define DB25_14 GPIO_NUM_16 /* Gray/Black */
#define DB25_15 GPIO_NUM_14 /* White */
#define DB25_16 GPIO_NUM_25 /* Black/White */
#define DB25_17 GPIO_NUM_4  /* Brown/White */
#define DB25_20 GPIO_NUM_13 /* Orange/Red */
#define DB25_22 GPIO_NUM_33 /* Pink */
#define DB25_24 GPIO_NUM_27 /* Cyan */

/* Since I only care about the DB9, any DB25 pins not mentioned below
 * can be used for other things */
/* In fact, now that I have determined that DB9_1 is GND, that could be
 * hard-wired to GND for an additional available GPIO (not enough for 2
 * controllers, though: 1 short.  Might be able to use 12 by setting efuse to
 * 3.3V, which is probably a good idea nayway.  Actually, I'm not sure why
 * I declared GPIO5 cfg but not GPIO13, but it might be available as well. */
#define DB9_1 DB25_8
#define DB9_2 DB25_3
#define DB9_3 DB25_2
#define DB9_4 DB25_20
#define DB9_5 DB25_7
#define DB9_6 DB25_6
#define DB9_7 DB25_4
#define DB9_8 DB25_5
#define DB9_9 DB25_22

#define PB(x) (1ULL<<(x))

static const int gpp[] = { DB9_1, DB9_2, DB9_3, DB9_4, DB9_5,
/* Return value indicates bottom 4 are reversed */
                           DB9_9, DB9_8, DB9_7, DB9_6 };
#define ALL_DB9 (PB(DB9_1)|PB(DB9_2)|PB(DB9_3)|PB(DB9_4)|PB(DB9_5)| \
                 PB(DB9_6)|PB(DB9_7)|PB(DB9_8)|PB(DB9_9))

/* All 9 pins were GPIO so I could figure out which one is GND (my original
 * tests showed no button response with hard-wired pin 5 to GND, so the
 * on-line references were wrong).  I experimented with a serial command
 * to set which pin gets GND, and found the best results with pin 1. */
/* Thus, DB9_1 is set to permanently output 0, and only 2..9 are read. */

static const gpio_config_t oconf = {
    .pin_bit_mask = PB(DB9_1),
    .mode = GPIO_MODE_OUTPUT_OD,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};
static const gpio_config_t iconf = {
    .pin_bit_mask = ALL_DB9&~PB(DB9_1),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE /* ANYEDGE? */
};

static void init_gpio(void)
{
    /* GPIO */
    ESP_ERROR_CHECK(gpio_config(&iconf));
    ESP_ERROR_CHECK(gpio_config(&oconf));
    ESP_ERROR_CHECK(gpio_set_level(DB9_1, 0));
    /* maybe some time in the future, always/only output when pins change */
    /* gpio_isr_register(fn, NULL, ESP_INTR_FLAG_DEFAULT, NULL); */
}

static int read_pad(void)
{
    int res = 0;
    for(int i = 1; i < 9; i++)
	res = (res << 1) | gpio_get_level(gpp[i]);
    return res;
}

/***** UART *****/
#define TXD_PIN (GPIO_NUM_1)
#define RXD_PIN (GPIO_NUM_3)

#include "driver/uart.h"
#include "esp_vfs_dev.h"
#if 0
#include <sys/termios.h>
#endif

static void serial_task(void *arg);
static void init_uart(void)
{
    /* UART0 is configured by bootloader as stdin/stdout */
    /* but it blocks by default, triggering WDT and making BT fail */
    /* This can sort of be fixed by "assigning a UART driver" */
    /* Even with this active, logging takes place by default on same port */
    uart_driver_install(UART_NUM_0, 2*1024, 0, 0, NULL, 0);
    esp_vfs_dev_uart_use_driver(0);
    /* Seems to output excess \r's */
#if 0 /* This doesn't work; CONFIG_NEWLIB_STDOUT_INE_ENDING_CRLF is only way */
    struct termios tio;
    tcgetattr(1, &tio);
    tio.c_oflag &= ~ONLCR;
    tcsetattr(1, TCSANOW, &tio);
#endif
    xTaskCreate(serial_task, "uart", 1024*3, (void *)0, configMAX_PRIORITIES, NULL);
}

/* Using unistd fd I/O for everything here.  Not sure if advantage or
 * disadvantage to using stdio instead, but I did get more crashes at first */
#include <unistd.h>

static int writes(int fd, const char *s, int len)
{
    if(!len)
	len = strlen(s);
    while(len) {
	int w = write(fd, s, len);
	if(w > 0) {
	    len -= w;
	    s += w;
	} else {
	    if(w < 0 && errno != EINTR && errno != EAGAIN)
		return len;
	    vTaskDelay(10);
	}
    }
    return 0;
}
#define writecs(fd, s) writes(fd, s, sizeof(s) - 1)
/* note: writec doesn't support error return */
#define writec(fd, c) do { \
    char _c = c; \
    writes(fd, &_c, 1); \
} while(0)

/* Note: SPP is non-blocking & no select() as of last time I looked. */
/* This means busy waiting.  Yay.  Only fix is to write my own vfs driver. */
static int readc(int ifd)
{
    static int wasesc = 0;
    while(1) {
	char c;
	int r;
	if((r = read(ifd, &c, 1)) == 1) {
	    if(c == 27)
		wasesc++;
	    else
		wasesc = 0;
	    if(wasesc == 3)
		esp_restart();
	    return (unsigned char)c;
	}
	if(r < 0 && errno != EINTR && errno != EAGAIN)
	    return -1;
	vTaskDelay(10);
    }
}

static int reads(int ifd, int ofd, char *buf, int size, int echo)
{
    char *buf0 = buf;
    while(1) {
	int c = readc(ifd);
	if(c < 0)
	    return -1;
	if(c == '\n' || c == '\r' || c == '\t') {
	    *buf = 0;
	    return (int)(buf - buf0);
	}
	if(c == '\b' || c == 0x7f) {
	    if(buf == buf0) {
		writec(ofd, '\a');
		continue;
	    }
	    writecs(ofd, "\b \b");
	    /* UTF-8 hack.  True glyph support would need Unicode library */
	    while(buf > buf0 + 1 && (buf[-1] & 0xc0) == 0x80) {
		buf--;
		size++;
	    }
	    buf--;
	    size++;
	    continue;
	}
	if(c < 32) {
	    writec(ofd, '\a');
	    continue;
	}
	if(size > 1) {
	    size--;
	    *buf++ = c;
	    if(!echo) {
		/* UTF-8 hack.  True glyph support would need Unicode library */
		if((c & 0xC0) == 0x80)
		    continue;
		c = '*';
	    }
	    writec(ofd, c);
	} else {
	    writec(ofd, '\a');
	    continue;
	}
    }
}

static int getyn(int ifd, int ofd)
{
    while(1) {
	int c = readc(ifd);
	if(c < 0)
	    return -1;
	if(c == 'y' || c == 'Y')
	    return 1;
	if(c == 'n' || c == 'N')
	    return 0;
	writec(ofd, '\a');
    }
}

/***** SPIFFS for persistent config *****/
#include "esp_spiffs.h"
#include <stdio.h> /* SPIFFS best accessed w/ buffered I/O */
#include <dirent.h>

static const esp_vfs_spiffs_conf_t fs_conf = {
    .base_path = "/config",
    .partition_label = NULL,
    .max_files = 32,
    .format_if_mount_failed = true
};

static void init_configfs(void)
{
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&fs_conf));
    // even (near) empty part takes *seconds* to check.  WTF?
    ESP_ERROR_CHECK(esp_spiffs_check(fs_conf.partition_label));
#if 0
    /* debug; remove when ready */
    size_t total = 0, used = 0;
    ESP_ERROR_CHECK(esp_spiffs_info(fs_conf.partition_label, &total, &used));
    ESP_LOGI("tjm", "Partition size: total: %d, used: %d", total, used);
#endif
//    chdir("/config"); /* doesn't seem to work, anyway */
}

/***** BT HID *****/
#include "esp_hidd.h"
#include "esp_hid_gap.h" /* support file from example */

/* Translate raw port value to axes/buttons */
/* masks taken from aqjoy.c; see there for accreditation */
/* Buttons */
static const uint8_t bmask[] = { 0xbf, 0x7b, 0x5f, 0xdf, 0x7d, 0x7e };
/* Paddle, clockwise from East */
static const uint8_t axmask[] = {
    0xfd, 0xed, 0xec, 0xfc, 0xfe, 0xee, 0xe6, 0xf6,
    0xf7, 0xe7, 0xe3, 0xf3, 0xfb, 0xeb, 0xe9, 0xf9
};

/* precomputed p(r,th)->r(x,y) so no fp math needed */
/* Originally I used 16-bit signed w/ R+x U+y, but HID strongly
 * encurages 8-bit unsigned U-y, so that's what I use now */
static const uint8_t dirval[][2] = {
    /* original: x=32767*cos(th);   y=32767*sin(th);   th=-i*π/8 */
    /* for HID:  x=128+127*cos(th); y=128+127*sin(th); th=i*π/8 */
    {255, 128}, {245, 177}, {218, 218}, {177, 245},
    {128, 255}, {79,  245}, {38,  218}, {11,  177},
    {1,   128}, {11,  79 }, {38,  38 }, {79,  11 },
    {128, 1  }, {177, 11 }, {218, 38 }, {245, 79 }
};

static void read_pad_mapped(uint8_t *x, uint8_t *y, uint8_t *b)
{
    uint8_t p = read_pad();
    /* I'm not going to worry about rollover */
    /* Multiple buttons are sent if sufficient cleared bits found */
    *b = 0;
    for(int i = 0; i < 6; i++)
	if(!(p & ~bmask[i]))
	    *b |= 1<<i;
    /* However, since they overlap, only send button 3 if both 3 & 4 are set */
    if((*b & (1<<2)))
	*b &= ~(1<<3);
    /* Buttons 6, 5 and 2 return directions.  Maybe filter in the future. */
    /* I should probably set up a special overall table for most likely
     * interpretations for everything, but I don't care right now. */
    /* direction is harder:  which to choose? */
    /* screw it:  I'm going to choose the first one with the most set bits */
    int dir = -1, ndir = 0;
    for(int i = 0; i < 16; i++) {
	if(!(p & ~axmask[i])) {
	    int nbits = i % 4 + 1; /* 1 2 3 2 */
	    if(nbits == 4)
		nbits = 2;
	    if(nbits > ndir) {
		ndir = nbits;
		dir = i;
	    }
	}
    }
    if(dir == -1)
	*x = *y = 0x80;
    else {
	*x = dirval[dir][0];
	*y = dirval[dir][1];
    }
}

/* HID output mappings: */
#define HIDMAP_AQA    0 /* Aq emulator port A */
#define HIDMAP_AQB    1 /* Aq emulator port B */
#define HIDMAP_SIMPLE 2 /* X+Y axes, ABCXYZ buttons */

/* There are 4 different HID descriptors.  One for each of the modes above,
 * and one that can be used for all 3 ("-1") */
/* Since HID descriptors are advertised/set up on boot, the descriptor is
 * stored in boot_hid_map, and the current mode is hid_map.  If boot_hid_map
 * is not -1, hid_map must equal boot_hid_map for proper operation. */
static int boot_hid_map, hid_map;
static int conf_boot_hid_map = -1, conf_hid_map = HIDMAP_AQA;

static void write_bt_conf(void); /* HID config is part of bt config */

static int hid_setup(int ifd, int ofd)
{
    char ibuf[80]; /* on stack to make this "reentrant" */
    if(writecs(ofd,
	       "\r\nHID Joystick mapping:\r\n"
	       " 1) Aq controller port A\r\n"
	       " 2) Aq controller port B\r\n"
	       " 3) Simple controller (ax=X/Y; b=ABCXYZ)\r\n"
	       " H) Toggle HID descriptor (all/just selected)\r\n"
	       " W) Write current config (and reboot)\n"))
	return 1;
    int ret = 0;
    while(1) {
	sprintf(ibuf, "%d%s%s", hid_map, boot_hid_map < 0 ? "*" : "",
		boot_hid_map == conf_boot_hid_map || hid_map != conf_hid_map ? "\r\n" : "->");
	if(conf_boot_hid_map != boot_hid_map || hid_map != conf_hid_map)
	    sprintf(ibuf + strlen(ibuf), "%d%s\r\n", hid_map + 1,
		    boot_hid_map < 0 ? "*" : "");
	if(writes(ofd, ibuf, 0))
	    return 1;
	if(ret)
	    return 0;
	ret = 1;
	while(1) {
	    int c = readc(ifd);
	    switch(c) {
	      case -1:
		return 1;
	      case 27:
	      case '\n':
	      case '\r':
		break;
	      default:
		writec(ofd, '\a');
		continue;
	      case '1':
	      case '2':
	      case '3':
		writec(ofd, c);
		hid_map = c - '1';
		if(boot_hid_map != -1 && boot_hid_map != hid_map) {
		    boot_hid_map = hid_map;
		    writecs(ofd, "\r\nWarning: need save/reboot or this won't work\r\n");
		}
		break;
	      case 'h':
	      case 'H':
		writec(ofd, c);
		boot_hid_map = boot_hid_map == hid_map ? -1 : hid_map;
		break;
	      case 'w':
	      case 'W':
		writec(ofd, c);
		write_bt_conf();
		break;
	    }
	    /* FIXME: restart input-client if hid_map changes */
	    break;
	}
    }
}

/* Comments, structure courtesy of hidrd-convert on a PS4 controller */
static const uint8_t HIDDescAqA[] = {
    0x05, 0x01,         /*  Usage Page (Desktop),               */
    0x09, 0x05,         /*  Usage (Gamepad),                    */
    0xA1, 0x01,         /*  Collection (Application),           */
    /* Axes */
    0x09, 0x30,         /*      Usage (X),                      */
    0x09, 0x31,         /*      Usage (Y),                      */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x75, 0x08,         /*      Report Size (8),                */
    0x95, 0x02,         /*      Report Count (2),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Buttons */
    0x05, 0x09,         /*      Usage Page (Button),            */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x01,         /*      Logical Maximum (1),            */
    0x75, 0x01,         /*      Report Size (1),                */
    /* XY<TR>AB<TR2> (458 12A) */
    0x19, 0x04,         /*      Usage Minimum (04h),            */
    0x29, 0x05,         /*      Usage Maximum (05h),            */
    0x09, 0x08,         /*      Usage (8),                      */
    0x19, 0x01,         /*      Usage Minimum (01h),            */
    0x29, 0x02,         /*      Usage Maximum (02h),            */
    0x09, 0x0A,         /*      Usage (10),                     */
    0x95, 0x06,         /*      Report Count (6),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Pad to 1 byte */
    0x75, 0x02,         /*      Report Size (2),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x01,         /*      Input (Constant),               */
    0xC0                /*  End Collection                      */
};

static const uint8_t HIDDescAqB[] = {
    0x05, 0x01,         /*  Usage Page (Desktop),               */
    0x09, 0x05,         /*  Usage (Gamepad),                    */
    0xA1, 0x01,         /*  Collection (Application),           */
    /* Axes */
    0x09, 0x32,         /*      Usage (Z),                      */
    0x09, 0x35,         /*      Usage (Rz),                     */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x75, 0x08,         /*      Report Size (8),                */
    0x95, 0x02,         /*      Report Count (2),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Buttons */
    /* Actually, left 4 buttons are mapped to hat0 */
    0x09, 0x39,         /*      Usage (Hat Switch),             */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x07,         /*      Logical Maximum (7),            */
    0x75, 0x04,         /*      Report Size (4),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x42,         /*      Input (Variable, Null State),   */

    0x05, 0x09,         /*      Usage Page (Button),            */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x01,         /*      Logical Maximum (1),            */
    0x75, 0x01,         /*      Report Size (1),                */
    /* right 2 are mapped to <TL><TL2> (79) */
    0x09, 0x07,         /*      Usage (7),                      */
    0x09, 0x09,         /*      Usage (9),                      */
    0x95, 0x06,         /*      Report Count (2),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Pad to 1 byte */
    0x75, 0x02,         /*      Report Size (2),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x01,         /*      Input (Constant),               */
    0xC0                /*  End Collection                      */
};

static const uint8_t HIDDescSimple[] = {
    0x05, 0x01,         /*  Usage Page (Desktop),               */
    0x09, 0x05,         /*  Usage (Gamepad),                    */
    0xA1, 0x01,         /*  Collection (Application),           */
    /* Axes */
    0x09, 0x30,         /*      Usage (X),                      */
    0x09, 0x31,         /*      Usage (Y),                      */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x75, 0x08,         /*      Report Size (8),                */
    0x95, 0x02,         /*      Report Count (2),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Buttons */
    0x05, 0x09,         /*      Usage Page (Button),            */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x01,         /*      Logical Maximum (1),            */
    0x75, 0x01,         /*      Report Size (1),                */
    /* Default is ABC XYZ (123 456) */
    0x19, 0x01,         /*      Usage Minimum (01h),            */
    0x29, 0x06,         /*      Usage Maximum (06h),            */
    0x95, 0x06,         /*      Report Count (6),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Pad to 1 byte */
    0x75, 0x02,         /*      Report Size (2),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x01,         /*      Input (Constant),               */
    0xC0                /*  End Collection                      */
};

static const uint8_t HIDDescAll[] = {
    0x05, 0x01,         /*  Usage Page (Desktop),               */
    0x09, 0x05,         /*  Usage (Gamepad),                    */
    0xA1, 0x01,         /*  Collection (Application),           */
    /* Axes */
    0x09, 0x30,         /*      Usage (X),                      */
    0x09, 0x31,         /*      Usage (Y),                      */
    0x09, 0x32,         /*      Usage (Z),                      */
    0x09, 0x35,         /*      Usage (Rz),                     */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x26, 0xFF, 0x00,   /*      Logical Maximum (255),          */
    0x75, 0x08,         /*      Report Size (8),                */
    0x95, 0x04,         /*      Report Count (4),               */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Buttons */
    /* Actually, left 4 buttons for AqB are mapped to hat0 */
    0x09, 0x39,         /*      Usage (Hat Switch),             */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x07,         /*      Logical Maximum (7),            */
    0x75, 0x04,         /*      Report Size (4),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x42,         /*      Input (Variable, Null State),   */

    /* Buttons */
    0x05, 0x09,         /*      Usage Page (Button),            */
    0x15, 0x00,         /*      Logical Minimum (0),            */
    0x25, 0x01,         /*      Logical Maximum (1),            */
    0x75, 0x01,         /*      Report Size (1),                */
    /* ABC XYZ TL TR TL2 TR2 */
    0x19, 0x01,         /*      Usage Minimum (01h),            */
    0x29, 0x0A,         /*      Usage Maximum (0Ah),            */
    0x95, 0x0A,         /*      Report Count (10),              */
    0x81, 0x02,         /*      Input (Variable),               */
    /* Pad to 1 byte */
    0x75, 0x02,         /*      Report Size (2),                */
    0x95, 0x01,         /*      Report Count (1),               */
    0x81, 0x01,         /*      Input (Constant),               */
    0xC0                /*  End Collection                      */
};

/* Most of code below copied from esp_hid_device example */
/* Main changes are removal of log code, minor sytle changes */
/* Also, I eventually removed BLE code because I don't see the point of both */
static const esp_hid_raw_report_map_t bt_report_maps[] = {
    { .data = HIDDescAll,    .len = sizeof(HIDDescAll) },
    { .data = HIDDescAqA,    .len = sizeof(HIDDescAqA) },
    { .data = HIDDescAqB,    .len = sizeof(HIDDescAqB) }, 
    { .data = HIDDescSimple, .len = sizeof(HIDDescSimple) }
};

#define AQC_NAME "Aquarius Controller"
#define AQC_VEND 0x16C0 /* FIXME: better vendor ID */
#define AQC_PROD 0x05DF /* FIXME: better product ID */
#define AQC_VER  0x0100
static esp_hid_device_config_t bt_hid_config = {
    .vendor_id          = AQC_VEND,
    .product_id         = AQC_PROD,
    .version            = AQC_VER,
    .device_name        = AQC_NAME,
    .manufacturer_name  = "Thomas J. Moore",
    .serial_number      = "1234567890", /* FIXME: serial # */
    .report_maps        = NULL, /* filled in by init */
    .report_maps_len    = 1
};

/* Helper for Aq B buttons (hat) */
/* HAT dir:
 *    4
 *   5 3
 *  6   2
 *   7 1
 *    0
 */
static uint8_t but_to_dir(uint8_t b)
{
    /* only one of each axis can be returned; pick earlier one (left/up) */
    if(b & 1) /* 1-L */
	return b & 2 ? 5 /* +2-U */ : b & 8 ? 7 /* +4-D */ : 6;
    if(b & 2) /* 2-U */
	return b & 0x10 ? 3 /* +5-R */ : 4;
    if(b & 8) /* 4-D */
	return b & 0x10 ? 1 /* +5-R */ : 0;
    if(b & 0x10) /* 5-R */
	return 2;
    return 8; /* out-of-range (NULL) */
}

/* While it's up, the HID task just spits out HID events */
static void bt_hid_task(void *hid_dev)
{
    uint8_t ox = 0x80, oy = 0x80, ob = 0xff; /* ff is to force initial send */
    while(1) {
	// 16ms is not happening if system tick is 10ms.
	vTaskDelay(16 / portTICK_PERIOD_MS);
	uint8_t x, y, b;
	read_pad_mapped(&x, &y, &b);
	if(x == ox && y == oy && b == ob)
	    continue;
	ox = x; oy = y; ob = b;
	y = 256 - y; /* HID spec says -y = up */
	static uint8_t buf[6];
	int len = boot_hid_map == -1 ? 6 : 3;
	switch(boot_hid_map) {
	  case HIDMAP_AQA:
	  case HIDMAP_SIMPLE:
	    buf[0] = x;
	    buf[1] = y;
	    buf[2] = b;
	    break;
	  case HIDMAP_AQB:
	    buf[0] = x;
	    buf[1] = y;
	    buf[2] = b & 0x30;
	    buf[2] |= but_to_dir(b);
	    break;
	  case -1:
	    buf[0] = buf[1] = buf[2] = buf[3] = 128;
	    if(hid_map == HIDMAP_AQB) {
		/* right stick */
		buf[2] = x;
		buf[3] = y;
	    } else {
		/* left stick */
		buf[0] = x;
		buf[1] = y;
	    }
	    /* buf[4] = XCBA HHHH */
	    /* buf[5] = 00RL rlZY */
	    switch(hid_map) {
	      case HIDMAP_AQA: /* XYr ABR */
		buf[4] = ((b&0x18)<<1) | ((b&1)<<7) | 8;
		buf[5] = ((b&2)>>1) | ((b&4)<<1) | (b&0x20);
		break;
	      case HIDMAP_AQB: /* HHl HHL */
		buf[4] = but_to_dir(b);
		buf[5] = (b&4) | ((b&0x20)>>1);
		break;
	      case HIDMAP_SIMPLE: /* ABC XYZ */
		buf[4] = (b << 4) | 8;
		buf[5] = (b >> 4) & 3;
		break;
	    }
	    break;
	}
	ESP_ERROR_CHECK(esp_hidd_dev_input_set(hid_dev, 0, 0, buf, len));
    }
}

static struct hid_info {
    esp_hidd_dev_t *dev;
    TaskHandle_t task;
} ble_info, classic_info;

static void bt_hidd_event_callback(void *handler_args, esp_event_base_t base,
				   int32_t id, void *event_data,
				   struct hid_info *info)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        if (param && param->start.status != ESP_OK)
            ESP_LOGE("tjm", "START failed!");
	else if(info == &classic_info)
	    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
	else
	    esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
	/* FIXME: update config */
        if (param && param->connect.status != ESP_OK)
            ESP_LOGE("tjm", "CONNECT failed!");
	else {
	    if(info == &classic_info)
		esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
	    else
		esp_ble_gap_stop_advertising(); /* sample doesn't do this */
	    xTaskCreate(bt_hid_task, "bt_hid", 2 * 1024, info->dev,
			configMAX_PRIORITIES - 3, &info->task);
        }
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        if (param && param->disconnect.status != ESP_OK)
            ESP_LOGE("tjm", "DISCONNECT failed!");
	else {
	    if (info->task) {
		vTaskDelete(info->task);
		info->task = NULL;
	    }
	    if(info == &classic_info)
		esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
	    else
		esp_hid_ble_gap_adv_start();
        }
        break;
      default: break; /* stupid -Werror policies */
    }
}

/* There is no user data, so use separate callbacks */
static void bt_hidd_callback(void *handler_args, esp_event_base_t base,
			     int32_t id, void *event_data)
{
    bt_hidd_event_callback(handler_args, base, id, event_data, &classic_info);
}

static void ble_hidd_callback(void *handler_args, esp_event_base_t base,
			      int32_t id, void *event_data)
{
    bt_hidd_event_callback(handler_args, base, id, event_data, &ble_info);
}

/***** BT Serial *****/
#include "esp_spp_api.h"

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
      case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            /* Enable SPP VFS mode */
            esp_spp_vfs_register();
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0,
			      "Aquarius SPP");
        } else
            ESP_LOGE("SPP", "ESP_SPP_INIT_EVT status:%d", param->init.status);
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        if (param->srv_open.status == ESP_SPP_SUCCESS)
	    /* on CLOSE, vfs returns -1 on read, so task will auto-kill itself */
	    xTaskCreate(serial_task, "spp", 1024*3, (void *)param->srv_open.fd, configMAX_PRIORITIES, NULL);
	else
            ESP_LOGE("SPP", "ESP_SPP_INIT_EVT status:%d", param->init.status);
        break;
    default:
        break;
    }
}

/***** Rest of BT *****/
#include "esp_bt_device.h"

static int bt_classic, bt_ble, spp;
static int conf_bt_classic = 1, conf_bt_ble = 1, conf_spp = 1;

static int bt_setup(int ifd, int ofd)
{
    char ibuf[80]; /* on stack to make this "reentrant" */
    /* Technically, I could turn BLE/Classic on/off on the fly */
    /* Also, could turn individual services (SPP/HID) on/off for classic */
    /* However, to avoid issues, better to just reboot */
    if(writecs(ofd,
	       "\r\nBluetooth Configuration:\r\n"
	       " C) Toggle Classic at boot\r\n"
	       " L) Toggle BLE at boot\r\n"
	       " S) Toggle Serial port\r\n"
	       " W) Write config and reboot\r\n"))
	return 1;
    int ret = 0;
    while(1) {
	sprintf(ibuf, "Classic %s%s%s%s BLE %s%s\r\n",
		conf_bt_classic ? "On" : "Off",
		conf_bt_classic == bt_classic ? "" :
		bt_classic ? "->On" : "->Off",
		conf_spp ? " SPP" : "", conf_spp == spp ? "" :
		spp ? "->SPP" : "->Off",
		conf_bt_ble ? "On" : "Off",
		conf_bt_ble == bt_ble ? "" :
		bt_ble ? "->On" : "->Off");
	if(writes(ofd, ibuf, 0))
	    return 1;
	if(ret)
	    return 0;
	ret = 1;
	while(1) {
	    int c = readc(ifd);
	    switch(c) {
	      case -1:
		return 1;
	      case 27:
	      case '\n':
	      case '\r':
		break;
	      default:
		writec(ofd, '\a');
		continue;
	      case 'c':
	      case 'C':
		bt_classic = !bt_classic;
		break;
	      case 'l':
	      case 'L':
		bt_ble = !bt_ble;
		break;
	      case 's':
	      case 'S':
		spp = !spp;
		break;
	      case 'w':
	      case 'W':
		writec(ofd, c);
		write_bt_conf();
		break;
	    }
	    break;
	}
    }
}

static void write_bt_conf(void)
{
    FILE *bt_conf;
    if((bt_conf = fopen("/config/bt", "w"))) {
	putc(bt_classic ? 'Y' : 'N', bt_conf);
	putc(bt_ble ? 'Y' : 'N', bt_conf);
	putc(spp ? 'Y' : 'N', bt_conf);
	putc('0' + boot_hid_map, bt_conf);
	putc('0' + hid_map, bt_conf);
	if(!fflush(bt_conf) && !fclose(bt_conf)) {
	    if(bt_classic != conf_bt_classic || bt_ble != conf_bt_ble ||
	       spp != conf_spp || conf_boot_hid_map != boot_hid_map)
		esp_restart();
	    conf_hid_map = hid_map; /* the only one that doesn't need reboot */
	}
    }
}

static void init_bluetooth(void)
{
    FILE *bt_conf = fopen("/config/bt", "r");
    if(bt_conf) {
	do {
	    /* BT config file: */
	    /*  <connClassicY|N><connBLEY|N><boot_map#><map#> */
	    int c = getc(bt_conf);
	    if(c != 'Y' && c != 'N')
		goto error;
	    conf_bt_classic = c == 'Y';
	    c = getc(bt_conf);
	    if(c != 'Y' && c != 'N')
		goto error;
	    conf_bt_ble = c == 'Y';
	    c = getc(bt_conf);
	    if(c != 'Y' && c != 'N')
		goto error;
	    conf_spp = c == 'Y';
	    c = getc(bt_conf);
	    if(c < '0' - 1 || c > '2')
		goto error;
	    conf_boot_hid_map = c - '0';
	    c = getc(bt_conf);
	    if(c < '0' || c > '2')
		goto error;
	    conf_hid_map = c - '0';
	    fclose(bt_conf);
	    break;
error:
	    fclose(bt_conf);
	    bt_conf = NULL;
	} while(0);
    }
    bt_ble = conf_bt_ble;
    bt_classic = conf_bt_classic;
    spp = conf_spp;
    hid_map = conf_hid_map;
    boot_hid_map = conf_boot_hid_map;
    if(!bt_conf)
	write_bt_conf();

    int dev_mode = bt_ble ? HIDD_BLE_MODE : 0;
    dev_mode |= bt_classic ? HIDD_BT_MODE : 0;

    if(!dev_mode)
	return;

    ESP_ERROR_CHECK(esp_hid_gap_init(dev_mode));

    bt_hid_config.report_maps =
	(esp_hid_raw_report_map_t *)&bt_report_maps[boot_hid_map + 1];
    /* classic init */
    if(bt_classic || spp) {
	esp_bt_dev_set_device_name(bt_hid_config.device_name);
	esp_bt_cod_t cod;
	cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
	esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
	/* seems to crash (more often) w/o this delay */
	/* probably somebody not waiting long enough for init to complete */
	vTaskDelay(500 / portTICK_PERIOD_MS);
	if(bt_classic)
	    ESP_ERROR_CHECK(esp_hidd_dev_init(&bt_hid_config, ESP_HID_TRANSPORT_BT,
					      bt_hidd_callback, &classic_info.dev));
	if(spp) {
	    ESP_ERROR_CHECK(esp_spp_register_callback(esp_spp_cb));
	    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_VFS));
	}
    }
    /* ble init */
    if(bt_ble) {
	ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC,
						 bt_hid_config.device_name));
	ESP_ERROR_CHECK(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler));
	ESP_ERROR_CHECK(esp_hidd_dev_init(&bt_hid_config, ESP_HID_TRANSPORT_BLE,
					  ble_hidd_callback, &ble_info.dev));
    }
}

/***** network-gamepads *****/
#include "lwip/sockets.h"
#include "ngp_protocol.h"
#include "esp_wifi.h"

/* Low level protocol: read/write messages */
static int send_msg(int fd, const uint8_t *buf)
{
    int len = get_size_from_command(buf, 2);
    while(len > 0) {
	int r = send(fd, buf, len, MSG_NOSIGNAL);
	if(!r || (r < 0 && (errno == EAGAIN || errno == EINTR))) {
	    vTaskDelay(1);
	    continue;
	}
	if(r < 0)
	    return 1;
	len -= r;
	buf += r;
    }
    return 0;
}

static int recv_buf(int fd, uint8_t *buf, int len)
{
    while(len > 0) {
	int r = recv(fd, buf, len, 0);
	if(!r || (r < 0 && (errno == EAGAIN || errno == EINTR))) {
	    vTaskDelay(1);
	    continue;
	}
	if(r < 0)
	    return 1;
	len -= r;
	buf += r;
    }
    return 0;
}

static int recv_msg(int fd, uint8_t *buf, int blen)
{
    if(recv_buf(fd, buf, 1))
	return 1;
    int len = get_size_from_command(buf, 1); /* no variable-len responses */
    if(len > blen)
	return 1;
    if(len > 1 && recv_buf(fd, buf + 1, len - 1))
       return 1;
    return 0;
}

#define MAX_NGP_RECV 2
static uint32_t ngp_addr, ngp_port;
static char ngp_pw[256]; /* actually, stored in ngp_pw + 2 */
static TaskHandle_t ngp_task_id;
#include "input-event-codes.h"

/* from aqdrv */
static const int mapto[3 * 6] = {
    BTN_X, BTN_Y, BTN_TR, BTN_A, BTN_B, BTN_TR2, /* Aq port A */
    -1,    -2,    BTN_TL, -3,    -4,    BTN_TL2, /* Aq port B (-x == hat) */
    BTN_A, BTN_B, BTN_C,  BTN_X, BTN_Y, BTN_Z,   /* simple mapping */
};


static int ngp_init(int fd, int slot)
{
    uint8_t recbuf[MAX_NGP_RECV];
    const HelloMessage hm = {
	.msg_type = NGP_MESSAGE_HELLO, .version = NGP_PROTOCOL_VERSION,
	.slot = slot
    };
    if(send_msg(fd, (const uint8_t *)&hm) ||
       recv_msg(fd, recbuf, MAX_NGP_RECV))
	return -1;
    if(recbuf[0] == NGP_MESSAGE_PASSWORD_REQUIRED) {
	ngp_pw[0] = NGP_MESSAGE_PASSWORD;
	ngp_pw[1] = strlen(ngp_pw);
	if(send_msg(fd, (uint8_t *)ngp_pw) ||
	   recv_msg(fd, recbuf, MAX_NGP_RECV))
	    return -1;
    }
    if(recbuf[0] == NGP_MESSAGE_SETUP_REQUIRED) {
	static const DeviceMessage dm = {
	    .msg_type = NGP_MESSAGE_DEVICE, .length = sizeof(AQC_NAME) - 1,
	    .id = { 3 /* USB */, AQC_VEND, AQC_PROD, AQC_VER },
	    .name = AQC_NAME
	};
	if(send_msg(fd, (const uint8_t *)&dm))
	    return -1;
	RequestEventMessage bti = {
	    .msg_type = NGP_MESSAGE_REQUEST_EVENT, .type = EV_KEY
	};
	/* mostly copied from aqdrv */
	const int *bmap = mapto + 6 * hid_map;
	for(int i = 0; i < 6; i++)
	    if(bmap[i] > 0) {
		bti.code = bmap[i];
		if(send_msg(fd, (uint8_t *)&bti))
		    return -1;
	    }
	bti.type = EV_SYN;
	bti.code = SYN_REPORT;
	if(send_msg(fd, (uint8_t *)&bti))
	    return -1;
	bti.type = EV_ABS;
	ABSInfoMessage axi = {
	    .msg_type = NGP_MESSAGE_ABSINFO,
	    .info = {
		.minimum = 1,
		.maximum = 255,
		.value = 128
	    }
	};
	if(hid_map != 1) {
	    axi.axis = bti.code = ABS_X;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	    axi.axis = bti.code = ABS_Y;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	} else {
	    axi.axis = bti.code = ABS_Z;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	    axi.axis = bti.code = ABS_RZ;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	    memset(&axi.info, 0, sizeof(axi.info));
	    axi.info.minimum = -1;
	    axi.info.maximum = 1;
	    axi.axis = bti.code = ABS_HAT0X;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	    axi.axis = bti.code = ABS_HAT0Y;
	    if(send_msg(fd, (uint8_t *)&axi) || send_msg(fd, (uint8_t *)&bti))
		return -1;
	}
	uint8_t done = NGP_MESSAGE_SETUP_END;
	if(send_msg(fd, &done) ||
	   recv_msg(fd, recbuf, MAX_NGP_RECV))
	    return -1;
    }
    if(recbuf[0] != NGP_MESSAGE_SUCCESS)
	return -1;
    return recbuf[1];
}

static int write_ev(int t, int fd, int ev, int val)
{
    DataMessage dm = {
	.msg_type = NGP_MESSAGE_DATA, .type = htons(t), .code = htons(ev),
	.value = htonl(val)
    };
    return send_msg(fd, (uint8_t *)&dm);
}

#define write_abs(fd, ev, val) do { \
    if(write_ev(EV_ABS, fd, ev, val)) \
	return; \
} while(0)
#define write_key(fd, ev, val) do { \
    if(write_ev(EV_KEY, fd, ev, !!(val))) \
	return; \
} while(0)
#define write_sr(fd) do { \
    if(write_ev(EV_SYN, fd, SYN_REPORT, 0)) \
	return; \
} while(0)

static int ngp_sock = -1;

static void ngp_loop(void)
{
    /* main loop from aqdrv */
    uint8_t nx, ny, nb, ox = 0x80, oy = 0x80, ob = 0;
    while(1) {
	read_pad_mapped(&nx, &ny, &nb);
	// 16ms is not happening if system tick is 10ms.
	vTaskDelay(16 / portTICK_PERIOD_MS);
	int ws = nx != ox || ny != oy;
	if(nx != ox)
	    write_abs(ngp_sock, hid_map == 1 ? ABS_Z : ABS_X, (ox = nx));
	if(ny != oy)
	    write_abs(ngp_sock, hid_map == 1 ? ABS_RZ : ABS_Y, 256 - (oy = ny));
	if(ws)
	    write_sr(ngp_sock);
	if(nb == ob)
	    continue;
	int is_hatx = 0, is_haty = 0, hatx = 0, haty = 0;
	const int *bmap = mapto + 6 * hid_map;
	for(uint8_t i = 0, m = 1; i < 6; i++, m *= 2) {
	    uint8_t n = nb & m, o = ob & m;
	    if(n == o)
		continue;
	    if(bmap[i] > 0)
		write_key(ngp_sock, bmap[i], n);
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
	    write_abs(ngp_sock, ABS_HAT0X, hatx);
	if(is_haty)
	    write_abs(ngp_sock, ABS_HAT0Y, haty);
	ob = nb;
	write_sr(ngp_sock);
    }
}

static void ngp_task(void *parm)
{
    int slot = 0; /* FIXME: maybe make this a config option */
    while(1) {
	if(ngp_sock >= 0)
	    close(ngp_sock);
	ngp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	struct sockaddr_in addr = {
	    .sin_addr = { .s_addr = ngp_addr },
	    .sin_family = AF_INET,
	    .sin_port = htons(ngp_port)
	};
	if(ngp_sock < 0)
	    vTaskDelete(NULL);  /* probably ought to ESP_LOGE() */
	if(!connect(ngp_sock, (struct sockaddr *)&addr, sizeof(addr)) &&
	   (slot = ngp_init(ngp_sock, slot)) >= 0)
	    ngp_loop();
	if(slot < 0)
	    slot = 0;
	vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

static void write_wifi_cfg(void);

#include "esp_wifi.h"
static wifi_config_t wifi_cfg = {};

static int ngp_setup(int ifd, int ofd)
{
    static char ibuf[300]; /* not reentrant.  tough. */
    if(writecs(ofd, "\r\nnetwork-gamepads client config\r\n"))
	return 1;
    if(!wifi_cfg.sta.ssid[0])
	return writecs(ofd, "Configure wifi first\r\n");
    esp_ip4_addr_t ad = { .addr = ngp_addr };
    esp_ip4addr_ntoa(&ad, ibuf, sizeof(ibuf));
    if(writes(ofd, ibuf, 0))
	return 1;
    sprintf(ibuf, ":%d/%s", (int)ngp_port, ngp_pw[2] ? "*...*" : "");
    if(writes(ofd, ibuf, 0))
	return 1;
    if(writecs(ofd, "\r\nServer IP address[:Port][/passwd] (blank=keep, 0=disable)? ") ||
       reads(ifd, ofd, ibuf, sizeof(ibuf), 1) < 0)
	return 1;
    if(!ibuf[0])
	return writecs(ofd, "\r\n");
    uint32_t port;
    char *p = strchr(ibuf, '/');
    if(p) {
	*p++ = 0;
	if(!*p)
	    p = NULL;
    }
    char *s = strchr(ibuf, ':');
    if(s)
	*s++ = 0;
    port = s ? atoi(s) : 9292;
    if(!port)
	port = 9292;
    esp_netif_str_to_ip4(ibuf, &ad);
    if(ad.addr != ngp_addr || port != ngp_port ||
       (!p && ngp_pw[2]) || (p && strcmp(p, ngp_pw))) {
	if(ngp_task_id) {
	    /* Technically leaks ngp_sock, but it will be closed on restart */
	    vTaskDelete(ngp_task_id);
	    ngp_task_id = NULL;
	}
	ngp_addr = ad.addr;
	ngp_port = port;
	if(p)
	    strcpy(ngp_pw + 2, p);
	else ngp_pw[2] = 0;
	if(ngp_addr)
	    xTaskCreate(ngp_task, "input_client", 1024*3, NULL, configMAX_PRIORITIES, &ngp_task_id);
    }
    if(writecs(ofd, "\r\n"))
	return 1;
    write_wifi_cfg(); /* always save */
    return 0;
}

/***** WiFi *****/
static int conf_wifi = 1, wifi_serial = 28010;
static TaskHandle_t wifi_serial_task_id;
static esp_netif_ip_info_t conf_ip;
static esp_netif_dns_info_t conf_dns;

#define DEFAULT_SCAN_LIST_SIZE CONFIG_EXAMPLE_SCAN_LIST_SIZE

static wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
static uint16_t ap_count;

static int cmp_str(const void *a, const void *b)
{
    const wifi_ap_record_t *apa = a, *apb = b;
    if(apa->rssi != apb->rssi)
	return apb->rssi - apa->rssi;
    return strcmp((char *)apa->ssid, (char *)apb->ssid);
}

static void do_wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));
    esp_wifi_scan_start(NULL, true);
    if(!conf_wifi)
	return;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    qsort(ap_info, ap_count, sizeof(*ap_info), cmp_str);
}

static int wifi_scan(int ofd)
{
    char ibuf[10];
    do_wifi_scan();
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
	sprintf(ibuf, "%d", ap_info[i].rssi);
	if(writecs(ofd, "Wifi scan: '") ||
	   writes(ofd, (char *)ap_info[i].ssid, 0) ||
	   writecs(ofd, "' (") ||
	   writes(ofd, ibuf, 0) ||
	   writecs(ofd, ")\r\n"))
	    return 1;
//        ESP_LOGI("tjm", "Channel \t\t%d\n", ap_info[i].primary);
    }
    return 0;
}

static esp_netif_t *netif;

/* ip/mask>gw?ns */
/* modifies str in place (temporarily) */
static int parse_ip_info(char *str, esp_netif_ip_info_t *info,
			 esp_netif_dns_info_t *ns)
{
    char *s, c;
    memset(info, 0, sizeof(*info));
    memset(ns, 0, sizeof(*ns));
    ns->ip.type = ESP_IPADDR_TYPE_V4; /* not necessary, since it's 0 */
    if(!atoi(str))
	return 0;
    for(s = str; isdigit(*s) || *s == '.'; s++);
    c = *s;
    *s = 0;
    esp_netif_str_to_ip4(str, &info->ip);
    *s++ = c;
    if(c == '/') {
	str = s;
	int mlen = strtol(s, &s, 10);
	if(*s != '.' && mlen <= 32) {
	    uint32_t mask = (1 << mlen) - 1;
	    c = *s;
	    info->netmask.addr = esp_netif_htonl(mask);
	} else {
	    for(s = str; isdigit(*s) || *s == '.'; s++);
	    c = *s;
	    *s = 0;
	    esp_netif_str_to_ip4(str, &info->netmask);
	}
	*s++ = c;
    } else
	info->netmask.addr = esp_netif_htonl(0xFFFFFF00);
    if(c == '>') {
	for(str = s; isdigit(*s) || *s == '.'; s++);
	c = *s;
	*s = 0;
	esp_netif_str_to_ip4(str, &info->gw);
	*s++ = c;
    } else
	info->gw.addr = (info->ip.addr & info->netmask.addr) | esp_netif_htonl(1);
    if(c == '?') {
	for(str = s; isdigit(*s) || *s == '.'; s++);
	c = *s;
	*s = 0;
	esp_netif_str_to_ip4(str, &ns->ip.u_addr.ip4);
	*s++ = c;
    } else
	ns->ip.u_addr.ip4 = info->gw;
    while(isspace(c))
	c = *s++;
    return !c;
}

static int read_wifi_cfg(void)
{
    FILE *cf;
    char ssbuf[80];
    memset(&conf_ip, 0, sizeof(conf_ip));
    memset(&conf_dns, 0, sizeof(conf_dns));
    wifi_cfg.sta.password[0] = 0;
    wifi_serial = 28010;
    ngp_addr = 0;
    ngp_port = 9292;
    ngp_pw[2] = 0;
    sprintf(ssbuf, "/config/%.32s.wifi", wifi_cfg.sta.ssid);
    while((cf = fopen(ssbuf, "r"))) {
	if(!fgets(ssbuf, sizeof(wifi_cfg.sta.password) + 1, cf))
	    break;
	if(ssbuf[0])
	    ssbuf[strlen(ssbuf) - 1] = 0; /* remove \n */
	strcpy((char *)wifi_cfg.sta.password, ssbuf);
	if(!fgets(ssbuf, sizeof(ssbuf), cf))
	    break;
	parse_ip_info(ssbuf, &conf_ip, &conf_dns);
	/* FIXME: server port should be "global" */
	if(!fgets(ssbuf, sizeof(ssbuf), cf))
	    break;
	if(isdigit(ssbuf[0]))
	    wifi_serial = atoi(ssbuf);
	else
	    wifi_serial = 28010;
	if(!fgets(ssbuf, sizeof(ssbuf), cf))
	   break;
	ssbuf[strlen(ssbuf)-1] = 0;
	char *s = strchr(ssbuf, ':');
	if(s) {
	    *s = 0;
	    ngp_port = atoi(s + 1);
	    if(!ngp_port)
		ngp_port = 9292;
	}
	esp_ip4_addr_t ad;
	esp_netif_str_to_ip4(ssbuf, &ad);
	ngp_addr = ad.addr;
	if(!fgets(ngp_pw + 2, sizeof(ngp_pw) - 2, cf))
	    break;
	ngp_pw[2 + strlen(ngp_pw + 2) - 1] = 0;
	break;
    }
    if(cf)
	fclose(cf);
    return !!cf;
}

static void wifi_listen_task(void *_fd)
{
    int lfd = (int)_fd;
    while(1) {
	int fd = accept(lfd, NULL, NULL);
	if(fd >= 0)
	    xTaskCreate(serial_task, "wifisc", 1024*3, (void *)fd, configMAX_PRIORITIES, NULL);
	vTaskDelay(1);
    }
}

static int wifi_listen_fd = -1;

static void start_wifi_serial(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in addr = {
	.sin_addr = { .s_addr = htonl(INADDR_ANY) },
	.sin_family = AF_INET,
	.sin_port = htons(wifi_serial)
    };
    if(fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) ||
       listen(fd, 1)) {
	ESP_LOGE("wifi", "Unable to create serial listener %d %s\n", errno, strerror(errno));
	if(fd >= 0)
	    close(fd);
	return;
    }
    wifi_listen_fd = fd;
    xTaskCreate(wifi_listen_task, "wifis", 1024*3, (void *)fd, configMAX_PRIORITIES, &wifi_serial_task_id);
}

static void stop_wifi_serial(void)
{
    if(wifi_serial_task_id) {
	vTaskDelete(wifi_serial_task_id);
	wifi_serial_task_id = NULL;
	if(wifi_listen_fd >= 0) {
	    close(wifi_listen_fd);
	    wifi_listen_fd = -1;
	}
    }
}

static int print_ip(FILE *f, esp_ip4_addr_t *ip)
{
    return fprintf(f, "%d.%d.%d.%d",
		   esp_ip4_addr1(ip), esp_ip4_addr2(ip),
		   esp_ip4_addr3(ip), esp_ip4_addr4(ip));
}

static void write_wifi_cfg(void)
{
    char buf[80];
    if(!wifi_cfg.sta.ssid[0])
	return;
    sprintf(buf, "/config/%s.wifi", wifi_cfg.sta.ssid);
    FILE *f = fopen(buf, "w");
    if(f) {
	fputs((const char *)wifi_cfg.sta.password, f);
	putc('\n', f);
	if(conf_ip.ip.addr) {
	    print_ip(f, &conf_ip.ip);
	    putc('/', f);
	    print_ip(f, &conf_ip.netmask);
	    putc('>', f);
	    print_ip(f, &conf_ip.gw);
	    putc('?', f);
	    print_ip(f, &conf_dns.ip.u_addr.ip4);
	}
	putc('\n', f);
	if(wifi_serial == 28010)
	    putc('\n', f);
	else
	    fprintf(f, "%d\n", wifi_serial);
	esp_ip4_addr_t ad;
	ad.addr = ngp_addr;
	print_ip(f, &ad);
	if(ngp_port != 9292)
	    fprintf(f, ":%d", (int)ngp_port);
	fprintf(f, "\n%s\n", ngp_pw + 2);
	fflush(f);
	fclose(f);
    }
    /* FIXME: file: wifi; default (current) ssid, serial port config? */
}

static int wifi_setup(int ifd, int ofd)
{
    /* FIXME: wait until init_wifi() complete */
    esp_err_t err;
    char ibuf[80]; /* on stack to make this "reentrant" */
    /* note: relies on ssid/pw being short enough */
    /* in esp-idf-4, ssid=32 max, pw=64 max */
    /* Technically, I could turn wifi on and off on the fly */
    /* However, to avoid issues, better to just reboot */
    /* FIXME: use temporary storage; update globals on success */
    if(conf_wifi && wifi_scan(ofd))
	return 1;
    esp_wifi_get_mac(WIFI_IF_STA, (uint8_t *)ibuf + 25);
    strcpy(ibuf, "MAC: ");
    for(int i = 0; i < 6; i++) {
	sprintf(ibuf + 5 + i * 3, "%02X", (int)ibuf[i + 25]);
	ibuf[5 + i * 3 + 2] = ':';
    }
    strcpy(ibuf + 5 + 6 * 3 - 1, "\r\n");
    writes(ofd, ibuf, 0);
    DIR *dir = opendir("/config");
    if(dir) {
	struct dirent *de;
	while((de = readdir(dir))) {
	    writec(ofd, ':');
	    writes(ofd, de->d_name, 0);
	    writec(ofd, '\n');
	    if(de->d_type != DT_REG)
		continue;
	    int len = strlen(de->d_name);
	    if(len < 6 || strcmp(de->d_name + len - 5, ".wifi"))
		continue;
	    de->d_name[len - 5] = 0;
	    if(writecs(ofd, "\r\nConfiguration found for ") ||
	       writes(ofd, de->d_name, 0)) {
		closedir(dir);
		return 1;
	    }
	}
	closedir(dir);
    }
    if(!conf_wifi)
	err = ESP_ERR_WIFI_CONN;
    else
	err = esp_wifi_sta_get_ap_info(ap_info);
    if(err == ESP_OK) {
	sprintf(ibuf, "Connected to %.32s\r\n", ap_info->ssid);
	if(writes(ofd, ibuf, 0))
	    return 1;
    } else
	if(writecs(ofd, "Currently not connected to AP\r\n"))
	    return 1;
    if(writecs(ofd, "\r\nWiFi SSID (*OFF*=disable") ||
       (wifi_cfg.sta.ssid[0] && (writecs(ofd, "; blank=") ||
				 writes(ofd, (char *)wifi_cfg.sta.ssid, 0))) ||
       writecs(ofd, ")? ") ||
       reads(ifd, ofd, ibuf, sizeof(wifi_cfg.sta.ssid), 1) < 0)
	return 1;
    if(!wifi_cfg.sta.ssid[0] && !ibuf[0])
	return writecs(ofd, "\r\n");
    if(!strcmp(ibuf, "*OFF*")) {
	if(conf_wifi) {
	    stop_wifi_serial();
	    if(ngp_task_id) {
		vTaskDelete(ngp_task_id);
		ngp_task_id = NULL;
	    }
	    /* FIXME: write "disable" into "wifi" config file, or something */
	    esp_wifi_deinit();
	}
	conf_wifi = 0;
	return 0;
    }
    int chg = ibuf[0] && strcmp((char *)wifi_cfg.sta.ssid, ibuf);
    if(chg) {
	strcpy((char *)wifi_cfg.sta.ssid, ibuf);
	read_wifi_cfg();
    }
    if(writecs(ofd, "\r\nWiFi Password (space=none, *OFF* = disable SSID)? ") ||
       reads(ifd, ofd, ibuf, sizeof(wifi_cfg.sta.password), 0) < 0)
	return 1;
    if(!strcmp(ibuf, "*OFF*")) {
	if(!chg) {
	    esp_wifi_disconnect();
	    wifi_cfg.sta.ssid[0] = 0;
	}
	sprintf(ibuf, "/config/%s.wifi", wifi_cfg.sta.ssid);
	int fd = open(ibuf, O_RDONLY);
	if(fd >= 0) {
	    close(fd);
	    if(writecs(ofd, "\r\nRemove from flash (Y/N)? "))
		return 1;
	    int yn = getyn(ifd, ofd);
	    if(yn < 0)
		return 1;
	    if(yn)
		remove(ibuf);
	}
	return 0;
    }
    if(ibuf[0] && strcmp(ibuf, (char *)wifi_cfg.sta.password) &&
       (ibuf[0] != ' ' || ibuf[1] || wifi_cfg.sta.password[0])) {
	if(ibuf[0] == ' ' && !ibuf[1])
	    ibuf[0] = 0;
	strcpy((char *)wifi_cfg.sta.password, ibuf);
	chg = 1;
    }
    if(chg) {
	esp_wifi_disconnect();
	esp_netif_dhcpc_stop(netif);
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
	esp_wifi_connect();
    }
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);
    writecs(ofd, "\r\nIP Address: ");
    esp_ip4addr_ntoa(&ip_info.ip, ibuf, sizeof(ibuf));
    writes(ofd, ibuf, 0);
    writec(ofd, '/');
    esp_ip4addr_ntoa(&ip_info.netmask, ibuf, sizeof(ibuf));
    writes(ofd, ibuf, 0);
    writec(ofd, '>');
    esp_ip4addr_ntoa(&ip_info.gw, ibuf, sizeof(ibuf));
    writes(ofd, ibuf, 0);
    esp_netif_dns_info_t dns_info;
    writec(ofd, '?');
    esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
    esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, ibuf, sizeof(ibuf));
    writes(ofd, ibuf, 0);
    if(writecs(ofd, "\r\nWiFi IP address: 0=dhcp; blank=keep; else ip[/mask][>gw][?ns]? ") ||
       reads(ifd, ofd, ibuf, sizeof(ibuf), 1) < 0)
	return 1;
    if(*ibuf) {
	parse_ip_info(ibuf, &ip_info, &dns_info);
	chg |= !!memcmp(&ip_info, &conf_ip, sizeof(ip_info)) ||
	       !!memcmp(&dns_info, &conf_dns, sizeof(dns_info));
    }
    if(chg) {
	if(!ip_info.ip.addr)
	    esp_netif_dhcpc_start(netif);
	else {
	    esp_netif_set_ip_info(netif, &ip_info);
	    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);
	}
    }
    sprintf(ibuf, "\r\nWiFi Serial Port (blank=%d,0=disable)? ", wifi_serial);
    if(writes(ofd, ibuf, 0) || reads(ifd, ofd, ibuf, sizeof(ibuf), 1) < 0)
	return 1;
    if(*ibuf) {
	int p = atoi(ibuf);
	if(p != wifi_serial) {
	    chg = 1;
	    stop_wifi_serial();
	    wifi_serial = p;
	    if(p)
		start_wifi_serial();
	}
    }
    if(chg) {
	/* FIXME: display new wifi config */
	if(writecs(ofd, "\r\nSave to flash (Y/N)? "))
	    return 1;
	int yn = getyn(ifd, ofd);
	if(yn < 0)
	    return 1;
	if(yn)
	    write_wifi_cfg();
    }
    writecs(ofd, "\r\n");
    return 0;
}

void wifi_connect(void)
{
    DIR *dir = opendir("/config");
    if(dir) {
	struct dirent *de;
	while((de = readdir(dir))) {
	    if(de->d_type != DT_REG)
		continue;
	    int len = strlen(de->d_name);
	    if(len < 6 || strcmp(de->d_name + len - 5, ".wifi"))
		continue;
	    break;
	}
	closedir(dir);
	/* No wifi config: don't bother starting wifi */
	if(!de)
	    return;
    }
    /* 1 retry only */
    int n;
    for(n = 2; n > 0; n--) {
	do_wifi_scan();
	int i;
	for(i = 0; i < ap_count; i++) {
	    strcpy((char *)wifi_cfg.sta.ssid, (char *)ap_info[i].ssid);
	    if(!read_wifi_cfg()) {
		wifi_cfg.sta.ssid[0] = 0;
		continue;
	    }
	    ESP_LOGW("tjm", "Connecting to %s", ap_info[i].ssid);
	    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
	    esp_err_t cerr = esp_wifi_connect();
	    /* FIXME: this does not wait until connected/failed */
	    /* This means I'm going to need that event handler stuff */
	    if(cerr != ESP_OK) {
		wifi_cfg.sta.ssid[0] = 0;
		continue;
	    }
	    if(conf_ip.ip.addr) {
		esp_netif_dhcpc_stop(netif);
		esp_netif_set_ip_info(netif, &conf_ip);
		esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &conf_dns);
	    }
	    break;
	}
	if(i != ap_count)
	    break;
	if(n > 1)
	    ESP_LOGW("tjm", "WiFi failed to init; trying again");
	vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    if(!n)
	return;
    if(wifi_serial)
	start_wifi_serial();
    if(ngp_addr)
	xTaskCreate(ngp_task, "input_client", 1024*3, NULL, configMAX_PRIORITIES, &ngp_task_id);
}

static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif = esp_netif_create_default_wifi_sta();
    assert(netif);

    /* FIXME: set up event handlers as per station example. */
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());
    /* FIXME: hold off 'w' command until this point */
    /* (or later if event needed) */

    wifi_connect();
}

/***** Main UI task ("serial port") *****/
static int log_level = CONFIG_LOG_DEFAULT_LEVEL;
static const char * const log_name[] = {
    "None", "Error", "Warn", "Info", "Debug", "Verbose"
};

/* Note that if config is changed in more than one serial session at a time
 * things will go wrong (no locks).  I have no intention of fixing this. */
static void serial_loop(int ifd, int ofd)
{
    char ibuf[80]; /* on stack to make this "reentrant" */
    if(writecs(ofd, "Hello, this is aqctl!\r\n"))
	return;
    while (1) {
	int c = readc(ifd);
	if(c < 0)
	    return;
	switch(c) {
	  /* basic protocol:
	   *   wait for serial to die down, or ignore unexpected input
	   *   send a; verify response is "\r\nAquarius\r\n"
	   *   repeat:  send r or c to get current stick values
	   */
	  case 'a':
	  case 'A':
	    if(writecs(ofd, "\r\nAquarius\r\n"))
		return;
	    break;
	  case 'r':
	  case 'R': {
	    char obuf[5];
	    sprintf(obuf, "\r=%02X", read_pad());
	    writes(ofd, obuf, 4);
	    break;
	  }
	  case 'c':
	  case 'C': {
	    uint8_t x, y, b;
	    read_pad_mapped(&x, &y, &b);
	    sprintf(ibuf, "\rx%02Xy%02Xb", (int)x, (int)y);
	    for(int i = 0; i < 6; i++)
		ibuf[i + 8] = b&(1<<i)?i+'1':' ';
	    writes(ofd, ibuf, 14);
	    break;
	  }
	  /* Bluetooth setup */
	  case 'b':
	  case 'B':
	    if(bt_setup(ifd, ofd))
		return;
	    break;
	  case 'j':
	  case 'J':
	    if(hid_setup(ifd, ofd))
		return;
	    break;
	  /* WiFi setup */
	  case 'w':
	  case 'W':
	    if(wifi_setup(ifd, ofd))
		return;
	    break;
	  case 'n':
	  case 'N':
	    if(ngp_setup(ifd, ofd))
		return;
	    break;
	  case 'v':
	  case 'V': /* CONFIG_LOG_MAXIMUM_LEVEL */
	    if(log_level == CONFIG_LOG_MAXIMUM_LEVEL)
		log_level = CONFIG_LOG_DEFAULT_LEVEL;
	    else
		log_level++;
	    esp_log_level_set("*", log_level);
	    writecs(ofd, "\r\nLog set to ");
	    writes(ofd, log_name[log_level], 0);
	    break;
	  default:
	    writec(ofd, '\a');
	}
    }
}

static void serial_task(void *arg)
{
    serial_loop((int)arg, arg ? (int)arg : 1);
    if(arg)
	close((int)arg);
    vTaskDelete(NULL);
}

void app_main(void)
{
    init_uart();
    init_gpio();

    /* needed for netowrking, and maybe other stuff */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_configfs();
    init_bluetooth();
    init_wifi();
}
