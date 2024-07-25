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

set(CMAKE_CXX_STANDARD 20)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

message(STATUS "Building Path Tracer...")
project ("distributed-path-tracer")

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/stb")
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/cgltf")
add_subdirectory("${CMAKE_SOURCE_DIR}/path_tracer")


# Add source to this project's executable.
set(TARGET_NAME "PathTracer")
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

find_package(spdlog CONFIG REQUIRED)
find_package(aws-lambda-runtime REQUIRED)

include_directories(${INCLUDE_DIR})
add_executable (${TARGET_NAME} ${SRC})

set_target_properties(${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME} path_tracer_lib spdlog::spdlog_header_only AWS::aws-lambda-runtime)
aws_lambda_package_target(${TARGET_NAME})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
