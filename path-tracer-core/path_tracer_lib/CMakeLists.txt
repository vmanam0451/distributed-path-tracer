cmake_minimum_required (VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)

set(TARGET_NAME "path_tracer_lib")

message(STATUS "Building Path Tracer Lib...")

file(GLOB_RECURSE TARGET_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/path_tracer")

add_library(${TARGET_NAME} ${TARGET_SOURCES})

target_include_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TARGET_NAME} stb cgltf)
target_precompile_headers(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/path_tracer/pch.hpp")