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
