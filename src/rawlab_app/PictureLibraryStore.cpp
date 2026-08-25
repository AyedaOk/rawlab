#include "PictureLibraryStore.h"

#include <QFileInfo>
#include <QSettings>

QStringList PictureLibraryStore::files() const
{
  return QSettings{}.value(QStringLiteral("library/files")).toStringList();
}

QStringList PictureLibraryStore::addFiles(const QStringList &paths) const
{
  QStringList result = files();
  for(const QString &path : paths)
  {
    const QString normalized = normalizedPath(path);
    if(!normalized.isEmpty() && !result.contains(normalized)) result.push_back(normalized);
  }
  QSettings{}.setValue(QStringLiteral("library/files"), result);
  return result;
}

void PictureLibraryStore::removeFiles(const QStringList &paths) const
{
  QStringList result = files();
  for(const QString &path : paths) result.removeAll(normalizedPath(path));
  QSettings{}.setValue(QStringLiteral("library/files"), result);
}

QString PictureLibraryStore::normalizedPath(const QString &path)
{
  const QFileInfo info{path};
  const QString canonical = info.canonicalFilePath();
  return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
