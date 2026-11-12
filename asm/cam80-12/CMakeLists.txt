set(ROM "${CMAKE_BINARY_DIR}/roms/cam80-12.bin")
set(TAPE "${CMAKE_CURRENT_BINARY_DIR}/cam80-12.tape")

# FIXME: add deps on camel80[dh].azm (automatically)
ram_or_rom(cam80-12 ram -z camel80.azm)
ram_or_rom(cam80-12 rom -z camel80.azm)

add_custom_command(OUTPUT "${TAPE}"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cam80-12-ram.ihx" "${IHX2BIN}"
  COMMAND "${IHX2BIN}" -t cam80-12 cam80-12-ram.ihx "${TAPE}" VERBATIM)
install(FILES "${TAPE}" DESTINATION "${TAPE_INSTALL_DIR}")
add_custom_command(OUTPUT "${ROM}"
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/cam80-12-rom.ihx" "${IHX2BIN}"
  COMMAND "${IHX2BIN}" -p 8192 cam80-12-rom.ihx "${ROM}" VERBATIM)
add_custom_target(cam80-12 ALL DEPENDS "${TAPE}" "${ROM}")
install(FILES "${ROM}" DESTINATION "${ROM_INSTALL_DIR}")

#clean:
#	rm -f *.{ihx,lst,sym,rel}
