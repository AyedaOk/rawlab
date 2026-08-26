find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Network)
find_package(Threads REQUIRED)

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(LibRaw QUIET IMPORTED_TARGET libraw)
  pkg_check_modules(LibTIFF QUIET IMPORTED_TARGET libtiff-4)
  pkg_check_modules(LibPNG QUIET IMPORTED_TARGET libpng)
endif()

set(RAWLAB_DEPENDENCY_ROOT "" CACHE PATH "Root directory for packaged dependencies")

if(NOT TARGET PkgConfig::LibRaw)
  find_path(RAWLAB_LIBRAW_INCLUDE_DIR NAMES libraw/libraw.h
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/include")
  find_library(RAWLAB_LIBRAW_LIBRARY NAMES raw libraw
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/lib" "${RAWLAB_DEPENDENCY_ROOT}/lib64")
  if(NOT RAWLAB_LIBRAW_INCLUDE_DIR OR NOT RAWLAB_LIBRAW_LIBRARY)
    message(FATAL_ERROR "LibRaw headers/library not found")
  endif()
  add_library(RawLab::LibRaw UNKNOWN IMPORTED)
  set_target_properties(RawLab::LibRaw PROPERTIES
    IMPORTED_LOCATION "${RAWLAB_LIBRAW_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWLAB_LIBRAW_INCLUDE_DIR}")
  set(RAWLAB_LIBRAW_TARGET RawLab::LibRaw)
else()
  set(RAWLAB_LIBRAW_TARGET PkgConfig::LibRaw)
endif()

if(NOT TARGET PkgConfig::LibTIFF)
  find_path(RAWLAB_LIBTIFF_INCLUDE_DIR NAMES tiff.h
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/include")
  find_library(RAWLAB_LIBTIFF_LIBRARY NAMES tiff tiff-6
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/lib" "${RAWLAB_DEPENDENCY_ROOT}/lib64")
  if(NOT RAWLAB_LIBTIFF_INCLUDE_DIR OR NOT RAWLAB_LIBTIFF_LIBRARY)
    message(FATAL_ERROR "libtiff headers/library not found")
  endif()
  add_library(RawLab::LibTIFF UNKNOWN IMPORTED)
  set_target_properties(RawLab::LibTIFF PROPERTIES
    IMPORTED_LOCATION "${RAWLAB_LIBTIFF_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWLAB_LIBTIFF_INCLUDE_DIR}")
  set(RAWLAB_LIBTIFF_TARGET RawLab::LibTIFF)
else()
  set(RAWLAB_LIBTIFF_TARGET PkgConfig::LibTIFF)
endif()

if(NOT TARGET PkgConfig::LibPNG)
  find_path(RAWLAB_LIBPNG_INCLUDE_DIR NAMES png.h
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/include")
  find_library(RAWLAB_LIBPNG_LIBRARY NAMES png png16
    PATHS "${RAWLAB_DEPENDENCY_ROOT}/lib" "${RAWLAB_DEPENDENCY_ROOT}/lib64")
  if(NOT RAWLAB_LIBPNG_INCLUDE_DIR OR NOT RAWLAB_LIBPNG_LIBRARY)
    message(FATAL_ERROR "libpng headers/library not found")
  endif()
  add_library(RawLab::LibPNG UNKNOWN IMPORTED)
  set_target_properties(RawLab::LibPNG PROPERTIES
    IMPORTED_LOCATION "${RAWLAB_LIBPNG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RAWLAB_LIBPNG_INCLUDE_DIR}")
  set(RAWLAB_LIBPNG_TARGET RawLab::LibPNG)
else()
  set(RAWLAB_LIBPNG_TARGET PkgConfig::LibPNG)
endif()

find_path(ONNXRuntime_INCLUDE_DIR
  NAMES onnxruntime_c_api.h
  PATHS "${RAWLAB_DEPENDENCY_ROOT}/include"
  PATH_SUFFIXES onnxruntime onnxruntime/core/session
)
find_library(ONNXRuntime_LIBRARY
  NAMES onnxruntime
  PATHS "${RAWLAB_DEPENDENCY_ROOT}/lib" "${RAWLAB_DEPENDENCY_ROOT}/lib64"
)

if(NOT ONNXRuntime_INCLUDE_DIR OR NOT ONNXRuntime_LIBRARY)
  message(FATAL_ERROR "ONNX Runtime headers/library not found")
endif()

add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
  IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
)
