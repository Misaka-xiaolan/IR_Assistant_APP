cmake_minimum_required(VERSION 3.22)
enable_language(C ASM)

set(Easylogger_Include_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

set(Easylogger_Src
        ${CMAKE_CURRENT_SOURCE_DIR}/port/elog_port.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/elog.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/elog_async.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/elog_buf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/elog_utils.c
)

add_library(Easylogger OBJECT)
target_sources(Easylogger PRIVATE ${Easylogger_Src})
target_include_directories(Easylogger PUBLIC ${Easylogger_Include_DIR})
target_link_libraries(Easylogger stm32cubemx)
target_link_libraries(Easylogger Segger)
target_link_libraries(${CMAKE_PROJECT_NAME} Easylogger)

