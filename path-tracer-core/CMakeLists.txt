# CMakeList.txt : CMake project for path-tracer, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(CREATE_LINK
  "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_SOURCE_DIR}/compile_commands.json"
  SYMBOLIC
)

set(CMAKE_CXX_STANDARD 23)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

message(STATUS "Building Path Tracer...")
project ("distributed-path-tracer")

find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
  message(STATUS "clang-format found: ${CLANG_FORMAT_EXE}")
  
  file(GLOB_RECURSE ALL_SOURCE_FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
  )
  
  add_custom_target(
    format
    COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${ALL_SOURCE_FILES}
    COMMENT "Running clang-format to format all source files"
    VERBATIM
  )
  
  set(HAS_FORMAT_TARGET TRUE)
else()
  message(WARNING "clang-format not found. Code formatting target will not be available.")
  set(HAS_FORMAT_TARGET FALSE)
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/stb")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/cgltf")
add_subdirectory("${CMAKE_SOURCE_DIR}/path_tracer_lib")


# Add source to this project's executable.
set(TARGET "PathTracer")
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

find_package(spdlog CONFIG REQUIRED)
find_package(AWSSDK REQUIRED COMPONENTS s3 servicediscovery)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-concurrentqueue CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

include_directories(${INCLUDE_DIR})
add_executable (${TARGET} ${SRC})

set_target_properties(${TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET} path_tracer_lib spdlog::spdlog_header_only nlohmann_json::nlohmann_json unofficial::concurrentqueue::concurrentqueue Boost::system ${AWSSDK_LINK_LIBRARIES})
target_precompile_headers(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp")

if(DEFINED HAS_FORMAT_TARGET AND HAS_FORMAT_TARGET)
  add_dependencies(${TARGET} format)
  message(STATUS "Format will run automatically when building the target")
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${TARGET} PROPERTY CXX_STANDARD 23)
endif()

# TODO: Add tests and install targets if needed.
