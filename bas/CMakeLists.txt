set(TAPE "${CMAKE_CURRENT_BINARY_DIR}/tape")
set(TAPE_BAS "${TAPE}.bas") # must be tape.bas (hard-coded in tape.cmake)

set(BAS
  bb.bas
  boxes.bas
  chars.bas
  chr-poke.bas
  clk.bas
  clk2.bas
  col-poke.bas
  colors.bas
  conccirc.bas
  hangman.bas
  kaleid.bas
  palette.bas
  pt.bas
  rest-ex.bas
  rndmul.bas
  snd-test.bas)
list(TRANSFORM BAS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
install(FILES ${BAS} DESTINATION "${BASIC_INSTALL_DIR}")

add_custom_command(OUTPUT "${TAPE_BAS}"
  DEPENDS ${BAS}
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/bas/tape.cmake -- ${BAS} VERBATIM)

add_custom_command(OUTPUT "${TAPE}" DEPENDS "${TAPE_BAS}" Aq roms
  COMMAND rm -f "${TAPE}"
  COMMAND ./Aq -nkBtb "${TAPE}" "${TAPE_BAS}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" VERBATIM)
add_custom_target(bas ALL DEPENDS ${TAPE})
install(FILES "${TAPE}" DESTINATION "${TAPE_INSTALL_DIR}" RENAME basic.tape)
