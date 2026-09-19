file(GLOB_RECURSE FONT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c)

set(BSP_SRCS "bsp_touch.c")
if(CONFIG_SERIN_BOARD_VIEWE_15)
    list(APPEND BSP_SRCS "bsp_viewe.c")
    set(BSP_REQUIRES esp_lcd_sh8601 esp_lcd_touch_cst820 knob button)
else()
    list(APPEND BSP_SRCS "bsp_viewe21.c")
    set(BSP_REQUIRES esp_lcd_touch_cst816s)
endif()

idf_component_register(
    SRCS "dial_main.c" ${BSP_SRCS} "ui.c" "ui_widgets.c" "ui_rim.c" "ui_arim.c" "ui_wifi.c" "ui_about.c" "ui_settings.c" "child_lock.c" "ui_advanced.c" "ui_zones.c" "ui_room_source.c" "ui_fan.c" "ui_menu.c" "ui_dialog.c" "ui_coach.c" "ui_gesture.c" "icons_serin.c" "espnow_bond.c" "espnow_client.c" "link_crypto.c" "link_ota.c" "perf_stats.c" "prefs.c" "wdt_crumb.c" "factory_reset.c" "sensor_sht40.c" "thermal_cal.c" "wifi_sta.c" "lv_mem_psram.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
    REQUIRES esp_lcd ${BSP_REQUIRES} esp_lvgl_port lvgl driver nvs_flash esp_wifi esp_netif console esp_timer mbedtls esp_https_ota app_update esp_http_client json lwip efuse
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
idf_component_get_property(lvgl_dir lvgl__lvgl COMPONENT_DIR)
set_source_files_properties(
    ${lvgl_dir}/src/draw/sw/blend/lv_draw_sw_blend.c
    ${lvgl_dir}/src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c
    ${lvgl_dir}/src/draw/sw/blend/lv_draw_sw_blend_to_rgb565_swapped.c
    TARGET_DIRECTORY ${lvgl_lib}
    PROPERTIES COMPILE_DEFINITIONS "LV_ATTRIBUTE_FAST_MEM=IRAM_ATTR")

target_compile_definitions(${lvgl_lib} PRIVATE
    "LV_ASSERT_HANDLER=abort()\;"
    "LV_ASSERT_HANDLER_INCLUDE=<stdlib.h>")

target_compile_definitions(${COMPONENT_TARGET} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)

if(CONFIG_LV_USE_CUSTOM_MALLOC)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init")
endif()
