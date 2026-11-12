set(OS_ROMS Aquarius-chars.rom radofin.bin)

set(GAME_ROMS
    AD&D.BIN astrosmash.bin biorhythms.bin BurgerTime.bin chess.bin
    MelodyChase.bin nightstalker.bin)

set(OTHER_ROMS
    ExtendedBasic.bin fileform.bin finform.bin logo.bin MarqueeDemo.bin
    QuickDisk.bin S_Burgertime.rom S_ExtendedBasic.rom Shark!.bin
    S_ModemTerminalSoftware.rom snafu.bin SpaceSpeller.bin
    S_Word_processor.rom tron.bin utopia.bin ZEROIN.BIN ZEROINm.bin)

# generated elsewhere (camel80?)
#ebasic.bin
#cam80-12.bin camel80.bin zexall.bin

# for convenience, link roms into current dir if out-of-tree
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  set(ROM_FILES ${OS_ROMS} ${GAME_ROMS} ${OTHER_ROMS})
  list(TRANSFORM ROM_FILES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
  add_custom_target(roms ALL
      COMMAND ln -sf ${ROM_FILES} ${CMAKE_CURRENT_BINARY_DIR}
      VERBATIM)
else()
  add_custom_target(roms)
endif()

install(FILES ${OS_ROMS} ${GAME_ROMS} ${OTHER_ROMS}
        DESTINATION "${ROM_INSTALL_DIR}")
