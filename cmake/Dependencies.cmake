find_package(PkgConfig REQUIRED)
pkg_check_modules(LibRaw REQUIRED IMPORTED_TARGET libraw)
pkg_check_modules(LibTIFF REQUIRED IMPORTED_TARGET libtiff-4)
pkg_check_modules(LibPNG REQUIRED IMPORTED_TARGET libpng)

find_path(ONNXRuntime_INCLUDE_DIR
  NAMES onnxruntime_c_api.h
  PATH_SUFFIXES onnxruntime
)
find_library(ONNXRuntime_LIBRARY
  NAMES onnxruntime
)

if(NOT ONNXRuntime_INCLUDE_DIR OR NOT ONNXRuntime_LIBRARY)
  message(FATAL_ERROR "ONNX Runtime headers/library not found")
endif()

add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
  IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
)
