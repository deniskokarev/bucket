cmake_minimum_required(VERSION 3.20.0)

# Board-specific overlay/conf are auto-picked from boards/<board>.{overlay,conf}
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

# Default target board. Override on the command line with:
#   west build -b <board> .
if(NOT DEFINED BOARD)
  set(BOARD "nucleo_wb55rg")
endif()

# Use OpenOCD as the default flash/debug runner (the board otherwise defaults to
# stm32cubeprogrammer). `west flash` / `west debug` now use OpenOCD with no -r.
if(NOT DEFINED BOARD_FLASH_RUNNER)
  set(BOARD_FLASH_RUNNER openocd)
endif()
if(NOT DEFINED BOARD_DEBUG_RUNNER)
  set(BOARD_DEBUG_RUNNER openocd)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bucket C)

target_sources(app PRIVATE
  src/main.c
  src/pump.c
  src/level.c
)
