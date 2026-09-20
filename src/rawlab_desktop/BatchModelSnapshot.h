#pragma once

#include "BatchQueueStore.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#include <algorithm>

inline QString batchModelSnapshotPath(const BatchQueue &queue)
{
  return BatchQueueStore::directory() + '/' + queue.snapshotId + "/models.json";
}

inline QString batchModelPendingPath()
{
  return BatchQueueStore::directory() + "/model-snapshot.pending";
}

inline QMap<QString, QString> selectedBatchModels(const BatchQueue &queue)
{
  QMap<QString, QString> selected;
  QSet<QString> denoiseIds;
  if(queue.recipe.globalEnabled && !queue.recipe.globalModelId.isEmpty()) denoiseIds.insert(queue.recipe.globalModelId);
  for(const auto &layer : queue.recipe.layers)
    if(layer.enabled && !layer.modelId.isEmpty()) denoiseIds.insert(layer.modelId);
  for(const auto &model : queue.settings.denoiseModels)
    if(denoiseIds.contains(model.id)) selected["denoise:" + model.id] = model.path;
  for(const auto &id : denoiseIds)
    if(!selected.contains("denoise:" + id)) throw std::runtime_error{"Selected denoise model is missing from the batch snapshot"};
  const bool usesAiSharpen = (queue.recipe.globalEnabled && queue.recipe.globalSharpenMode == SharpenMode::Ai)
    || std::any_of(queue.recipe.layers.begin(), queue.recipe.layers.end(),
                   [](const auto &layer) { return layer.enabled && layer.sharpenMode == SharpenMode::Ai; });
  QString sharpenId = queue.recipe.sharpenModelId;
  if(sharpenId.isEmpty() && !queue.settings.sharpenModels.isEmpty())
    sharpenId = queue.settings.sharpenModels.front().id;
  if(usesAiSharpen && sharpenId.isEmpty())
    throw std::runtime_error{"Selected sharpening model is missing from the batch snapshot"};
  if(usesAiSharpen && !sharpenId.isEmpty())
  {
    for(const auto &model : queue.settings.sharpenModels)
      if(model.id == sharpenId) selected["sharpen:" + model.id] = model.path;
    if(!selected.contains("sharpen:" + sharpenId))
      throw std::runtime_error{"Selected sharpening model is missing from the batch snapshot"};
  }
  return selected;
}

inline void beginBatchModelSnapshot()
{
  QSaveFile file{batchModelPendingPath()};
  if(!file.open(QIODevice::WriteOnly) || file.write("pending") != 7 || !file.commit())
    throw std::runtime_error{"Cannot prepare batch model snapshot"};
}

inline void finishBatchModelSnapshot()
{
  if(!QFile::remove(batchModelPendingPath()) && QFileInfo::exists(batchModelPendingPath()))
    throw std::runtime_error{"Cannot finalize batch model snapshot"};
}

inline void saveBatchModelSnapshot(const BatchQueue &queue)
{
  QJsonObject models;
  const auto selected = selectedBatchModels(queue);
  for(auto entry = selected.cbegin(); entry != selected.cend(); ++entry)
    models[entry.key()] = BatchQueueStore::hashFile(entry.value());
  QSaveFile file{batchModelSnapshotPath(queue)};
  const QByteArray bytes = QJsonDocument{models}.toJson(QJsonDocument::Compact);
  if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error{"Cannot save batch model snapshot"};
}

inline void verifyBatchModelSnapshot(const BatchQueue &queue)
{
  QFile file{batchModelSnapshotPath(queue)};
  if(!file.exists()) return; // Recovery files created before model fingerprints remain readable.
  if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read batch model snapshot"};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &error);
  if(error.error != QJsonParseError::NoError || !document.isObject())
    throw std::runtime_error{"Invalid batch model snapshot"};
  const auto hashes = document.object();
  const auto selected = selectedBatchModels(queue);
  for(auto entry = selected.cbegin(); entry != selected.cend(); ++entry)
  {
    const QString expected = hashes[entry.key()].toString();
    if(expected.size() != 64) throw std::runtime_error{"Batch model snapshot is incomplete"};
    if(!QFileInfo::exists(entry.value()) || BatchQueueStore::hashFile(entry.value()) != expected)
      throw std::runtime_error{"A model changed after this batch was created"};
  }
}

inline std::optional<BatchQueue> loadVerifiedBatchQueue()
{
  auto queue = BatchQueueStore::load();
  if(queue)
  {
    if(QFileInfo::exists(batchModelPendingPath()) && !QFileInfo::exists(batchModelSnapshotPath(*queue)))
      throw std::runtime_error{"Batch model snapshot was interrupted"};
    verifyBatchModelSnapshot(*queue);
    if(QFileInfo::exists(batchModelPendingPath())) finishBatchModelSnapshot();
  }
  return queue;
}
