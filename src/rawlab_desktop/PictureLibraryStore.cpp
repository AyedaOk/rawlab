#include "PictureLibraryStore.h"

#include <QFileInfo>
#include <QCryptographicHash>
#include <QSettings>

#include <algorithm>
#include <stdexcept>

namespace {

QString metadataKey(const QString &path)
{
  return QStringLiteral("library/metadata/") + QString::fromLatin1(
    QCryptographicHash::hash(PictureLibraryStore::normalizedPath(path).toUtf8(),
                             QCryptographicHash::Sha1).toHex());
}

}

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

PictureLibraryMetadata PictureLibraryStore::metadata(const QString &path) const
{
  const QString key = metadataKey(path);
  const QSettings settings;
  PictureLibraryMetadata result;
  result.rating = std::clamp(settings.value(key + "/rating", 0).toInt(), 0, 5);
  const QStringList allowed{"red", "yellow", "green", "blue", "purple"};
  for(const QString &label : settings.value(key + "/labels").toStringList())
    if(allowed.contains(label) && !result.labels.contains(label)) result.labels.push_back(label);
  return result;
}

void PictureLibraryStore::saveMetadata(const QString &path, const int rating,
                                       const QStringList &labels) const
{
  const QStringList allowed{"red", "yellow", "green", "blue", "purple"};
  QStringList clean;
  for(const QString &label : labels)
    if(allowed.contains(label) && !clean.contains(label)) clean.push_back(label);
  QSettings settings;
  const QString key = metadataKey(path);
  settings.setValue(key + "/rating", std::clamp(rating, 0, 5));
  settings.setValue(key + "/labels", clean);
  settings.sync();
  if(settings.status() != QSettings::NoError)
    throw std::runtime_error{"Cannot save library metadata"};
}

QString PictureLibraryStore::normalizedPath(const QString &path)
{
  const QFileInfo info{path};
  const QString canonical = info.canonicalFilePath();
  return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
