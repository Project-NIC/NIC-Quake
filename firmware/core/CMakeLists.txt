cmake_minimum_required(VERSION 3.16)
project(nic-core C)

#
# NIC core — the universal, front-agnostic platform.
#
# Every nic-* library is portable, hardware-agnostic C: it talks to the world
# through callbacks (SPI/UART/flash/clock) supplied by a glue layer. The whole
# tree compiles and the tests run on a plain host PC with gcc/clang — no MCU
# toolchain needed. The product fronts (quake/, weather/, iono/, ...) build on
# top of this core and never fork it: a front lives in glue, not in here.
#
# Standalone host build + tests:
#   cmake -S core -B core/build
#   cmake --build core/build
#   ctest --test-dir core/build --output-on-failure
#

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# --- Shared universal types / proto / spi / crc / math (header-only) -----
add_library(nic-common INTERFACE)
target_include_directories(nic-common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/nic-common/include)

# --- Universal frame bridge (portable transport) ------------------------
add_library(nic-link STATIC lib/nic-link/src/nic_link.c)
target_include_directories(nic-link PUBLIC lib/nic-link/include)
target_link_libraries(nic-link PUBLIC nic-common)

# --- Node lifecycle state machine + data packer (portable core) ----------
add_library(nic-node STATIC lib/nic-node/src/nic_node.c)
target_include_directories(nic-node PUBLIC lib/nic-node/include)
target_link_libraries(nic-node PUBLIC nic-common)

# --- Master-side orchestration (portable; ESP32 glue is separate) --------
add_library(nic-master STATIC lib/nic-master/src/nic_master.c)
target_include_directories(nic-master PUBLIC lib/nic-master/include)
target_link_libraries(nic-master PUBLIC nic-common nic-link)

# --- LoRa telemetry frame (portable) ------------------------------------
add_library(nic-lora STATIC lib/nic-lora/src/nic_lora.c)
target_include_directories(nic-lora PUBLIC lib/nic-lora/include)
target_link_libraries(nic-lora PUBLIC nic-common)

# --- Time sync: PPS-anchored absolute time (portable) --------------------
add_library(nic-timesync STATIC lib/nic-timesync/src/nic_timesync.c)
target_include_directories(nic-timesync PUBLIC lib/nic-timesync/include)
target_link_libraries(nic-timesync PUBLIC nic-common)

# --- Clock switch sequencing (portable; STM32 register pokes in the glue) -
add_library(nic-clocksync STATIC lib/nic-clocksync/src/nic_clocksync.c)
target_include_directories(nic-clocksync PUBLIC lib/nic-clocksync/include)
target_link_libraries(nic-clocksync PUBLIC nic-common)

# --- Head-end record spool: batch on N-or-T, store-and-forward (portable) -
add_library(nic-uplink STATIC lib/nic-uplink/src/nic_uplink.c)
target_include_directories(nic-uplink PUBLIC lib/nic-uplink/include)
target_link_libraries(nic-uplink PUBLIC nic-common)

# --- Core tests (host only) ---------------------------------------------
enable_testing()

add_executable(test_link test/test_link.c)
target_link_libraries(test_link PRIVATE nic-link)
add_test(NAME test_link COMMAND test_link)

add_executable(test_node test/test_node.c)
target_link_libraries(test_node PRIVATE nic-node)
add_test(NAME test_node COMMAND test_node)

add_executable(test_master test/test_master.c)
target_link_libraries(test_master PRIVATE nic-master)
add_test(NAME test_master COMMAND test_master)

add_executable(test_lora test/test_lora.c)
target_link_libraries(test_lora PRIVATE nic-lora)
add_test(NAME test_lora COMMAND test_lora)

add_executable(test_timesync test/test_timesync.c)
target_link_libraries(test_timesync PRIVATE nic-timesync)
add_test(NAME test_timesync COMMAND test_timesync)

add_executable(test_clocksync test/test_clocksync.c)
target_link_libraries(test_clocksync PRIVATE nic-clocksync)
add_test(NAME test_clocksync COMMAND test_clocksync)

add_executable(test_uplink test/test_uplink.c)
target_link_libraries(test_uplink PRIVATE nic-uplink)
add_test(NAME test_uplink COMMAND test_uplink)
