Prerequisites
=============

The system and cartridge ROM files are not provided.  You will at
least need a BASIC ROM, by default named `radofin.bin`, and a
character ROM, by default named `Aquarius-chars.rom`, placed in the
default search path.

Creating tapes requires running `Aq`, which, in turn, requires the
above-mentioned ROMs either in the XDG directory or in the source
tree's `roms` sub-directory.

The base configuration requires SDL2 (and its associated development
headers, for distributions which package these separately).  It was
tested with 2.0.18 and 2.26.  It also requires CMake; the minimum
version required is 3.21.

For the Qt GUI, use Qt5.  Qt5 is likely already installed by your Linux
distribution, although you may need to manually install the
development package(s.  This was built and tested using Qt-5.15.  Note
that the Qt interface requires a minimum SDL2 version of 2.0.22.

For the Dear ImGui GUI, three separate packages are required:
[Dear ImGui](https://github.com/ocornut/imgui),
[ImGuiFileDialog](https://github.com/aiekick/ImGuiFileDialog) and
[imgui_markdown](https://github.com/juliettef/imgui_markdown).  It is
technically possible to get Dear ImGui as a precompiled package, maybe
even on your Linux distribution, but it is not likely that it is
current or complete.  It is best to just download all of these
projects and use the source form.  It also uses the `fontconfig` and
`freetype2` libraries, but these should already be present on any
modern system with X11 (as the standard Xft font library depends on
them as well).  Of course you will need the development packages for
these on systems which distribute that part separately (e.g.
`libfontconfig-dev` on Debian systems).

Building the assembly programs (sample ROMs and sound tests) requires
the [Small Device C Compiler Suite for the
Z80](https://sdcc.sourceforge.net/) (note: 4.2.0 works, but 4.3.0
might fail due to incorrect math).  Building the relocated Extended
BASIC ROM also requires xdelta (not xdelta3; 1.1.x is good) and
[z80dasm](https://www.tablix.org/~avian/blog/articles/z80dasm/) (note:
tablix may be gone; if so, you can get it from an old Debian pool).  If
SDCC is not found, the assembly programs will not be built, and if
xdelta is not also found, the relocated Extended BASIC ROM will not be
built.

Quick creation
==============
The script `mk` will attempt to build Aq in all possible configurations,
those being:

>
 `build/n`        - No GUI
 `build/i`        - Build with ImGUI
 `build/q`        - Build with Qt
 `build/l[niq]`   - Build the above w/ clang rather than gcc.


Building Using GNU `make`
=========================

It is recommended that you build using CMake.  However, there is a
convenience makefile for use with GNU `make`.  If it works for you,
great.  Otherwise, see [below](#building-using-cmake) on using CMake.
To build without GUI (creates and builds in `build` subdirectory):

>     make -j9 && sudo make install

To build with Qt (creates and builds in `build$(QT)` subdirectory,
`buildq` in this case):

>     make -j9 QT=q && sudo make QT=q install

To fetch or update the Dear ImGUI prerequisites:

>     make external

To build with Dear ImGui (creates and builds in `build$(IMGUI)`
subdirectory, `buildi` in this case):

>     make -j9 IMGUI=i && sudo make IMGUI=i install

In addition, you can remove the build directories with:

>     make clean

and remove the external downloads as well with:

>     make distclean

Building Using CMake
====================

To build, just use `cmake`:

>     cmake -S . -B build
>     cmake --build build -j9
>     sudo cmake --install build

Of course modify the above procedure for any special needs you have.  I
also highly recommend using `ccmake` or `cmake-gui` instead of `cmake` for
interactive configuration.  One major special need is the GUI wrapper.
By default, there is none.  If you want a menu and file request popups,
use either [Qt](#qt) or [Dear ImGui](#dear-imgui).

Another common change to the procedure is to create a package or
binary tarball.  For example, replace the last (install) line with:

>     DESTDIR=/tmp/aq$$ cmake --install build
>     (cd /tmp/aq$$ && tar cjf /tmp/Aq.tar.bz2 *)
>     rm -rf /tmp/aq$$

Qt
--

Replace the `cmake` line above with:

>     cmake -S . -B build -DUSE_QT=ON

Dear ImGui
----------

Download the prerequisites and place them where cmake can find them. 
The preferred location is the same directory as the Aq source tree
(not inside the Aq source tree itself, though, although that works if
you want it that way).  Since these are highly volatile git
repositories, you may need to check out a specific, compatible
version.  It is recommended to just use the convenience makefile for
this, since it has the last tested commit hard-coded for checkout.
Feel free to try something newer (or older, if you like), though.  The
makefile actually checks these out into the `external` subdirectory of
the source tree, and updates them if already there.

>     make external

Replace the `cmake` line above with:

>     cmake -S . -B build -DUSE_IMGUI=ON

If you have a precompiled, incompatible version of Dear ImGui already
installed on your system, it might be selected instead of the git
repository.  In that case, override with `-DIMGUI_LIB=` (the default
when using the supplied makefile):

>     cmake -S . -B build -DUSE_IMGUI=ON -DIMGUI_LIB=
