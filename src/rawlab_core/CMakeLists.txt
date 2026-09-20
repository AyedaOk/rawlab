add_library(rawlab_core STATIC
  src/ai/OnnxSession.cpp
  src/ai/RawForgeAdapter.cpp
  src/ai/Sam2Segmenter.cpp
  src/dng/TiffDngWriter.cpp
  src/image/PngImage.cpp
  src/image/AmazeDemosaic.cpp
  src/image/RawPreview.cpp
  src/image/LensCorrection.cpp
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
    ${ONNXRuntime_INCLUDE_DIR}
)

target_link_libraries(rawlab_core
  PUBLIC
    ${RAWLAB_LIBRAW_TARGET}
    ${RAWLAB_LIBTIFF_TARGET}
    ${RAWLAB_LIBPNG_TARGET}
    Qt6::Core
    Threads::Threads
)

rawlab_enable_warnings(rawlab_core)

if(RAWLAB_LENSFUN_TARGET)
  target_link_libraries(rawlab_core PUBLIC ${RAWLAB_LENSFUN_TARGET})
  target_compile_definitions(rawlab_core PUBLIC RAWLAB_HAS_LENSFUN=1)
endif()
