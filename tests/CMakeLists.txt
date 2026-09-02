add_executable(rawlab_smoke_test
  smoke_test.cpp
)

target_link_libraries(rawlab_smoke_test
  PRIVATE
    rawlab::core
)

rawlab_enable_warnings(rawlab_smoke_test)

add_test(NAME rawlab_smoke_test COMMAND rawlab_smoke_test)

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

add_test(NAME rawlab_app_smoke COMMAND $<TARGET_FILE:rawlab_app> --smoke-test)
set_tests_properties(rawlab_app_smoke PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(NAME rawlab_app_external_requires_output
  COMMAND $<TARGET_FILE:rawlab_app> --external-edit unused.raw)
set_tests_properties(rawlab_app_external_requires_output PROPERTIES
  WILL_FAIL TRUE
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(rawlab_app_state_test
  app_state_test.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ApplicationSettings.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/LibraryPage.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/LibraryPage.h
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ModelCatalog.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ModelDownloader.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/PictureLibraryStore.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/PictureRecipe.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/PreviewWidget.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/PreviewWidget.h
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/SettingsDialog.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/SettingsDialog.h
)
target_include_directories(rawlab_app_state_test PRIVATE ${CMAKE_SOURCE_DIR}/src/rawlab_app)
target_link_libraries(rawlab_app_state_test PRIVATE rawlab::core Qt6::Core Qt6::Widgets Qt6::Network)
set_target_properties(rawlab_app_state_test PROPERTIES AUTOMOC ON)
rawlab_enable_warnings(rawlab_app_state_test)
add_test(NAME rawlab_app_state_test COMMAND rawlab_app_state_test)
set_tests_properties(rawlab_app_state_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rawlab_model_downloader_test
  model_downloader_test.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ApplicationSettings.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ModelCatalog.cpp
  ${CMAKE_SOURCE_DIR}/src/rawlab_app/ModelDownloader.cpp
)
target_include_directories(rawlab_model_downloader_test PRIVATE ${CMAKE_SOURCE_DIR}/src/rawlab_app)
target_link_libraries(rawlab_model_downloader_test PRIVATE Qt6::Core Qt6::Network)
set_target_properties(rawlab_model_downloader_test PROPERTIES AUTOMOC ON)
rawlab_enable_warnings(rawlab_model_downloader_test)
add_test(NAME rawlab_model_downloader_test COMMAND rawlab_model_downloader_test)
