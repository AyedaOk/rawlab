#include "ApplicationSettings.h"
#include "ModelCatalog.h"
#include "ModelDownloader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

namespace {

QByteArray sha256(const QByteArray &data)
{
  return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool writeFile(const QString &path, const QByteArray &data)
{
  QDir{}.mkpath(QFileInfo{path}.absolutePath());
  QFile file{path};
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application{argc, argv};
  const QByteArray modelBytes{"verified model bytes"};
  const QJsonObject manifestObject{
    {QStringLiteral("schema"), 1},
    {QStringLiteral("id"), QStringLiteral("test-model")},
    {QStringLiteral("name"), QStringLiteral("Test Model")},
    {QStringLiteral("version"), QStringLiteral("1.0")},
    {QStringLiteral("task"), QStringLiteral("denoise")},
    {QStringLiteral("adapter"), QStringLiteral("rawnind-bayer-v1")},
    {QStringLiteral("files"), QJsonArray{QJsonObject{
      {QStringLiteral("name"), QStringLiteral("model.onnx")},
      {QStringLiteral("size"), modelBytes.size()},
      {QStringLiteral("sha256"), QString::fromLatin1(sha256(modelBytes))},
    }}},
  };
  const QByteArray manifestBytes = QJsonDocument{manifestObject}.toJson(QJsonDocument::Compact);
  const QJsonObject catalogObject{
    {QStringLiteral("schema"), 1},
    {QStringLiteral("repository"), QStringLiteral("test/models")},
    {QStringLiteral("models"), QJsonArray{QJsonObject{
      {QStringLiteral("id"), QStringLiteral("test-model")},
      {QStringLiteral("name"), QStringLiteral("Test Model")},
      {QStringLiteral("version"), QStringLiteral("1.0")},
      {QStringLiteral("task"), QStringLiteral("denoise")},
      {QStringLiteral("adapter"), QStringLiteral("rawnind-bayer-v1")},
      {QStringLiteral("available"), true},
      {QStringLiteral("manifest"), QStringLiteral("models/test-model/1.0/model.json")},
      {QStringLiteral("manifestSize"), manifestBytes.size()},
      {QStringLiteral("manifestSha256"), QString::fromLatin1(sha256(manifestBytes))},
    }}},
  };
  const QByteArray catalogBytes = QJsonDocument{catalogObject}.toJson(QJsonDocument::Compact);

  QString error;
  const QList<ModelCatalogEntry> parsed = ModelCatalog::parseCatalog(catalogBytes, &error);
  if(!error.isEmpty() || parsed.size() != 1 || parsed.front().id != QStringLiteral("test-model")) return 1;
  if(ModelCatalog::isSafeRelativePath(QStringLiteral("../escape"))) return 1;
  if(!ModelCatalog::verify(modelBytes, modelBytes.size(), sha256(modelBytes))) return 1;
  if(ModelCatalog::verify(modelBytes + 'x', modelBytes.size(), sha256(modelBytes))) return 1;

  QTemporaryDir temporary;
  if(!temporary.isValid()) return 1;
  const QString repository = temporary.filePath(QStringLiteral("repository"));
  const QString manifestPath = QDir{repository}.filePath(QStringLiteral("models/test-model/1.0/model.json"));
  if(!writeFile(QDir{repository}.filePath(QStringLiteral("catalog.json")), catalogBytes)
     || !writeFile(manifestPath, manifestBytes)
     || !writeFile(QFileInfo{manifestPath}.dir().filePath(QStringLiteral("model.onnx")), modelBytes)) return 1;

  const QString installRoot = temporary.filePath(QStringLiteral("installed"));
  QUrl repositoryUrl = QUrl::fromLocalFile(repository + QLatin1Char('/'));
  ModelDownloader downloader{installRoot, repositoryUrl};
  QEventLoop loop;
  QString failure;
  int installed = 0;
  QObject::connect(&downloader, &ModelDownloader::modelInstalled, &loop,
                   [&installed](const QString &, const QString &, const QString &,
                                const QString &, const QString &) { ++installed; });
  QObject::connect(&downloader, &ModelDownloader::failed, &loop, [&failure, &loop](const QString &message) {
    failure = message;
    loop.quit();
  });
  QObject::connect(&downloader, &ModelDownloader::finished, &loop, &QEventLoop::quit);
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  downloader.start();
  loop.exec();
  if(!failure.isEmpty() || installed != 1 || !ModelDownloader::isInstalled(installRoot, parsed.front())) return 1;

  ApplicationSettingsData settings;
  settings.denoiseModels.push_back({QStringLiteral("manual"), QStringLiteral("Manual"),
                                    QStringLiteral("/manual.onnx")});
  ApplicationSettingsStore::registerInstalledModel(settings, QStringLiteral("test-model"),
    QStringLiteral("Test Model"), QStringLiteral("denoise"), QStringLiteral("rawnind-bayer-v1"),
    QDir{installRoot}.filePath(QStringLiteral("test-model/1.0/model.onnx")));
  if(settings.denoiseModels.size() != 2 || settings.denoiseModels.front().id != QStringLiteral("manual")
     || settings.denoiseModels.back().sourceId != QStringLiteral("test-model")) return 1;
  ApplicationSettingsStore::registerInstalledModel(settings, QStringLiteral("test-model"),
    QStringLiteral("Updated"), QStringLiteral("denoise"), QStringLiteral("rawnind-bayer-v1"),
    QStringLiteral("/updated.onnx"));
  if(settings.denoiseModels.size() != 2 || settings.denoiseModels.back().path != QStringLiteral("/updated.onnx")) return 1;
  return 0;
}
