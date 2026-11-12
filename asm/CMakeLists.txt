find_program(SDLDZ80 sdldz80)
find_program(SDASZ80 sdasz80)

set(IHX2BIN "${CMAKE_BINARY_DIR}/utils/ihx2bin")
set(XOR "${CMAKE_BINARY_DIR}/utils/xor")

set(MACDIR "${CMAKE_SOURCE_DIR}/asm")
set(MAC "${MACDIR}/aquarius.mac")

set(SDASZ80_OPTS -s -l -p -o "-I${MACDIR}")

# Pass in base names of .as files to convert to .ihx
function(as2ihx)
  foreach(f ${ARGV})
    set(rel "${CMAKE_CURRENT_BINARY_DIR}/${f}.rel")
    set(as "${CMAKE_CURRENT_SOURCE_DIR}/${f}.as")
    if(NOT EXISTS "${as}")
      set(as "${CMAKE_CURRENT_BINARY_DIR}/${f}.as")
    endif()
    add_custom_command(OUTPUT "${rel}" DEPENDS ${f}.as "${MAC}"
      COMMAND ${SDASZ80} ${SDASZ80_OPTS} "-I${CMAKE_CURRENT_SOURCE_DIR}"
              "${f}.rel" "${as}" VERBATIM)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${f}.ihx"
      DEPENDS "${rel}" COMMAND ${SDLDZ80} -n -i "${rel}" VERBATIM)
  endforeach()
endfunction()

# pass in basename of ROM/TAPE and type (ram or rom), followed by sources
# basically inserts do${t}.as since sdas doesn't support -D
# FIXME: generalize to support 0 or more conditionals
# FIXME: instead of hard-coding .mac as includes, allow others as well
#        In fact, make this a "real" compiler with auto-deps
# FIXME: better setting of per-target flags (like -z)
function(ram_or_rom b t)
  set(deff "${MACDIR}/do${t}.as")
  set(srcf "${ARGN}")
  set(opts "${ARGN}" -s -l -p -o)
  list(FILTER srcf EXCLUDE REGEX [[.*\.mac$|^-]])
  list(FILTER ARGN EXCLUDE REGEX [[^-]])
  list(FILTER opts INCLUDE REGEX [[^-]])
  list(TRANSFORM srcf PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
  set(rel "${CMAKE_CURRENT_BINARY_DIR}/${b}-${t}.rel")
  add_custom_command(OUTPUT "${rel}"
    DEPENDS ${ARGN} ${MAC} "${deff}"
    COMMAND ${SDASZ80} ${opts} "-I${MACDIR}" "-I${CMAKE_CURRENT_SOURCE_DIR}"
                       ${b}-${t}.rel "${deff}" ${srcf} VERBATIM)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${b}-${t}.ihx"
    DEPENDS "${rel}"
    COMMAND ${SDLDZ80} -n -i "${b}-${t}.rel" VERBATIM)
endfunction()

if(SDLDZ80 AND SDASZ80)
  subdirs(sndtest exc cam80-12 eb)
else()
  message(WARNING "SDCC Not found; Assembly programs will not be built.")
endif()

