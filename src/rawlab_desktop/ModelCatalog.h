#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

struct ModelCatalogEntry
{
  QString id;
  QString name;
  QString version;
  QString task;
  QString adapter;
  QString manifestPath;
  qint64 manifestSize = 0;
  QByteArray manifestSha256;
  bool available = false;
};

struct ModelFileEntry
{
  QString name;
  qint64 size = 0;
  QByteArray sha256;
};

struct ModelManifest
{
  QString id;
  QString name;
  QString version;
  QString task;
  QString adapter;
  QList<ModelFileEntry> files;
};

class ModelCatalog
{
public:
  static QList<ModelCatalogEntry> parseCatalog(const QByteArray &data, QString *error = nullptr);
  static ModelManifest parseManifest(const QByteArray &data, QString *error = nullptr);
  static bool verify(const QByteArray &data, qint64 expectedSize, const QByteArray &expectedSha256);
  static bool isSafeRelativePath(const QString &path);
};
