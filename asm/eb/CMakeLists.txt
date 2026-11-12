set(ROM "${CMAKE_BINARY_DIR}/roms/ebasic.bin")

# Copyright protection
# Note: If your ROM or z80dis version is different, this will fail!
find_local_file(ORIG_EBASIC_ROM ExtendedBasic.bin "Original 16KB Extended BASIC ROM"
                roms .)
find_program(XDELTA xdelta3 xdelta)
find_program(Z80DASM z80dasm)
if(XDELTA AND Z80DASM AND EXISTS "${ORIG_EBASIC_ROM}")
  execute_process(COMMAND ${XDELTA} -V RESULT_VARIABLE IS_XDELTA3
                  OUTPUT_QUIET ERROR_QUIET)
  if(IS_XDELTA3 EQUAL 0)
    set(IS_XDELTA3 1)
    set(patch ebasic.xdelta3)
  else()
    set(IS_XDELTA3 0)
    set(patch ebasic.xdelta)
  endif()
  add_custom_command(OUTPUT ebasic.as
    DEPENDS "${XOR}" "${ORIG_EBASIC_ROM}"
    # FIXME: use cmake script instead of shell script
    COMMAND
      env "XORCMD=${XOR}" "DASMCMD=${Z80DASM}" "XDELTACMD=${XDELTA}"
          "PATCH=${CMAKE_CURRENT_SOURCE_DIR}/${patch}" "XDELTA3=${IS_XDELTA3}"
      sh "${CMAKE_CURRENT_SOURCE_DIR}/mkebas" "${ORIG_EBASIC_ROM}" VERBATIM)

  add_custom_command(OUTPUT "${ROM}"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/ebasic.ihx" "${IHX2BIN}"
    COMMAND "${IHX2BIN}" -p 4096 ebasic.ihx "${ROM}" VERBATIM)
  as2ihx(ebasic)
  add_custom_target(ebasic ALL DEPENDS "${ROM}")
  install(FILES "${ROM}" DESTINATION "${ROM_INSTALL_DIR}")

  #clean:
  #	# Note: this will wipe any changes made to ebasic.as!
  #	rm -f *.{sym,lst,rel,ihx} eb.bin eb.bin.dec ebasic.{dis,as}

  #distclean: clean
  #	rm -f $(EBASIC_ROM)
else()
  set(msg "Relocated Extended BASIC ROM not built")
  if(NOT XDELTA)
    set(msg "${msg}\nxdelta not found")
  endif()
  if(NOT Z80DASM)
    set(msg "${msg}\nz80dasm not found")
  endif()
  if(NOT EXISTS "${ORIG_EBASIC_ROM}")
    set(msg "${msg}\noriginal ExtendedBasic.bin not found")
  endif()
  message(WARNING "${msg}")
endif()

