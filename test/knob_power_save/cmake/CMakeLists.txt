# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
cmake_minimum_required(VERSION 3.16)
project(knob_patch_host C)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON "${Python3_EXECUTABLE}")
option(CONFIG_SERIN_BOARD_VIEWE_15 "Primary display" ON)
option(CONFIG_SERIN_INPUT_POWER_SAVE "Input power saving" ON)
get_filename_component(serin_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SERIN_UPSTREAM_DIR "${CMAKE_CURRENT_LIST_DIR}/../upstream" CACHE PATH "Pinned test component")

add_library(pinned_knob STATIC "${SERIN_UPSTREAM_DIR}/iot_knob.c")
target_include_directories(pinned_knob PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../stub" "${SERIN_UPSTREAM_DIR}/include")
target_compile_options(pinned_knob PRIVATE
    -Wall -Wextra -Werror -Wno-pointer-to-int-cast -Wno-unused-parameter)
target_compile_definitions(pinned_knob PRIVATE
    CONFIG_KNOB_PERIOD_TIME_MS=3 CONFIG_KNOB_DEBOUNCE_TICKS=2
    CONFIG_KNOB_HIGH_LIMIT=1000 CONFIG_KNOB_LOW_LIMIT=-1000)

# Match the public IDF component-property API while retaining a real CMake
# target. Configuration changes must select the right source on the next build.
function(idf_component_get_property output component property)
    if(NOT component STREQUAL "espressif__knob")
        message(FATAL_ERROR "Unexpected component ${component}")
    endif()
    if(property STREQUAL "COMPONENT_LIB")
        set(${output} pinned_knob PARENT_SCOPE)
    elseif(property STREQUAL "COMPONENT_DIR")
        set(${output} "${SERIN_UPSTREAM_DIR}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unexpected component property ${property}")
    endif()
endfunction()

include("${serin_repo}/cmake/knob_power_save.cmake")
get_target_property(serin_sources pinned_knob SOURCES)
if(CONFIG_SERIN_BOARD_VIEWE_15 AND CONFIG_SERIN_INPUT_POWER_SAVE)
    set(serin_expected "${CMAKE_BINARY_DIR}/serin_patches/knob/iot_knob.c")
else()
    set(serin_expected "${SERIN_UPSTREAM_DIR}/iot_knob.c")
endif()
if(NOT serin_sources STREQUAL serin_expected)
    message(FATAL_ERROR "Wrong source for configuration: ${serin_sources}")
endif()
