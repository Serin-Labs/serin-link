cmake_minimum_required(VERSION 3.16)

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
project(serin_dial)
