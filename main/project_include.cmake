# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
# Stamp the board into esp_app_desc_t.project_name, so the updater can refuse
# the other board's image (ota_same_board in main/ota_ver.h). IDF includes this
# file once sdkconfig exists and before any component CMakeLists.txt runs, and
# only esp_app_format reads PROJECT_NAME at that point. The build outputs keep
# the project() name, so serin_link.bin and serin_link-unsigned.bin do not move.
if(CONFIG_SERIN_BOARD_VIEWE_15)
    idf_build_set_property(PROJECT_NAME serin_link_viewe15)
elseif(CONFIG_SERIN_BOARD_VIEWE_21)
    idf_build_set_property(PROJECT_NAME serin_link_viewe21)
else()
    message(FATAL_ERROR "No SERIN_BOARD in sdkconfig: give the new board its own "
                        "OTA project name in main/project_include.cmake")
endif()
