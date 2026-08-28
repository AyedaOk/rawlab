#include "ModelDownloader.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace {

constexpr auto repository = "https://huggingface.co/AyedaOk/rawlab-models/resolve/main/";

bool verifyFile(const QString &path, const ModelFileEntry &expected)
{
  QFile file{path};
  if(!file.open(QIODevice::ReadOnly) || file.size() != expected.size) return false;
  QCryptographicHash hash{QCryptographicHash::Sha256};
  if(!hash.addData(&file)) return false;
  return hash.result().toHex() == expected.sha256;
}

} // namespace

ModelDownloader::ModelDownloader(QObject *parent)
  : ModelDownloader{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/models"),
                    QUrl{QString::fromLatin1(repository)}, parent}
{
}

ModelDownloader::ModelDownloader(QString modelRoot, QUrl repositoryUrl, QObject *parent)
  : QObject{parent}, modelRoot_{std::move(modelRoot)}, repositoryUrl_{std::move(repositoryUrl)}
{
}

ModelDownloader::~ModelDownloader()
{
  if(reply_)
  {
    disconnect(reply_, nullptr, this, nullptr);
    reply_->abort();
  }
  clearCurrentDownload();
}

QString ModelDownloader::modelRoot() const { return modelRoot_; }

bool ModelDownloader::isRunning() const { return reply_ || !stagingPath_.isEmpty(); }

void ModelDownloader::start(const QStringList &selectedIds)
{
  if(isRunning()) return;
  cancelled_ = false;
  catalogOnly_ = false;
  selectedIds_ = selectedIds;
  models_.clear();
  modelIndex_ = 0;
  emit progressChanged(0);
  fetchCatalog();
}

void ModelDownloader::refreshCatalog()
{
  if(isRunning()) return;
  cancelled_ = false;
  catalogOnly_ = true;
  selectedIds_.clear();
  models_.clear();
  modelIndex_ = 0;
  emit progressChanged(0);
  fetchCatalog();
}

bool ModelDownloader::loadCachedCatalog()
{
  QFile file{QDir{modelRoot_}.filePath(QStringLiteral("catalog.json"))};
  if(!file.open(QIODevice::ReadOnly)) return false;
  QString error;
  if(!processCatalog(file.readAll(), &error)) return false;
  return true;
}

void ModelDownloader::cancel()
{
  if(!isRunning()) return;
  cancelled_ = true;
  if(reply_)
  {
    reply_->abort();
    return;
  }
  clearCurrentDownload();
  emit statusChanged(tr("Download cancelled"));
  emit finished();
}

bool ModelDownloader::isInstalled(const QString &modelRoot, const ModelCatalogEntry &entry)
{
  const QString directory = QDir{modelRoot}.filePath(entry.id + QLatin1Char('/') + entry.version);
  QFile manifestFile{QDir{directory}.filePath(QStringLiteral("model.json"))};
  if(!manifestFile.open(QIODevice::ReadOnly)) return false;
  const QByteArray bytes = manifestFile.readAll();
  if(!ModelCatalog::verify(bytes, entry.manifestSize, entry.manifestSha256)) return false;
  QString error;
  const ModelManifest manifest = ModelCatalog::parseManifest(bytes, &error);
  if(!error.isEmpty() || manifest.id != entry.id || manifest.version != entry.version
     || manifest.task != entry.task || manifest.adapter != entry.adapter) return false;
  for(const ModelFileEntry &file : manifest.files)
  {
    if(!verifyFile(QDir{directory}.filePath(file.name), file)) return false;
  }
  return true;
}

void ModelDownloader::fetchCatalog()
{
  emit statusChanged(tr("Downloading model catalog..."));
  fetchBytes(fileUrl(QStringLiteral("catalog.json")), [this](const QByteArray bytes) {
    QString error;
    if(!processCatalog(bytes, &error)) return fail(error);
    if(catalogOnly_)
    {
      emit progressChanged(100);
      emit statusChanged(tr("Model catalog refreshed"));
      emit finished();
      return;
    }
    fetchNextManifest();
  });
}

bool ModelDownloader::processCatalog(const QByteArray &data, QString *error)
{
  models_ = ModelCatalog::parseCatalog(data, error);
  if(error && !error->isEmpty()) return false;
  QDir root;
  if(!root.mkpath(modelRoot_))
  {
    if(error) *error = tr("Could not create the model directory");
    return false;
  }
  QSaveFile catalogFile{QDir{modelRoot_}.filePath(QStringLiteral("catalog.json"))};
  if(!catalogFile.open(QIODevice::WriteOnly) || catalogFile.write(data) != data.size()
     || !catalogFile.commit())
  {
    if(error) *error = tr("Could not save the model catalog");
    return false;
  }
  for(const ModelCatalogEntry &entry : models_)
  {
    const bool installed = isInstalled(modelRoot_, entry);
    emit catalogModelFound(entry.id, entry.name, entry.version, entry.task, entry.adapter, installed);
    if(installed)
    {
      const QString directory = QDir{modelRoot_}.filePath(entry.id + QLatin1Char('/') + entry.version);
      QFile localManifest{QDir{directory}.filePath(QStringLiteral("model.json"))};
      if(!localManifest.open(QIODevice::ReadOnly)) continue;
      QString manifestError;
      const ModelManifest manifest = ModelCatalog::parseManifest(localManifest.readAll(), &manifestError);
      if(manifestError.isEmpty())
      {
        const QString location = entry.task == QStringLiteral("mask") ? directory
          : QDir{directory}.filePath(manifest.files.front().name);
        emit modelInstalled(entry.id, entry.name, entry.version, entry.task, entry.adapter, location);
      }
    }
  }
  return true;
}

void ModelDownloader::fetchNextManifest()
{
  if(cancelled_) return;
  while(modelIndex_ < models_.size()
        && (!models_[modelIndex_].available || isInstalled(modelRoot_, models_[modelIndex_])
            || (!selectedIds_.isEmpty() && !selectedIds_.contains(models_[modelIndex_].id))))
  {
    ++modelIndex_;
  }
  if(modelIndex_ >= models_.size())
  {
    const int installedCount = static_cast<int>(std::count_if(
      models_.cbegin(), models_.cend(), [this](const ModelCatalogEntry &model) {
        return model.available && isInstalled(modelRoot_, model);
      }));
    const int availableCount = static_cast<int>(std::count_if(
      models_.cbegin(), models_.cend(), [](const ModelCatalogEntry &model) {
        return model.available;
      }));
    emit progressChanged(100);
    emit statusChanged(tr("Model download complete: %1 installed, %2 not installed")
                       .arg(installedCount).arg(availableCount - installedCount));
    emit finished();
    return;
  }
  const ModelCatalogEntry entry = models_[modelIndex_];
  emit statusChanged(tr("Downloading %1 manifest...").arg(entry.name));
  fetchBytes(fileUrl(entry.manifestPath), [this, entry](const QByteArray bytes) {
    if(!ModelCatalog::verify(bytes, entry.manifestSize, entry.manifestSha256))
      return fail(tr("Manifest verification failed for %1").arg(entry.name));
    QString error;
    manifest_ = ModelCatalog::parseManifest(bytes, &error);
    if(!error.isEmpty() || manifest_.id != entry.id || manifest_.version != entry.version
       || manifest_.task != entry.task || manifest_.adapter != entry.adapter)
      return fail(error.isEmpty() ? tr("Manifest does not match catalog for %1").arg(entry.name) : error);
    manifestBytes_ = bytes;
    fileIndex_ = 0;
    stagingPath_ = QDir{modelRoot_}.filePath(
      QStringLiteral(".install-%1-%2").arg(entry.id, QUuid::createUuid().toString(QUuid::Id128)));
    if(!QDir{}.mkpath(stagingPath_)) return fail(tr("Could not create model staging directory"));
    fetchNextFile();
  });
}

void ModelDownloader::fetchNextFile()
{
  if(cancelled_) return;
  if(fileIndex_ >= manifest_.files.size()) return publishCurrentModel();
  const ModelFileEntry file = manifest_.files[fileIndex_];
  const QString manifestPath = models_[modelIndex_].manifestPath;
  const int slash = manifestPath.lastIndexOf(QLatin1Char('/'));
  const QString relative = slash < 0 ? file.name : manifestPath.left(slash + 1) + file.name;
  emit statusChanged(tr("Downloading %1: %2").arg(manifest_.name, file.name));
  beginFileDownload(fileUrl(relative), QDir{stagingPath_}.filePath(file.name), file);
}

void ModelDownloader::fetchBytes(const QUrl &url, const std::function<void(QByteArray)> &done)
{
  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  reply_ = network_.get(request);
  connect(reply_, &QNetworkReply::finished, this, [this, done] {
    QNetworkReply *completed = reply_;
    reply_ = nullptr;
    if(cancelled_)
    {
      completed->deleteLater();
      clearCurrentDownload();
      emit statusChanged(tr("Download cancelled"));
      emit finished();
      return;
    }
    if(completed->error() != QNetworkReply::NoError)
    {
      const QString message = completed->errorString();
      if(catalogOnly_ && completed->url().path().endsWith(QStringLiteral("/catalog.json"))
         && loadCachedCatalog())
      {
        completed->deleteLater();
        emit progressChanged(100);
        emit statusChanged(tr("Using cached model catalog: %1").arg(message));
        emit finished();
        return;
      }
      completed->deleteLater();
      return fail(tr("Download failed: %1").arg(message));
    }
    const QByteArray bytes = completed->readAll();
    completed->deleteLater();
    done(bytes);
  });
}

void ModelDownloader::beginFileDownload(const QUrl &url, const QString &path,
                                        const ModelFileEntry &file)
{
  output_.setFileName(path);
  if(!output_.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return fail(tr("Could not create %1").arg(path));
  hash_.reset();
  bytesWritten_ = 0;
  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  reply_ = network_.get(request);
  connect(reply_, &QNetworkReply::readyRead, this, [this] {
    const QByteArray bytes = reply_->readAll();
    if(output_.write(bytes) != bytes.size())
    {
      reply_->abort();
      return;
    }
    hash_.addData(bytes);
    bytesWritten_ += bytes.size();
  });
  connect(reply_, &QNetworkReply::downloadProgress, this, [this](const qint64 received, const qint64 total) {
    const int withinModel = total > 0 ? static_cast<int>((received * 100) / total) : 0;
    const int count = std::max(1, static_cast<int>(models_.size()));
    emit progressChanged(std::min(99, (modelIndex_ * 100 + withinModel) / count));
  });
  connect(reply_, &QNetworkReply::finished, this, [this, file] {
    QNetworkReply *completed = reply_;
    reply_ = nullptr;
    output_.close();
    if(cancelled_)
    {
      completed->deleteLater();
      clearCurrentDownload();
      emit statusChanged(tr("Download cancelled"));
      emit finished();
      return;
    }
    if(completed->error() != QNetworkReply::NoError)
    {
      const QString message = completed->errorString();
      completed->deleteLater();
      return fail(tr("Download failed: %1").arg(message));
    }
    completed->deleteLater();
    if(bytesWritten_ != file.size || hash_.result().toHex() != file.sha256)
      return fail(tr("File verification failed for %1").arg(file.name));
    ++fileIndex_;
    fetchNextFile();
  });
}

void ModelDownloader::publishCurrentModel()
{
  QSaveFile manifestFile{QDir{stagingPath_}.filePath(QStringLiteral("model.json"))};
  if(!manifestFile.open(QIODevice::WriteOnly)
     || manifestFile.write(manifestBytes_) != manifestBytes_.size() || !manifestFile.commit())
    return fail(tr("Could not stage the model manifest"));

  const ModelCatalogEntry entry = models_[modelIndex_];
  const QString modelDirectory = QDir{modelRoot_}.filePath(entry.id);
  const QString finalPath = QDir{modelDirectory}.filePath(entry.version);
  const QString backupPath = finalPath + QStringLiteral(".old-") + QUuid::createUuid().toString(QUuid::Id128);
  if(!QDir{}.mkpath(modelDirectory)) return fail(tr("Could not create model directory"));
  bool movedOld = false;
  if(QFileInfo::exists(finalPath))
  {
    movedOld = QDir{}.rename(finalPath, backupPath);
    if(!movedOld) return fail(tr("Could not replace the existing model installation"));
  }
  if(!QDir{}.rename(stagingPath_, finalPath))
  {
    if(movedOld) QDir{}.rename(backupPath, finalPath);
    return fail(tr("Could not publish the verified model"));
  }
  stagingPath_.clear();
  if(movedOld) QDir{backupPath}.removeRecursively();
  const QString location = manifest_.task == QStringLiteral("mask") ? finalPath
    : QDir{finalPath}.filePath(manifest_.files.front().name);
  emit modelInstalled(entry.id, entry.name, entry.version, entry.task, entry.adapter, location);
  ++modelIndex_;
  fetchNextManifest();
}

void ModelDownloader::fail(const QString &message)
{
  if(cancelled_) return;
  if(reply_)
  {
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
  }
  clearCurrentDownload();
  emit statusChanged(message);
  emit failed(message);
}

void ModelDownloader::clearCurrentDownload()
{
  if(output_.isOpen()) output_.close();
  if(!stagingPath_.isEmpty()) QDir{stagingPath_}.removeRecursively();
  stagingPath_.clear();
  manifest_ = {};
  manifestBytes_.clear();
}

QUrl ModelDownloader::fileUrl(const QString &relativePath) const
{
  return repositoryUrl_.resolved(QUrl{relativePath});
}
