# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
# Compile the pinned driver with only its startup ordering fixed. Generated
# source belongs to this build; component-manager files are never modified.
if(CONFIG_SERIN_BOARD_VIEWE_15 AND CONFIG_SERIN_INPUT_POWER_SAVE)
    idf_component_get_property(serin_knob_lib espressif__knob COMPONENT_LIB)
    idf_component_get_property(serin_knob_dir espressif__knob COMPONENT_DIR)
    set(serin_knob_original "${serin_knob_dir}/iot_knob.c")
    set(serin_knob_generated "${CMAKE_BINARY_DIR}/serin_patches/knob/iot_knob.c")
    set(serin_knob_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/patch_knob_power_save.py")

    execute_process(
        COMMAND "${PYTHON}" "${serin_knob_generator}"
                "${serin_knob_original}" "${serin_knob_generated}"
        RESULT_VARIABLE serin_knob_patch_result
        ERROR_VARIABLE serin_knob_patch_error)
    if(NOT serin_knob_patch_result EQUAL 0)
        message(FATAL_ERROR "Knob power-save startup patch failed: ${serin_knob_patch_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${serin_knob_original}" "${serin_knob_generator}")

    get_target_property(serin_knob_sources "${serin_knob_lib}" SOURCES)
    list(FIND serin_knob_sources "${serin_knob_original}" serin_knob_source_index)
    if(serin_knob_source_index EQUAL -1)
        message(FATAL_ERROR "Pinned knob target does not contain ${serin_knob_original}")
    endif()
    list(REMOVE_AT serin_knob_sources ${serin_knob_source_index})
    list(INSERT serin_knob_sources ${serin_knob_source_index} "${serin_knob_generated}")
    set_property(TARGET "${serin_knob_lib}" PROPERTY SOURCES "${serin_knob_sources}")
    # Preserve quoted/private headers formerly found beside the original source.
    # The existing component target retains all includes, version definitions,
    # and dependencies supplied by IDF and cu_pkg_define_version().
    target_include_directories("${serin_knob_lib}" PRIVATE "${serin_knob_dir}")
endif()
