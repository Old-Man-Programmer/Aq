ESP32 Aquarius Controller Adapter
=================================

This is an adapter for an Aquarius game pad, running on an ESP32
development board (apparently a DOIT ESP32 DEVKIT V1).  Originally, it
was meant to be a Tang Nano FPGA development board, but I was unable
to get USB serial working on that. The ESP32 is cheaper, anyway, and
can do much more.  Apparently the board I'm using as a DOIT ESP32
DEVKIT V1 or a clone thereof.

The ESP32 side can be compiled using ESP IDF.  I used the latest
stable at the time of writing, 5.0.  It probably doesn't work on any
other version, since the IDF is not very stable in general, like most
modern free software projects (in fact, this project was started with
4.4.2 and 4.4.3, which were compatible with each other, but 5.0 made
many gratuitous API, build system, and even compiler behavior
changes).  To build and flash (not necessary to do separately, since
the `flash` command depends on successful `build` internally, but I do
it so that it doesn't try to access the serial port until building is
complete):

   idf.py build && idf.py flash

The IDF's `esp_hid` component crashed for me, which was solved by
increasing the event task stack sizes.  I copied components/esp_hid
from the main IDF here and applied `esp_hid.patch`.  I suggest
re-copying and re-patching for newer IDF versions rather than keeping
this copy.

Originally this was meant to be a serial-over-USB device that used a
user-space driver.  The source for this is `aqdrv.c`; compile with:

    gcc -O2 -o aqdrv{,.c}

Run `aqdrv` as someone who can access `/dev/uinput` (or change the
permissions on `/dev/uinput`, which is just asking for much bigger
security issues than running an open source, simple driver as `root`).
It takes two optional parameters, in any order:  the mapping type and
the serial device name.  Naturally, it needs to run with privilege to
access the serial device, as well.

If no mapping type is specified, it uses the default (just straight
mapping: XY ABCXYZ).  Otherwise it can specify the Aq emulator's port
A (XY XYRtABRt2) or B (ZRz HlHuLtHdHrLt2) using that letter
(case-insensitive).  

IF no serial device is given, it uses `/dev/ttyACM0`.  If the serial
device can't be opened, it will first try prepending `/dev/` and
`/dev/tty`.  It also supports `-` for standard input/output, although
I do not recommend this (even though my first version *only* supported
that, it's more trouble than it's worth).  If the device stll can't be
opened, `aqdrv` will retry indefinitely every 5 seconds.

Since it's an ESP32, I also support wireless access.  I tried to
support BLE, BT Classic, and WiFi.

By default, it starts up HID on BLE and BT Classic.  On my system, it
requires manual connection via `bluetoothctl` when starting cold, but
it re-attaches after that, usually.  This is unlike my other bluetooth
devices, which auto-attach on powerup without intervention other than
the initial pairing.  I have no idea why; this is supposed to just
work, so there is no help I could find on the Internet.

It also, by default, starts up a standard (RFCOMM) serial port (SPP)
over BT Classic.  There is no standard for BLE serial ports, so I did
not do this.  There is a "driver" for several BLE vendor protocols,
but I won't support it (among other things, I am heavily opposed to
using Python for device drivers, or anything else, for that matter). 
Operation is idential to the default USB serial port, except of course
using `/dev/rfcomm0`.  For some reason, though, even though the
permissions are identical, not being part of the `dialup` group on my
system gives a permission denied error for running `screen` on
`rfcomm0`, but not for `ttyUSB0` or `ttyACM0`.  The main other
difference is that system logging is only done on the built-in serial
port.  Connecting to SPP is done using the `rfcomm` command, or via
your bluetooth GUI's serial access funtion, presumably (I haven't
tested this).  For example, to bind (connect on first access;
disconnect when device closed) `rfcomm0` to SPP device 1, and then
start the driver:

    sudo rfcomm bind 0 94:B9:7E:C3:49:12 1
    sudo ./aqdrv a rfcomm0

For WiFi, there is also a service that acts like the serial port
running on port 28010 (mnemonic for Z80A) by default (but
configurable).  If all of the attempts at opening a device fail,
`aqdrv` will try interpretning the device sas *host* or
*host*`:`*port* before giving up and waiting 5 seconds.

    sudo ./aqdrv a 192.168.1.220

In addition, I have in the past used `network-gamepads` for remote
input devices on Linux, so I have added a client implementation,
adapted from its `input-client.c`.  You need to configure a target IP
address/host name, port, and optional (clear text) password for a
machine running `input-server`.  The main advantage this has over
`aqdrv` is that it doesn't require a Linux-side driver written by me.
However, it is not standard on most distributions, so it does still
require effort.  Note that if you kill `input-server` or otherwise
lose connection, reconnection will not happen until a change occurs on
the controller.  The socket status is only chacked when the change
event is sent.  You will likely also have to set the listen address to
`0.0.0.0` instead of the default `:::`, since I do not use/suppport
IPv6 on the ESP32 side.

    sudo input-server -b 0.0.0.0 &

License
-------
As mentioned in the files, all code and documentation was created and
granted to the Public Domain by Thomas J. Moore in 2022-2023, except
as noted.  In paritcular, `components/esp_hid` and
`main/esp_hid_gap.[ch]` are almost exactly extracted from the
Espressif ESP IDF.  Also, 'main/ngp_protocol*` is mostly from
`network-gamepads`, and `input-event-codes.h` comes from the exported
Linux kernel headers on my current system.  See those files for
copyright information.

Hardware
========
The hardware consists of the dev board with a DB25 cable (maybe an old
printer cable?) attached (it's what I had available), which is then
attached to a DB25-to-DB9 serial adapter.  The end result 9 GPIO pins
attached to the 9 DB9 pins, one of which is permanently output as low,
and the other 8 of which are essentially the Aquarius game port.  In
short:

    Aq  DB9  DB25  ESP32
    GND 1    8     GPIO23
    0   2    3     GPIO22
    1   3    2     GPIO17
    2   4    20    GPIO13
    3   5    7     GPIO21
    4   9    22    GPIO33
    5   8    5     GPIO32
    6   7    4     GPIO18
    7   6    6     GPIO19

Serial Commands
===============
Commands are entered as single characters on any "serial" interface:

System
------
 - Esc-Esc-Esc: Reboot (any time, even in middle of other input).
   Naturally, this only works if the ESP32 is responding to serial
   input at all; if not, the `EN`/reset button must be used.
 - v/V(erbose): toggle main serial port log level (warning/info/debug)
   Note that this may not work, since I will probably remove most tracing.

Basic Gamepad Protocol
----------------------
 - a/A(q) - print "\r\nAquarius\r\n" (to verify it's me)
 - r/R(aw) - print "\r=XX" where XX is the Aquarius gameport value, in hex
 - c/C(ooked) - print "\rx<n>y<n>b<123456>"; n = axis value, 1-6=1-6 or space

Bluetooth configuration
-----------------------
 - b/B(t) - set BT mode:
     - l/L) Toggle BLE HID (FIXME: maybe mutually exc. w/ classic HID)
     - c/C) Toggle classic BT HID
     - s/S) Toggle classic BT Serial (SPP)
     - w/W) Save config & reboot if boot mode changed
 - j/J(oymap) - set HID mapping:
     - 1) Aq joystick 1: ax=XY b=XYRt ABRt2
     - 2) Aq joystick 2: ax=RxRy b=HlHuLt HdHrLt2
     - 3) Simple joystick: ax=XY b=ABCXYZ
     - b/B) Toggle boot mode (single/all)
     - w/W) Save config & reboot if boot mode changed

WiFi
----
  - w/W(iFi) - interactive WiFi connect config
    Each SSID has its own config, and the strongest configured
    interface is connected.   Each config has password and IP address/port
    config, as well as network-gamepads config (below).
  - n/N(g) - interactive network-gamepads config
    IP=0: disable
    Port=0/blank: 9292
    password (blank=no passwd)
    Note that the Bluetooth HID mode affects the ngp client behavior.

Known Bugs
==========
  - For me at least, BT doesn't auto-connect on cold boot, ever.  It's
    supposed to "just work", so I have no idea what's wrong.
  - For me at least, if both BLE and BT Classic are enabled, BLE is
    ignored.  So, to use BLE, both BT Classic HID and SPP must be
    disabled, probably.
  - Sometimes (actualy, most of the time on cold boot) it fails to
    connect WiFi.  A reboot is often necessary.  Instead, it should
    probably retry every few seconds as long as there are configured
    SSIDs.
  - As of now, static IP config has not been tested, and is therefore
    likely broken.  In particular, dhcpc startup and shutdown is handled
    by the default handlers, and also by my code, and I'll have to
    rearchitect to make my own event handlers to make it work properly.

TODO
====
  - Binary protocol, or at least no use of printf
  - Feature: "smartconfig" for wifi (requires Android device)
  - Better handling of key rollover
  - Maybe support PIN in BT config
  - IPv6 over WiFi
  - Serial continuous output mode (only on change)
  - Dynamic enable/disable of BT, WiFi
