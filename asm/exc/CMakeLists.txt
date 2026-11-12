set(ROM "${CMAKE_BINARY_DIR}/roms/zexall.bin")
set(TAPE "${CMAKE_CURRENT_BINARY_DIR}/zexall.tape")

ram_or_rom(zexall ram zexall.z80 crctab.mac)
ram_or_rom(zexall rom zexall.z80 crctab.mac)

add_custom_command(OUTPUT "${TAPE}"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/zexall-ram.ihx" "${IHX2BIN}"
  COMMAND "${IHX2BIN}" -t zexall zexall-ram.ihx "${TAPE}" VERBATIM)
install(FILES "${TAPE}" DESTINATION "${TAPE_INSTALL_DIR}")
add_custom_command(OUTPUT "${ROM}"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/zexall-rom.ihx" "${IHX2BIN}"
  COMMAND "${IHX2BIN}" -p 8192 zexall-rom.ihx "${ROM}" VERBATIM)
add_custom_target(zexall ALL DEPENDS "${TAPE}" "${ROM}")
install(FILES "${ROM}" DESTINATION "${ROM_INSTALL_DIR}")

#clean:
#	rm -f *.{ihx,lst,sym,rel}
