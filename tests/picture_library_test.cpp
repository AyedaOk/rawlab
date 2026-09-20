#include "PictureLibraryStore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

int main(int argc, char **argv)
{
  QCoreApplication app{argc, argv};
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;

  QCoreApplication::setOrganizationName(QStringLiteral("RawLabTest"));
  QCoreApplication::setApplicationName(QStringLiteral("RawLabTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

  PictureLibraryStore store;
  const QString path = QStringLiteral("/photos/camera.ARW");

  store.addFiles({path});
  if(!store.files().contains(PictureLibraryStore::normalizedPath(path))) return 1;

  store.saveMetadata(path, 4, {QStringLiteral("red")});
  {
    const auto metadata = store.metadata(path);
    if(metadata.rating != 4
       || metadata.labels != QStringList{QStringLiteral("red")}) return 1;
  }

  store.removeMetadata(path);
  {
    const auto metadata = store.metadata(path);
    if(metadata.rating != 0 || !metadata.labels.isEmpty()) return 1;
  }

  store.removeFiles({path});
  if(store.files().contains(PictureLibraryStore::normalizedPath(path))) return 1;

  std::cout << "picture library store ok\n";
  return 0;
}
