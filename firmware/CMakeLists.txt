cmake_minimum_required(VERSION 3.16)
project(nic-quake C)

#
# NIC-Quake — the complete seismograph: the universal core plus the seismo front.
#
# core/      The universal, front-agnostic NIC platform (nic-* libraries). This
#            is the trunk every NIC node reuses unchanged; NIC-Station vendors it
#            from here for its weather/iono/stardust fronts.
# lib/       The seismo front (nq-*): the sensor drivers (nq-icm42688, nq-adxl355,
#            nq-scl3300), the sensor manager (nq-sensors), axis calibration
#            (nq-cal) and the event trigger (nq-detect). They stay nq-* on
#            purpose — front-specific, not universal. Everything generic they
#            need (NIC_OK, nic_sample3_t, nic_spi_t, math/crc) comes from core/.
#
# Host build + tests (builds the core and the seismo front together):
#   cmake -S firmware -B firmware/build
#   cmake --build firmware/build
#   ctest --test-dir firmware/build --output-on-failure
#

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# The core platform supplies nic-common (and the nic-* libraries the glue uses).
# It lives here in core/ — the trunk this repo owns.
if(NOT TARGET nic-common)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/core ${CMAKE_BINARY_DIR}/core)
endif()

# --- Sensor drivers (portable, seismo front) ----------------------------
add_library(nq-icm42688 STATIC lib/nq-icm42688/src/nq_icm42688.c)
target_include_directories(nq-icm42688 PUBLIC lib/nq-icm42688/include)
target_link_libraries(nq-icm42688 PUBLIC nic-common)

add_library(nq-adxl355 STATIC lib/nq-adxl355/src/nq_adxl355.c)
target_include_directories(nq-adxl355 PUBLIC lib/nq-adxl355/include)
target_link_libraries(nq-adxl355 PUBLIC nic-common)

add_library(nq-scl3300 STATIC lib/nq-scl3300/src/nq_scl3300.c)
target_include_directories(nq-scl3300 PUBLIC lib/nq-scl3300/include)
target_link_libraries(nq-scl3300 PUBLIC nic-common)

# --- Sensor manager / auto-detection ------------------------------------
add_library(nq-sensors STATIC lib/nq-sensors/src/nq_sensors.c)
target_include_directories(nq-sensors PUBLIC lib/nq-sensors/include)
target_link_libraries(nq-sensors PUBLIC nq-icm42688 nq-adxl355 nq-scl3300 nic-common)

# --- Axis calibration (portable math) -----------------------------------
add_library(nq-cal STATIC lib/nq-cal/src/nq_cal.c)
target_include_directories(nq-cal PUBLIC lib/nq-cal/include)
target_link_libraries(nq-cal PUBLIC nic-common)

# --- Seismic event trigger (portable STA/LTA) ---------------------------
add_library(nq-detect STATIC lib/nq-detect/src/nq_detect.c)
target_include_directories(nq-detect PUBLIC lib/nq-detect/include)
target_link_libraries(nq-detect PUBLIC nic-common)

# Remaining hardware-bound pieces live in node/ and master/ (board HAL +
# main_app) — they need the STM32 / ESP-IDF toolchains and real hardware, so
# they are syntax-checked, not built here.

# --- Seismo tests (host only) -------------------------------------------
enable_testing()

add_executable(test_sensors test/test_sensors.c)
target_link_libraries(test_sensors PRIVATE nq-sensors)
add_test(NAME test_sensors COMMAND test_sensors)

add_executable(test_cal test/test_cal.c)
target_link_libraries(test_cal PRIVATE nq-cal m)
add_test(NAME test_cal COMMAND test_cal)

add_executable(test_detect test/test_detect.c)
target_link_libraries(test_detect PRIVATE nq-detect)
add_test(NAME test_detect COMMAND test_detect)

add_executable(test_drivers test/test_drivers.c)
target_link_libraries(test_drivers PRIVATE nq-icm42688 nq-adxl355)
add_test(NAME test_drivers COMMAND test_drivers)

add_executable(test_scl3300 test/test_scl3300.c)
target_link_libraries(test_scl3300 PRIVATE nq-scl3300)
add_test(NAME test_scl3300 COMMAND test_scl3300)
