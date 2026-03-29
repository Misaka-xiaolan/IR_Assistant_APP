cmake_minimum_required(VERSION 3.22)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)

file(GLOB_RECURSE User_BSP_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${User_BSP_Src}
)
