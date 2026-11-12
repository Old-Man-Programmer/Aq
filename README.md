# Aq: The Mattel / Radofin Aquarius emulator

This project is a console (Mattel | Radofin) *Aquarius* computer emulator.  It
was created for nostalgia purposes, but also as potentially part of a larger
and more proper open source emulator project.

Changes in this version are shown in the CHANGES file.

| | Command line options: |
| :- | :- |
| `-l` | Open log file (./log) |
| `-i` | Open I/O log file (./ilog) |
| `-b` *file* | Read basic file as input on startup. |
| `-g` *ROMfile* | Load (game) ROM into ROM memory area on startup. |
| `-t` *tapefile* | Sets tapefile as tape input.  Read when CLOAD is used. |
| `-k` | Sets keyboard input mode to scan mode rather than timer mode. |
| `-E` | Use special extended basic ROM (ebasic.bin) mapped at 0x2000 |
| `-B` | Run programs in "batch" mode, i.e. run in turbo w/o a screen |


asm/
├── aquarius.mac - Aquarius assembly definitions and macros
├── cam80-12/    - CamelForth for the Z80, ported to Aquarius
├── eb/          - Disassembly of the Extended Basic ROM w/ modifications to
│                  load at 0x2000
├── exc/         - Zexall -- The Z80 exerciser ported to Aquarius
└── sndtest/     - Some AY-3-8910/8912 sound testing programs


Requirements:
ncurses (w/ wide character support)
SDL2
portaudio

