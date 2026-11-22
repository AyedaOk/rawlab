#include "PictureLibraryStore.h"
#include "ApplicationSettings.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>

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

  // A file in place of the settings directory fails even when tests run as root.
  const QString blocked = directory.filePath("blocked");
  QFile blocker{blocked};
  if(!blocker.open(QIODevice::WriteOnly)) return 1;
  blocker.close();
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, blocked);
  const auto fails = [](const auto &operation) {
    try { operation(); return false; }
    catch(const std::runtime_error &) { return true; }
  };
  if(!fails([&] { store.addFiles({path}); })
     || !fails([&] { store.removeFiles({path}); })
     || !fails([&] { ApplicationSettingsStore{}.save({}); })) return 1;

  std::cout << "picture library store ok\n";
  return 0;
}
