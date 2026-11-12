set(TAPE "${CMAKE_CURRENT_BINARY_DIR}/tape")
set(TAPE_BAS "${TAPE}.bas") # must be tape.bas (hard-coded in tape.cmake)

set(TESTS car lowc shot2 siren whis expl scale shot snd440 jstest)
set(BAS ${TESTS})
list(TRANSFORM BAS APPEND .bas)
list(TRANSFORM BAS PREPEND "${CMAKE_CURRENT_BINARY_DIR}/")
install(FILES ${BAS} DESTINATION "${BASIC_INSTALL_DIR}")

add_custom_command(OUTPUT "${TAPE_BAS}" DEPENDS ${BAS}
   COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/bas/tape.cmake -- ${BAS})
add_custom_target(basic DEPENDS "${TAPE_BAS}")
add_custom_command(OUTPUT ${TAPE} DEPENDS "${TAPE_BAS}" Aq roms
  COMMAND rm -f "${TAPE}"
  COMMAND ./Aq -nkBtb "${TAPE}" "${TAPE_BAS}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" VERBATIM)
add_custom_target(sndtest ALL DEPENDS "${TAPE}")
install(FILES "${TAPE}" DESTINATION "${TAPE_INSTALL_DIR}" RENAME sndtest.tape)

as2ihx(${TESTS})
foreach(f ${TESTS})
  add_custom_command(OUTPUT "${f}.bas"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${f}.ihx" "${IHX2BIN}"
    COMMAND ${CMAKE_COMMAND} "-DIHX2BIN=${IHX2BIN}"
      # CMAKE passes bin dir in CMAKE_CURRENT_SOURCE_DIR
      # so pass as SOURCE_DIR instead
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      -P ${CMAKE_SOURCE_DIR}/utils/asm2bas ${f})
endforeach()

#clean:
#	rm -f *.{ihx,lst,rel,sym}
