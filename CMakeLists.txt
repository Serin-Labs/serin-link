cmake_minimum_required(VERSION 3.16)

# The optional latest-version probe can evaluate dependency Kconfig conditions
# before sdkconfig exists on a clean checkout (e.g. LV_USE_FREETYPE).
# Keep normal builds on dependencies.lock; upgrade with `idf.py update-dependencies`.
set(ENV{IDF_COMPONENT_CHECK_NEW_VERSION} "0")

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/secure_boot_signing_key.pem)
    message(WARNING "secure_boot_signing_key.pem missing — generating a fresh "
                    "OTA signing key (dials built from it accept only OTA "
                    "images signed with this key)")
    execute_process(COMMAND openssl genrsa -out ${CMAKE_CURRENT_SOURCE_DIR}/secure_boot_signing_key.pem 3072
                    RESULT_VARIABLE SERIN_SIGNGEN OUTPUT_QUIET ERROR_QUIET)
    if(NOT SERIN_SIGNGEN EQUAL 0)
        message(FATAL_ERROR "openssl genrsa failed — install openssl, or provide secure_boot_signing_key.pem yourself")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Names the output files for every board. The app descriptor's project_name
# is per board instead: see main/project_include.cmake.
project(serin_link)

include(cmake/knob_power_save.cmake)
