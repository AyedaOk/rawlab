cmake_minimum_required(VERSION 3.25)

project(
  rawlab
  VERSION 0.1.0
  DESCRIPTION "Raw-domain photo preprocessing application"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src/rawlab_core)
add_subdirectory(src/rawlab_cli)
add_subdirectory(src/rawlab_app)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
