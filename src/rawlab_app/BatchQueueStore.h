#pragma once
#include "BatchQueue.h"
#include "ExternalBatch.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>

class BatchQueueStore
{
public:
  static QString directory()
  {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/batch";
    if(!QDir{}.mkpath(path)) throw std::runtime_error{"Cannot create batch recovery directory"};
    return path;
  }
  static QString hashFile(const QString &path)
  {
    QFile file{path};
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read batch output for verification"};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if(!hash.addData(&file)) throw std::runtime_error{"Cannot verify batch output"};
    return QString::fromLatin1(hash.result().toHex());
  }
  static void save(BatchQueue &queue)
  {
    if(queue.temporary) { ExternalBatch::writeReceipt(queue); return; }
    const QString root = directory();
    if(queue.snapshotId.isEmpty())
    {
      const QString id = QUuid::createUuid().toString(QUuid::Id128);
      if(!QDir{root}.mkdir(id)) throw std::runtime_error{"Cannot create batch recipe snapshot"};
      PictureRecipeStore::saveAtPath(root + '/' + id + "/recipe.json", queue.recipe);
      queue.snapshotId = id;
    }
    QJsonObject settings{{"provider", queue.settings.onnxProvider},
                         {"device", queue.settings.onnxDeviceId},
                         {"runtime", queue.settings.onnxRuntimeLibrary}};
    const auto modelsJson = [](const auto &models) {
      QJsonArray array;
      for(const auto &model : models)
        array.push_back(QJsonObject{{"id", model.id}, {"name", model.name}, {"path", model.path},
          {"adapter", model.adapter}, {"sourceId", model.sourceId}, {"version", model.version}, {"origin", model.origin}});
      return array;
    };
    settings["denoise"] = modelsJson(queue.settings.denoiseModels);
    settings["sharpen"] = modelsJson(queue.settings.sharpenModels);
    QJsonArray jobs;
    for(const auto &job : queue.jobs)
      jobs.push_back(QJsonObject{{"source", job.source}, {"output", job.output},
        {"state", static_cast<int>(job.state)}, {"message", job.message}, {"hash", job.publishedHash}});
    const QJsonObject json{{"version", 1}, {"snapshot", queue.snapshotId},
      {"receipt", queue.receiptPath},
      {"width", queue.recipe.previewWidth}, {"height", queue.recipe.previewHeight},
      {"settings", settings}, {"jobs", jobs}};
    QSaveFile file{root + "/current.json"};
    const QByteArray bytes = QJsonDocument{json}.toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      throw std::runtime_error{"Cannot save batch recovery checkpoint"};
    ExternalBatch::writeReceipt(queue);
  }
  static std::optional<BatchQueue> load()
  {
    QFile file{directory() + "/current.json"};
    if(!file.exists()) return {};
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read saved batch"};
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    const QString id = json["snapshot"].toString();
    if(json["version"].toInt() != 1 || id.size() != 32 || id.contains('/') || id.contains('\\')
       || json["width"].toInt() <= 0 || json["height"].toInt() <= 0 || !json["jobs"].isArray())
      throw std::runtime_error{"Invalid batch recovery file"};
    BatchQueue queue;
    queue.snapshotId = id;
    queue.receiptPath = json["receipt"].toString();
    if(!queue.receiptPath.isEmpty() && !QDir::isAbsolutePath(queue.receiptPath))
      throw std::runtime_error{"Invalid saved batch receipt path"};
    auto recipe = PictureRecipeStore::loadAtPath(directory() + '/' + id + "/recipe.json",
      json["width"].toInt(), json["height"].toInt(), true);
    if(!recipe) throw std::runtime_error{"Batch recipe snapshot is missing or invalid"};
    queue.recipe = std::move(*recipe);
    const auto settings = json["settings"].toObject();
    queue.settings.onnxProvider = settings["provider"].toString();
    queue.settings.onnxDeviceId = settings["device"].toInt();
    queue.settings.onnxRuntimeLibrary = settings["runtime"].toString();
    const auto readModels = [](const QJsonArray &array, auto &models) {
      for(const auto &value : array)
      {
        const auto m = value.toObject();
        models.push_back({m["id"].toString(), m["name"].toString(), m["path"].toString(),
          m["adapter"].toString(), m["sourceId"].toString(), m["version"].toString(), m["origin"].toString()});
      }
    };
    readModels(settings["denoise"].toArray(), queue.settings.denoiseModels);
    readModels(settings["sharpen"].toArray(), queue.settings.sharpenModels);
    QSet<QString> outputs;
    for(const auto &value : json["jobs"].toArray())
    {
      const auto j = value.toObject();
      const int state = j["state"].toInt(-1);
      BatchQueue::Job job{j["source"].toString(), j["output"].toString(),
        static_cast<BatchQueue::State>(state), j["message"].toString(), j["hash"].toString()};
      if(state < 0 || state > 4 || !QDir::isAbsolutePath(job.source) || !QDir::isAbsolutePath(job.output)
         || outputs.contains(job.output)) throw std::runtime_error{"Invalid saved batch job"};
      outputs.insert(job.output);
      if(job.state == BatchQueue::State::Running)
      {
        // Crash after atomic publication but before the Completed checkpoint:
        // only treat the output as ours if it matches the pre-publication hash.
        bool published = false;
        if(!job.publishedHash.isEmpty() && QFileInfo::exists(job.output))
          published = hashFile(job.output) == job.publishedHash;
        job.state = published ? BatchQueue::State::Completed : BatchQueue::State::Pending;
      }
      queue.jobs.push_back(std::move(job));
    }
    return queue;
  }
};
