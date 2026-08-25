#pragma once

#include <QStringList>

class PictureLibraryStore
{
public:
  QStringList files() const;
  QStringList addFiles(const QStringList &paths) const;
  void removeFiles(const QStringList &paths) const;

  static QString normalizedPath(const QString &path);
};
