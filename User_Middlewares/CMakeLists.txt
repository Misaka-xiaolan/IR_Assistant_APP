cmake_minimum_required(VERSION 3.22)
add_subdirectory(easylogger)
add_subdirectory(RTT)


# --- LVGL 路径与功能开关 ---

# 1. 指定 lv_conf.h 的完整绝对路径
set(LV_BUILD_CONF_DIR "${CMAKE_SOURCE_DIR}/User_BSP/Inc" CACHE PATH "" FORCE)

# 4. 禁用 ThorVG (推荐 STM32F411 开启，节省空间)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

add_subdirectory(lvgl-9.5.0)
target_link_libraries(lvgl PUBLIC stm32cubemx)
