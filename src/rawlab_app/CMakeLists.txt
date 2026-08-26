qt_standard_project_setup()

qt_add_executable(rawlab_app
  ApplicationSettings.cpp
  ApplicationSettings.h
  main.cpp
  MainWindow.cpp
  MainWindow.h
  ModelCatalog.cpp
  ModelCatalog.h
  ModelDownloader.cpp
  ModelDownloader.h
  LayerItemDelegate.cpp
  LayerItemDelegate.h
  LibraryPage.cpp
  LibraryPage.h
  PreviewWidget.cpp
  PreviewWidget.h
  PictureLibraryStore.cpp
  PictureLibraryStore.h
  PictureRecipe.cpp
  PictureRecipe.h
  SettingsDialog.cpp
  SettingsDialog.h
)

set_target_properties(rawlab_app PROPERTIES
  OUTPUT_NAME rawlab-app
  MACOSX_BUNDLE TRUE
)

target_link_libraries(rawlab_app
  PRIVATE
    rawlab::core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
)

rawlab_enable_warnings(rawlab_app)
