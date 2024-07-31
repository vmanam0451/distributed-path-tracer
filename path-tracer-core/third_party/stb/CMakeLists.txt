cmake_minimum_required(VERSION 3.20)

project(stb VERSION 1.0.0 LANGUAGES CXX)

add_library(stb "${CMAKE_CURRENT_SOURCE_DIR}/stb.cpp")

target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
