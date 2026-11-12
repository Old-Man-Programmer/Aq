# Using per-config man page adjustments intead of a man page with "if
# you compiled with..." qualifiers
if(USE_IMGUI)
  set(IMGUI_FONT_DOC [[

.TP
"\fBimfont\fP": \fIstring\fP
Change the Dear ImGui UI font.  The format is either a FontConfig font
search pattern (see \fBfc_match\fP(1)), or a TrueType font file name.
The default is Sans (the default system-dependent sans-serif font).
.PP
.TP
"\fBimbold\fP": \fIstring\fP
Change the Dear ImGui UI bold font.  The format is the same as for
"\fBimfont\fP".  The default is to try tacking on \fB:bold\fP to the
main font; if that fails, the main font is used with coloring.
.PP
.TP
"\fBimitalic\fP": \fIstring\fP
Change the Dear ImGui UI italic font.  The format is the same as for
"\fBimfont\fP".  The default is to try tacking on \fB:italic\fP to the
main font; if that fails, the main font is used with coloring.  This
is currently unused.
.PP]])
else()
  set(IMGUI_FONT_DOC)
endif()

if(USE_QT)
  set(GUI_STATE_FILE [[

\fIconfig\fP\fB/qtgui.json\fP	GUI saved state]])
elseif(USE_IMGUI)
  set(GUI_STATE_FILE [[

\fIconfig\fP\fB/imgui.json\fP	GUI saved state]])
else()
  set(GUI_STATE_FILE)
endif()

foreach(m Aq.1 Aq_binding.5)
  configure_file(${m}.in "${CMAKE_CURRENT_BINARY_DIR}/${m}")
  string(REGEX REPLACE [[^.*\.]] "" s "${m}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${m}" DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man${s}")
endforeach()
