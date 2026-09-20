#pragma once

#include <QStringList>

struct PictureLibraryMetadata
{
  int rating = 0;
  QStringList labels;
};

class PictureLibraryStore
{
public:
  QStringList files() const;
  QStringList addFiles(const QStringList &paths) const;
  void removeFiles(const QStringList &paths) const;
  PictureLibraryMetadata metadata(const QString &path) const;
  void saveMetadata(const QString &path, int rating, const QStringList &labels) const;

  static QString normalizedPath(const QString &path);
};
