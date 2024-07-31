cmake_minimum_required(VERSION 3.20)

project(cgltf VERSION 1.0.0 LANGUAGES CXX)

add_library(cgltf "${CMAKE_CURRENT_SOURCE_DIR}/cgltf.cpp")

target_include_directories(cgltf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
