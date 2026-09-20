#pragma once
#include "ProcessingSession.h"
#include "PictureLibraryStore.h"
#include "PictureRecipe.h"
#include "ModelDownloader.h"
#include "BatchQueueStore.h"
#include "BatchModelSnapshot.h"
#include "BatchRecipeSnapshot.h"
#include <QFileInfo>
#include <QDir>
#include <QJsonValue>
#include <QEventLoop>
#include <QLockFile>
#include <QTimer>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>
#include <functional>
#include <stdexcept>

inline QJsonObject desktopControl(const QJsonObject &request, const std::function<void(QJsonObject)> &progress)
{
  const auto operation=request["operation"].toString();
  auto settings=ApplicationSettingsStore{}.load();
  if(operation=="settings")
  {
    auto result=ProcessingSession::models();QJsonArray sam,providers;
    for(const auto &m:settings.samModels)sam.push_back(QJsonObject{{"id",m.id},{"name",m.name},{"folder",m.folder},{"sourceId",m.sourceId},{"version",m.version},{"origin",m.origin}});
    QString runtimeError;
    try {for(const auto &p:rawlab::ai::OnnxSession::availableProviders(settings.onnxRuntimeLibrary.toStdString()))providers.push_back(QJsonObject{{"id",QString::fromStdString(p.id)},{"name",QString::fromStdString(p.name)}});}
    catch(const std::exception &e){runtimeError=QString::fromUtf8(e.what());}
    result["sam"]=sam;result["activeSamModelId"]=settings.activeSamModelId;result["providers"]=providers;
    result["runtimeLibrary"]=settings.onnxRuntimeLibrary;result["deviceId"]=settings.onnxDeviceId;result["runtimeError"]=runtimeError;
    return result;
  }
  if(operation=="settings_save")
  {
    const auto provider=request["provider"].toString("auto");
    if(provider!="auto" && provider!="cpu" && provider!="cuda" && provider!="rocm" && provider!="tensorrt") throw std::runtime_error{"Unsupported ONNX provider"};
    const auto deviceId=request["deviceId"].toInt(-1);if(deviceId<0) throw std::runtime_error{"Device ID must be non-negative"};
    const auto runtime=request["runtimeLibrary"].toString();
    if(!runtime.isEmpty() && !QFileInfo{runtime}.isFile()) throw std::runtime_error{"ONNX Runtime library does not exist"};
    const auto active=request["activeSamModelId"].toString();
    if(!active.isEmpty() && ApplicationSettingsStore::samModelById(settings,active).id.isEmpty()) throw std::runtime_error{"Selected SAM model is not installed"};
    settings.onnxProvider=provider;settings.onnxDeviceId=deviceId;settings.onnxRuntimeLibrary=runtime;settings.activeSamModelId=active;
    ApplicationSettingsStore{}.save(settings);return {{"saved",true}};
  }
  if(operation=="model_add" || operation=="model_remove")
  {
    const auto task=request["task"].toString(),id=request["id"].toString(),path=request["path"].toString(),name=request["name"].toString();
    if(operation=="model_remove")
    {
      settings.denoiseModels.removeIf([&](const auto &m){return task=="denoise"&&m.id==id;});
      settings.sharpenModels.removeIf([&](const auto &m){return task=="sharpen"&&m.id==id;});
      settings.samModels.removeIf([&](const auto &m){return task=="mask"&&m.id==id;});
      if(task=="mask"&&settings.activeSamModelId==id)settings.activeSamModelId=settings.samModels.isEmpty()?QString{}:settings.samModels.front().id;
    }
    else
    {
      if(name.trimmed().isEmpty() || (task!="denoise"&&task!="sharpen"&&task!="mask"))throw std::runtime_error{"Invalid local model definition"};
      if(task=="mask" ? !QFileInfo{QDir{path}.filePath("encoder.onnx")}.isFile()||!QFileInfo{QDir{path}.filePath("decoder.onnx")}.isFile() : !QFileInfo{path}.isFile())throw std::runtime_error{"Model files are missing"};
      const auto normalized=QFileInfo{path}.absoluteFilePath();
      const auto digest=QCryptographicHash::hash(normalized.toUtf8(),QCryptographicHash::Sha256).toHex().left(24);
      ApplicationSettingsStore::registerInstalledModel(settings,QStringLiteral("local:%1").arg(QString::fromLatin1(digest)),name,task,request["adapter"].toString(),normalized,"local","manual");
    }
    ApplicationSettingsStore{}.save(settings);return {{"saved",true}};
  }
  if(operation=="catalog" || operation=="model_download")
  {
    ModelDownloader downloader;QEventLoop loop;QJsonArray entries;QString error;
    QObject::connect(&downloader,&ModelDownloader::catalogModelFound,&loop,[&](QString id,QString name,QString version,QString task,QString adapter,bool installed){entries.push_back(QJsonObject{{"id",id},{"name",name},{"version",version},{"task",task},{"adapter",adapter},{"installed",installed}});});
    QObject::connect(&downloader,&ModelDownloader::modelInstalled,&loop,[&](QString id,QString name,QString version,QString task,QString adapter,QString location){ApplicationSettingsStore::registerInstalledModel(settings,id,name,task,adapter,location,version);ApplicationSettingsStore{}.save(settings);});
    QObject::connect(&downloader,&ModelDownloader::progressChanged,&loop,[&](int percent){progress({{"progress",percent},{"total",100},{"stage","Downloading model"}});});
    QObject::connect(&downloader,&ModelDownloader::failed,&loop,[&](QString message){error=message;loop.quit();});
    QObject::connect(&downloader,&ModelDownloader::finished,&loop,&QEventLoop::quit);
    QTimer cancel;cancel.setInterval(100);QObject::connect(&cancel,&QTimer::timeout,&loop,[&]{if(QFileInfo::exists(request["cancelPath"].toString())){error="Download cancelled";downloader.cancel();loop.quit();}});cancel.start();
    QTimer::singleShot(0,&downloader,[&]{if(operation=="catalog")downloader.refreshCatalog();else downloader.start({request["id"].toString()});});
    loop.exec();if(!error.isEmpty())throw std::runtime_error{error.toStdString()};return {{"catalog",entries}};
  }
  if(operation=="library_list" || operation=="library_add" || operation=="library_remove")
  {
    PictureLibraryStore store;QStringList paths;for(const auto &p:request["paths"].toArray())paths.push_back(p.toString());
    if(operation=="library_add")store.addFiles(paths);
    if(operation=="library_remove"){for(const auto &path:paths)PictureRecipeStore{}.remove(path);store.removeFiles(paths);}
    return {{"paths",QJsonArray::fromStringList(store.files())}};
  }
  if(operation == "library_metadata" || operation == "library_metadata_save")
  {
    const QString path = request["path"].toString();
    if(path.isEmpty()) throw std::runtime_error{"Missing library metadata path"};
    PictureLibraryStore store;
    if(operation == "library_metadata_save")
    {
      QStringList labels;
      for(const auto &label : request["labels"].toArray()) labels.push_back(label.toString());
      store.saveMetadata(path, request["rating"].toInt(0), labels);
    }
    const auto metadata = store.metadata(path);
    return {{"rating", metadata.rating}, {"labels", QJsonArray::fromStringList(metadata.labels)}};
  }
  const auto batchJson = [](const BatchQueue &queue) {
    QJsonArray jobs;
    for(const auto &job : queue.jobs)
      jobs.push_back(QJsonObject{{"source", job.source}, {"output", job.output},
        {"state", static_cast<int>(job.state)}, {"message", job.message},
        {"completed", job.state == BatchQueue::State::Completed}});
    return QJsonObject{{"snapshot", queue.snapshotId}, {"jobs", jobs},
                       {"completed", queue.completed()}, {"remaining", queue.remaining()}};
  };
  std::unique_ptr<QLockFile> batchLock;
  if(operation.startsWith("batch_"))
  {
    batchLock = std::make_unique<QLockFile>(BatchQueueStore::directory() + "/queue.lock");
    batchLock->setStaleLockTime(0);
    if(!batchLock->tryLock(0)) throw std::runtime_error{"Batch queue is open in another RawLab process"};
  }
  if(operation == "batch_discard")
  {
    if(!QFile::remove(BatchQueueStore::directory() + "/current.json")
       && QFileInfo::exists(BatchQueueStore::directory() + "/current.json"))
      throw std::runtime_error{"Cannot discard damaged batch recovery file"};
    QFile::remove(batchModelPendingPath());
    return QJsonObject{{"snapshot", QJsonValue::Null}, {"jobs", QJsonArray{}},
                       {"completed", 0}, {"remaining", 0}};
  }
  if(operation == "batch_create")
  {
    if(const auto existing = loadVerifiedBatchQueue(); existing && existing->remaining())
      throw std::runtime_error{"An unfinished batch already exists; resume, retry, or cancel it first"};
    const auto sourceValues = request["sources"].toArray();
    QStringList sources;
    for(const auto &value : sourceValues) sources.push_back(value.toString());
    const QString directory = request["outputDirectory"].toString();
    if(sources.isEmpty() || directory.isEmpty() || !QFileInfo{directory}.isDir())
      throw std::runtime_error{"Batch requires source files and an output directory"};
    auto recipe = toQtRecipe(request["recipe"].toObject(), request["width"].toInt(1024),
                             request["height"].toInt(1024));
    BatchQueue queue = BatchQueue::create(sources, directory, std::move(recipe), settings);
    queue.temporary = request["temporary"].toBool(false);
    try
    {
      if(!queue.temporary) beginBatchModelSnapshot();
      BatchQueueStore::save(queue);
      if(!queue.temporary && request["recipes"].isArray() && !request["recipes"].toArray().isEmpty())
        saveBatchRecipeSnapshots(queue, request["recipes"].toArray());
      if(!queue.temporary) { saveBatchModelSnapshot(queue); finishBatchModelSnapshot(); }
    }
    catch(...)
    {
      if(!queue.temporary)
      {
        QFile::remove(BatchQueueStore::directory() + "/current.json");
        QFile::remove(batchModelPendingPath());
      }
      throw;
    }
    return batchJson(queue);
  }
  if(operation == "batch_status")
  {
    const auto queue = loadVerifiedBatchQueue();
    return queue ? batchJson(*queue) : QJsonObject{{"snapshot", QJsonValue::Null},
                                                    {"jobs", QJsonArray{}},
                                                    {"completed", 0}, {"remaining", 0}};
  }
  if(operation == "batch_update")
  {
    auto queue = loadVerifiedBatchQueue();
    if(!queue) throw std::runtime_error{"No recoverable batch exists"};
    const int index = request["index"].toInt(-1);
    if(index < 0 || index >= static_cast<int>(queue->jobs.size()))
      throw std::runtime_error{"Invalid batch job index"};
    auto &job = queue->jobs[static_cast<std::size_t>(index)];
    const int state = request["state"].toInt(-1);
    if(state < 0 || state > static_cast<int>(BatchQueue::State::Cancelled))
      throw std::runtime_error{"Invalid batch job state"};
    job.state = static_cast<BatchQueue::State>(state);
    job.message = request["message"].toString();
    if(job.state == BatchQueue::State::Completed)
    {
      if(!QFileInfo::exists(job.output)) throw std::runtime_error{"Completed batch output is missing"};
      job.publishedHash = BatchQueueStore::hashFile(job.output);
    }
    BatchQueueStore::save(*queue);
    return batchJson(*queue);
  }
  if(operation == "batch_retry")
  {
    auto queue = loadVerifiedBatchQueue();
    if(!queue) throw std::runtime_error{"No recoverable batch exists"};
    for(auto &job : queue->jobs)
      if(job.state == BatchQueue::State::Failed || job.state == BatchQueue::State::Cancelled)
        job.state = BatchQueue::State::Pending;
    BatchQueueStore::save(*queue);
    return batchJson(*queue);
  }
  if(operation == "batch_cancel")
  {
    auto queue = loadVerifiedBatchQueue();
    if(!queue) throw std::runtime_error{"No recoverable batch exists"};
    for(auto &job : queue->jobs)
      if(job.state == BatchQueue::State::Pending || job.state == BatchQueue::State::Running)
        job.state = BatchQueue::State::Cancelled;
    BatchQueueStore::save(*queue);
    return batchJson(*queue);
  }
  if(operation=="preset_list")return {{"names",QJsonArray::fromStringList(PictureRecipeStore{}.presetNames())}};
  if(operation=="preset_delete"){PictureRecipeStore{}.removePreset(request["name"].toString());return {{"saved",true}};}
  if(operation=="preset_save" || operation=="recipe_save")
  {
    auto recipe=toQtRecipe(request["recipe"].toObject(),request["width"].toInt(1024),request["height"].toInt(1024));
    if(operation=="preset_save")PictureRecipeStore{}.savePreset(request["name"].toString(),recipe);
    else PictureRecipeStore{}.save(request["path"].toString(),recipe);
    return {{"saved",true}};
  }
  if(operation=="preset_load" || operation=="recipe_load")
  {
    const auto result=operation=="preset_load"
      ? PictureRecipeStore{}.loadPresetResult(request["name"].toString(),request["width"].toInt(1024),request["height"].toInt(1024))
      : PictureRecipeStore{}.loadResult(request["path"].toString(),request["width"].toInt(1024),request["height"].toInt(1024));
    return {{"outcome",pictureRecipeLoadOutcomeName(result.outcome)},
            {"error",result.message},
            {"recipe",result.recipe?QJsonValue{fromQtRecipe(*result.recipe)}:QJsonValue{QJsonValue::Null}}};
  }
  throw std::runtime_error{"Unknown control request"};
}
