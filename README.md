# Aq: The Mattel / Radofin Aquarius emulator

This project is a console (Mattel | Radofin) *Aquarius* computer emulator.  It
was created for nostalgia purposes, but also as potentially part of a larger
and more proper open source emulator project.

This version mostly just switching over to SDL2 rather than SDL1 and more
keyboard work. Most of the work in the early versions was on getting basic
functionality working and correctness.

| | Command line options: |
| :- | :- |
| `-l` | Open log file (./log) |
| `-i` | Open I/O log file (./ilog) |
| `-b` *file* | Read basic file as input on startup. |
| `-g` *ROMfile* | Load (game) ROM into ROM memory area on startup. |
| `-t` *tapefile* | Sets tapefile as tape input.  Read when CLOAD is used. |
| `-k` | Sets keyboard input mode to scan mode rather than timer mode. |

Requirements:
ncurses (w/ wide character support)
SDL
portaudio

radofin.bin	- The BASIC ROM image for the Aquarius computer.
