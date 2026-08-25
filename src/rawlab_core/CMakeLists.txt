add_library(rawlab_core STATIC
  src/ai/OnnxSession.cpp
  src/ai/RawForgeAdapter.cpp
  src/ai/Sam2Segmenter.cpp
  src/dng/TiffDngWriter.cpp
  src/image/PngImage.cpp
  src/image/RawPreview.cpp
  src/pipeline/Mask.cpp
  src/pipeline/LayerComposition.cpp
  src/pipeline/MaskedBlend.cpp
  src/pipeline/RawProcessingPipeline.cpp
  src/raw/BayerProcessing.cpp
  src/raw/LibRawDecoder.cpp
  src/raw/RawImage.cpp
  src/raw/RawInspectInfo.cpp
  src/Version.cpp
)

add_library(rawlab::core ALIAS rawlab_core)

target_include_directories(rawlab_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rawlab_core
  PUBLIC
    PkgConfig::LibRaw
    PkgConfig::LibTIFF
    PkgConfig::LibPNG
    ONNXRuntime::ONNXRuntime
    Threads::Threads
)

rawlab_enable_warnings(rawlab_core)
