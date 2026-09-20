add_executable(rawlab_preview main.cpp)
set_target_properties(rawlab_preview PROPERTIES OUTPUT_NAME rawlab-preview)
target_link_libraries(rawlab_preview PRIVATE rawlab::core rawlab::desktop_support Qt6::Gui Qt6::Network)
rawlab_enable_warnings(rawlab_preview)
