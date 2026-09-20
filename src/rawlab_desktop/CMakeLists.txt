add_library(rawlab_desktop_support STATIC
  ApplicationSettings.cpp
  ApplicationSettings.h
  ModelCatalog.cpp
  ModelCatalog.h
  ModelDownloader.cpp
  ModelDownloader.h
  PictureLibraryStore.cpp
  PictureLibraryStore.h
  PictureRecipe.cpp
  PictureRecipe.h
  BatchModelSnapshot.h
  BatchQueue.h
  BatchQueueStore.h
  ExternalBatch.h
)

add_library(rawlab::desktop_support ALIAS rawlab_desktop_support)
set_target_properties(rawlab_desktop_support PROPERTIES AUTOMOC ON)
target_include_directories(rawlab_desktop_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rawlab_desktop_support PUBLIC rawlab::core Qt6::Core Qt6::Network)
rawlab_enable_warnings(rawlab_desktop_support)
