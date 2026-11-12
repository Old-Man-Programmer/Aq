cmake_minimum_required(VERSION 3.21.0)

project(Aq VERSION 0.6 LANGUAGES CXX C)

set(USE_QT NO CACHE BOOL "Use Qt for the GUI")
set(USE_IMGUI NO CACHE BOOL "Use Dear ImGui for the GUI")
if(USE_QT AND USE_IMGUI)
  message(SEND_ERROR "Only select QT, ImGui, or neither")
endif()

set(DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}
    CACHE PATH "Default path to data files (ROMs, etc.)")
set(ROM_INSTALL_DIR ${DATA_DIR}/roms)
set(TAPE_INSTALL_DIR ${DATA_DIR}/tapes)
set(BASIC_INSTALL_DIR ${DATA_DIR}/bas)

# The default is to build with no debug or optimization options
# A better default is to build with both, if available
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  if(CMAKE_CXX_FLAGS_RELWITHDEBINFO)
    set(CMAKE_BUILD_TYPE RelWIthDebInfo CACHE STRING "" FORCE)
  else()
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
  endif()
  # Provide the standard build types as a selection list
  # This should really be part of standard cmake.
  # If a custom build type is added, just add the type and change this as well.
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
               STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
#set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTORCC ON)
if(USE_QT)
  find_package(Qt5 COMPONENTS Widgets REQUIRED)
  set(GUI_SRC qtgui.cpp)
  set(GUI_LIB Qt5::Widgets)
endif()
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
function(find_local_file v f d) # ...suf...
  find_file(${v} ${f} PATHS ${CMAKE_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/external ${CMAKE_SOURCE_DIR}
    NO_DEFAULT_PATH PATH_SUFFIXES ${ARGN} DOC "${d}")
  find_file(${v} ${f} PATH_SUFFIXES ${ARGN} DOC "${d}")
endfunction()
if(USE_IMGUI)
  # imgui.h must come from somewhere
  # FIXME: test-compile to find version # (should be >=1.89.3)
  # Technically, it could compile with plain v1.89 if I looked for sdl/sdl2
  # In fact, I did most of my initial tests w/ 1.88 (but no key nav in fd)
  find_local_file(IMGUI_H imgui.h "Dear ImGui base library header" imgui)
  # imgui itself can either be a set of cpp files or a precompiled library
  find_library(IMGUI_LIB imgui DOC "Dear ImGui precompiled library")
  find_local_file(IMGUI_CPP imgui.cpp "Dear ImGui core library source" imgui)
  if(NOT IMGUI_H OR (NOT IMGUI_CPP AND NOT IMGUI_LIB))
    message(SEND_ERROR "Dear ImGui not found.\n"
                       "See INSTALL.md or just \"make external\".")
  endif()
  # use freetype; it's a shared lib, so no real extra space
  # well, there's no advantage, either, so make it optional
  set(FONT_LIBRARIES)
  set(USE_FT2 YES CACHE BOOL "Use Freetype2 for font rendering")
  if(USE_FT2)
    include(FindPkgConfig)
    pkg_check_modules(FT2 freetype2 REQUIRED)
    add_compile_definitions(IMGUI_ENABLE_FREETYPE)
    add_compile_options(${FT2_CFLAGS})
    list(APPEND FONT_LIBRARIES ${FT2_LIBRARIES})
  endif()
  # use fontconfig to avoid having to remember file names
  include(FindPkgConfig)
  pkg_check_modules(FC fontconfig REQUIRED)
  add_compile_options(${FC_CFLAGS})
  list(APPEND FONT_LIBRARIES ${FC_LIBRARIES})
  # file selector widget
  # FIXME: test-compile to find version #
  # Unfortunately, it stopped updating tags, so I'm not sure it's accurate
  find_local_file(FILE_DIALOG_H ImGuiFileDialog.h "ImGuiFileDialog header"
                  imgui ImGuiFileDialog ImGuiFileDialog/ImGuiFileDialog)
  find_local_file(FILE_DIALOG_CPP ImGuiFileDialog.cpp
                  "ImGuiFileDialog source; if blank: assumed in main library"
                  imgui ImGuiFileDialog ImGuiFileDialog/ImGuiFileDialog)
  if(NOT FILE_DIALOG_H)
    message(SEND_ERROR
     "ImGuiFileDialog.h not found.\n"
     "See INSTALL.md or just \"make external\".")
  endif()
  # Markdown widget
  # Version?
  # Using 61a181bdb83f450f852f7cf5d1282d8cda1c0f57
  find_local_file(IMGUI_MD_H imgui_markdown.h "imgui_markdown header"
                  imgui imgui_markdown)
  if(NOT IMGUI_MD_H)
    message(SEND_ERROR
    "imgui_markdown.h not found.\n"
    "See INSTALL.md or just \"make external\".")
  endif()
  cmake_path(SET IMGUI_H ${IMGUI_H})
  cmake_path(GET IMGUI_H PARENT_PATH IMGUI_H)
  cmake_path(GET FILE_DIALOG_H PARENT_PATH FILE_DIALOG_H)
  cmake_path(SET IMGUI_MD_H ${IMGUI_MD_H})
  cmake_path(GET IMGUI_MD_H PARENT_PATH IMGUI_MD_H)
  include_directories(${IMGUI_H} ${FILE_DIALOG_H} ${IMGUI_MD_H})
  # Flags for ImGuiFileDialog.  Too much trouble to limit to specific files
  # Note that just adding ImGuiFileDialogConfig.h to source dir doesn't work(?)
  add_compile_definitions(USE_BOOKMARK USE_EXPLORATION_BY_KEYS
                          USE_DIALOG_EXIT_WITH_KEY IGFD_EXIT_KEY=ImGuiKey_Escape)
  set(GUI_SRC dimgui.cpp ${FILE_DIALOG_CPP})
#  if(FILE_DIALOG_CPP)
#    cmake_path(SET FILE_DIALOG_CPP ${FILE_DIALOG_CPP})
#    cmake_path(GET FILE_DIALOG_CPP PARENT_PATH CUSTOM_FONT_CPP)
#    cmake_path(GET CUSTOM_FONT_CPP PARENT_PATH CUSTOM_FONT_CPP)
#  endif()
  if(NOT IMGUI_LIB)
    foreach(x .cpp _draw.cpp _widgets.cpp _tables.cpp)
      string(REPLACE .cpp ${x} x ${IMGUI_CPP})
      list(APPEND GUI_SRC ${x})
    endforeach()
    cmake_path(REMOVE_FILENAME IMGUI_CPP)
    # Freetype is included with FontConfig, so may as well use it.  It
    # reduces the build size a tiny bit.  No need for a redundant renderer.
    set(IMGUI_FT "${IMGUI_CPP}/misc/freetype")
    list(APPEND GUI_SRC "${IMGUI_FT}/imgui_freetype.cpp")
    include_directories("${IMGUI_FT}")
    set(IMGUI_STL "${IMGUI_CPP}/misc/cpp")
    list(APPEND GUI_SRC "${IMGUI_STL}/imgui_stdlib.cpp")
    include_directories("${IMGUI_STL}")
    set(IMGUI_BE "${IMGUI_CPP}/backends/imgui_impl_")
    # Note: *sdl.* was renamed to *sdl2.* in 1.89.3.  Very annoying.
    foreach(x sdl2.cpp opengl3.cpp)
      list(APPEND GUI_SRC "${IMGUI_BE}${x}")
    endforeach()
    set(IMGUI_LIBS)
  else()
    set(IMGUI_LIBS ${IMGUI_LIB})
  endif()
  set(GUI_LIB ${IMGUI_LIBS} ${FONT_LIBRARIES})
endif()

subdirs(roms utils asm bas doc)

# I don't really know how to add these to the default clean target.
# Not that the default clean target is very good, anwyay .  Just build
# out-of-tree.
# clean: rm -f ilog log .errors
# distclean is definitely out:  just use out-of-tree builds.

# this target was unused in old Aq makefile, since there was no .files.
#file(STRINGS .files ARCHIVE_FILES)
#add_custom_target(archive
#    COMMAND ${CMAKE_COMMAND} -E tar zcf ../Aq-${PROJECT_VERSION} ${ARCHIVE_FILES}
#    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
#    DEPENDS clean)

if(USE_QT)
# Die on use of any symbols deprecated before version 6
add_compile_definitions(QT_DISABLE_DEPRECATED_BEFORE=0x060000)
# Warn about any other deprecated symbols
add_compile_definitions(QT_DEPRECATED_WARNINGS)
endif()

set(JSON_LIB libjson/json.c libjson/parse.c libjson/lex.c libjson/print.c)
include_directories(${CMAKE_SOURCE_DIR}/libjson)

add_executable(Aq
  main.c z80.c mem.c screen.c io.c aqsnd.c aqjoy.c xdg.c config.c
  gui_common.cpp sdlevent.cpp ${GUI_SRC} ${JSON_LIB})
target_link_libraries(Aq ${GUI_LIB} SDL2::SDL2 OpenGL::GL m dl pthread)
install(PROGRAMS "${CMAKE_BINARY_DIR}/Aq" TYPE BIN)

configure_file(config.h.in "${CMAKE_CURRENT_BINARY_DIR}/config.h")
