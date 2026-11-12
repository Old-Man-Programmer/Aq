TODO
====

Immediate
---------

 - update menu shortcut display to current binding (currently just
   default)
   - especially in qt, where the displayed shortcut is active
 - Get keyboard nav working properly in imgui.  Especially OK/Cancel as
   Enter if no text widget selected/Esc
   - Tab switching should be hotkeyable
   - buttons should be hotkeyable (e.g. _Ok -> alt+o)
   - retest keyboard nav in menus
   - ensure correct item focused by default
 - GUI keyboard/controller remapper
   - review basic GUI:
     - imgui: (re)size dialog properly; shift-key labels
     - qt: allow resizing & save size between invocations
   - qt: chording support (binding display, chord list)
   - Add, Find buttons:
     - popup win: text displaying accumulated event so far, followed by
       checkboxes for mods, followed by ok/cancel
       - mod checkboxes for "ignore"
     - add tacks to binding string; find switches to binding or says
       not found.
   - apply -> add to temporary config & bind immediately
   - when changing to new target, if binding string was changed, but
     not applied, warn user (Changed: Abandon/Save/Cancel)
   - primary OK/cacnel:
     - if no changes, both just close dialog
     - if changes, display dialog with list of changes.  Canceling this
       dialog resumes bind dialog.  OK saves temp config or undoes all
       immediate bindings by reloading from defaults+config
   - originally planned to include default implicit bindings for kb:
     add to displayed bind string, and filter out on apply.  There are
     many complications, though, such as not really knowing what keys
     are actually implicitly bound (e.g. <> key on european keyboards).
   - "controllers" config?
     - maybe tab, maybe separate dialog
       - top = pad select (number spinner)
       - next = text widget for general text entry
       - next = add/remove/update buttons
       - next =
         - left: list of patterns
	 - middle: buttons: add, add guid
	 - right: list of currently attached gamepads
 - Include file support in GUI?
   - set "current write" file: all changed config goes here
   - create new config file as sub-file, and set as current write
   - toggle file's read-only flag
   - add new "included" file
 - Config dependent on loaded cartridge
   - main config still written to if read-only
   - if not read-only, any changes to config written to cart. config
 - Imgui font selector dialog?
 - QT GUI still stalls emulator sometimes (probably due to global lock(s))
 - Test gamerom popup blankable
 - Test other ROM popups not blankable
 - Maybe document/install the utils as well
   - ihx2bin (-> aqihx2bin?)
   - asm2bas (-> aqasm2bas?)
   - aquarius.mac, doram.as, dorom.as -> <data>/asm
   - tape.cmake -> ?
     - actually, maybe provide a simple cmake-based build system for
       ROMs, BASIC, and tapes based on what Aq uses

Long Term
---------

 - License check (this whole thing is probably illegal)
 - Pick a GUI (or just config+hotkeys).  Only support that.
   - note: no GUI means no way to change tapes/roms while running
 - GDB remote debugging support
 - Disk drive support (QuickDisk)
 - Filesystem-based "tape files" (i.e., files whose name match the
   name on tape).  Probably part of the disk drive project.
 - Feature parity check with other available emulators (mame, the open
   source FPGA version in general circulation (specifically MiST[er]),
   two close-source, but free of change Windows-only ones that I know of).
 - Macros: record/write/edit key sequences with timing and bind to a hotkey
   - have a "stack" (how big?) and display all macros binding list
 - Serial port support
   - Printer support
 - Clipboard support (drag-select text for copy; paste -b-style on
   middle mouse/bound key)
 - Record video/screenshot/sound to file (ffmpeg's libavcodec)
 - Separate vol control for expander and bitbang (tape is always muted/turbo)
 - Color settings
 - GUI magnification level (IMGUI) (currently supports ctrl-wheel
   zooming at least, as well as font selection)
 - Save/load state.  Or at least mem image.
 - Finish bas2tape/tape2bas (don't remember their current state)
 - Gamepad hot plug support, or at least hot reconnect support
