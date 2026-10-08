add_executable(rawlab_smoke_test
  smoke_test.cpp
)

target_link_libraries(rawlab_smoke_test
  PRIVATE
    rawlab::core
)

rawlab_enable_warnings(rawlab_smoke_test)

add_test(NAME rawlab_smoke_test COMMAND rawlab_smoke_test)

add_executable(rawlab_picture_recipe_test
  picture_recipe_test.cpp
)
target_link_libraries(rawlab_picture_recipe_test PRIVATE rawlab::desktop_support)
rawlab_enable_warnings(rawlab_picture_recipe_test)
add_test(NAME rawlab_picture_recipe_test COMMAND rawlab_picture_recipe_test)

add_executable(rawlab_picture_library_test
  picture_library_test.cpp
)
target_link_libraries(rawlab_picture_library_test PRIVATE rawlab::desktop_support)
rawlab_enable_warnings(rawlab_picture_library_test)
add_test(NAME rawlab_picture_library_test COMMAND rawlab_picture_library_test)

if(RAWLAB_BUILD_PREVIEW_BRIDGE)
  add_executable(rawlab_processing_session_test processing_session_test.cpp)
  target_include_directories(rawlab_processing_session_test PRIVATE ${CMAKE_SOURCE_DIR}/src/rawlab_preview)
  target_link_libraries(rawlab_processing_session_test PRIVATE rawlab::core rawlab::desktop_support Qt6::Gui)
  rawlab_enable_warnings(rawlab_processing_session_test)
  add_test(NAME rawlab_processing_session_test COMMAND rawlab_processing_session_test)
  add_executable(rawlab_preview_regions_test preview_regions_test.cpp)
  target_include_directories(rawlab_preview_regions_test PRIVATE ${CMAKE_SOURCE_DIR}/src/rawlab_preview)
  target_link_libraries(rawlab_preview_regions_test PRIVATE rawlab::core rawlab::desktop_support Qt6::Gui)
  rawlab_enable_warnings(rawlab_preview_regions_test)
  add_test(NAME rawlab_preview_regions_test COMMAND rawlab_preview_regions_test)
  add_executable(rawlab_preview_bridge_test preview_bridge_test.cpp)
  target_link_libraries(rawlab_preview_bridge_test PRIVATE rawlab::core Qt6::Gui)
  rawlab_enable_warnings(rawlab_preview_bridge_test)
  add_test(NAME rawlab_preview_bridge_test COMMAND rawlab_preview_bridge_test $<TARGET_FILE:rawlab_preview>)
endif()

add_executable(rawlab_model_downloader_test model_downloader_test.cpp)
target_link_libraries(rawlab_model_downloader_test PRIVATE rawlab::desktop_support)
rawlab_enable_warnings(rawlab_model_downloader_test)
add_test(NAME rawlab_model_downloader_test COMMAND rawlab_model_downloader_test)

add_executable(rawlab_exposure_metadata_test exposure_metadata_test.cpp)
target_link_libraries(rawlab_exposure_metadata_test PRIVATE rawlab::core)
rawlab_enable_warnings(rawlab_exposure_metadata_test)
add_test(NAME rawlab_exposure_metadata_test COMMAND rawlab_exposure_metadata_test)

if(RAWLAB_BUILD_CLI)
add_test(
  NAME rawlab_process_requires_denoise
  COMMAND $<TARGET_FILE:rawlab_cli> process unused.raw unused.dng --blend 1.0
)
set_tests_properties(rawlab_process_requires_denoise PROPERTIES
  WILL_FAIL TRUE
)

add_test(
  NAME rawlab_process_mask_requires_denoise
  COMMAND $<TARGET_FILE:rawlab_cli> process unused.raw unused.dng --mask unused.png
)
set_tests_properties(rawlab_process_mask_requires_denoise PROPERTIES
  WILL_FAIL TRUE
)
endif()
