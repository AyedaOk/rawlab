cmake_minimum_required(VERSION 3.25)

project(
  rawlab
  VERSION 0.1.0
  DESCRIPTION "Raw-domain photo preprocessing application"
  LANGUAGES CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(RAWLAB_BUILD_GUI "Build the RawLab desktop application" ON)
option(RAWLAB_BUILD_CLI "Build the RawLab command-line tools" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

add_subdirectory(src/rawlab_core)
if(RAWLAB_BUILD_CLI)
  add_subdirectory(src/rawlab_cli)
endif()
if(RAWLAB_BUILD_GUI)
  add_subdirectory(src/rawlab_app)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(RAWLAB_BUILD_GUI)
  install(TARGETS rawlab_app
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rawlab.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rawlab.png"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
  )
endif()
