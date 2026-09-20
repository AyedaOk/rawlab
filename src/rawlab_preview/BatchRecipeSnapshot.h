#pragma once

#include "BatchQueueStore.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

inline QString batchRecipeSnapshotPath(const BatchQueue &queue)
{
  return BatchQueueStore::directory() + '/' + queue.snapshotId + "/recipes.json";
}

inline void saveBatchRecipeSnapshots(const BatchQueue &queue, const QJsonArray &recipes)
{
  QJsonObject snapshots;
  for(int index = 0; index < static_cast<int>(queue.jobs.size()) && index < recipes.size(); ++index)
    snapshots[queue.jobs[static_cast<std::size_t>(index)].source] = recipes[index];
  QSaveFile file{batchRecipeSnapshotPath(queue)};
  const QByteArray bytes = QJsonDocument{snapshots}.toJson(QJsonDocument::Compact);
  if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error{"Cannot save per-photo batch recipes"};
}

inline QJsonObject loadBatchRecipeSnapshot(const BatchQueue &queue, const QString &source,
                                           const QJsonObject &legacyRecipe)
{
  QFile file{batchRecipeSnapshotPath(queue)};
  if(!file.exists()) return legacyRecipe;
  if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error{"Cannot read per-photo batch recipes"};
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto snapshots = document.object();
  const auto value = snapshots.value(source);
  if(!value.isObject()) throw std::runtime_error{"Missing recipe snapshot for batch photograph"};
  return value.toObject();
}
