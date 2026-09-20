cmake_minimum_required(VERSION 3.25)

project(
  rawlab
  VERSION 0.1.1
  DESCRIPTION "Raw-domain photo preprocessing application"
  LANGUAGES CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(RAWLAB_BUILD_CLI "Build the RawLab command-line tools" ON)
option(RAWLAB_BUILD_PREVIEW_BRIDGE "Build read-only RAW viewing bridge for the React desktop" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src/rawlab_core)
add_subdirectory(src/rawlab_desktop)
if(RAWLAB_BUILD_PREVIEW_BRIDGE)
  add_subdirectory(src/rawlab_preview)
endif()
if(RAWLAB_BUILD_CLI)
  add_subdirectory(src/rawlab_cli)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(RAWLAB_BUILD_PREVIEW_BRIDGE)
  install(TARGETS rawlab_preview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/darktable/RawLab.lua"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rawlab/darktable"
  )
endif()
