#pragma once

#include "ModelCatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkReply;

class ModelDownloader final : public QObject
{
  Q_OBJECT

public:
  explicit ModelDownloader(QObject *parent = nullptr);
  ModelDownloader(QString modelRoot, QUrl repositoryUrl, QObject *parent = nullptr);
  ~ModelDownloader() override;

  QString modelRoot() const;
  bool isRunning() const;
  void start(const QStringList &selectedIds = {});
  void refreshCatalog();
  bool loadCachedCatalog();
  void cancel();

  static bool isInstalled(const QString &modelRoot, const ModelCatalogEntry &entry);

signals:
  void catalogModelFound(QString id, QString name, QString version, QString task,
                         QString adapter, bool installed);
  void modelInstalled(QString id, QString name, QString version, QString task,
                      QString adapter, QString location);
  void statusChanged(QString status);
  void progressChanged(int percent);
  void finished();
  void failed(QString message);

private:
  void fetchCatalog();
  bool processCatalog(const QByteArray &data, QString *error);
  void fetchNextManifest();
  void fetchNextFile();
  void fetchBytes(const QUrl &url, const std::function<void(QByteArray)> &done);
  void beginFileDownload(const QUrl &url, const QString &path, const ModelFileEntry &file);
  void publishCurrentModel();
  void fail(const QString &message);
  void clearCurrentDownload();
  QUrl fileUrl(const QString &relativePath) const;

  QNetworkAccessManager network_;
  QNetworkReply *reply_ = nullptr;
  QString modelRoot_;
  QUrl repositoryUrl_;
  QList<ModelCatalogEntry> models_;
  QStringList selectedIds_;
  int modelIndex_ = 0;
  ModelManifest manifest_;
  QByteArray manifestBytes_;
  int fileIndex_ = 0;
  QString stagingPath_;
  QFile output_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  qint64 bytesWritten_ = 0;
  bool cancelled_ = false;
  bool catalogOnly_ = false;
};
