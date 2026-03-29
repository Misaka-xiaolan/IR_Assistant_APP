cmake_minimum_required(VERSION 3.22)
enable_language(C ASM)

set(Segger_Include_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set(Segger_Src
        ${CMAKE_CURRENT_SOURCE_DIR}/SEGGER_RTT.c
        ${CMAKE_CURRENT_SOURCE_DIR}/SEGGER_RTT_printf.c
)

add_library(Segger OBJECT)
target_sources(Segger PRIVATE ${Segger_Src})
target_include_directories(Segger PUBLIC ${Segger_Include_DIR})
#target_link_libraries(Segger stm32cubemx)
#target_link_libraries(${CMAKE_PROJECT_NAME} Segger)
