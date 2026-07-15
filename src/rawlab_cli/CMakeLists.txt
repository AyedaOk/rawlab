add_executable(rawlab_cli
  main.cpp
)

target_link_libraries(rawlab_cli
  PRIVATE
    rawlab::core
)

set_target_properties(rawlab_cli PROPERTIES OUTPUT_NAME rawlab)

rawlab_enable_warnings(rawlab_cli)
