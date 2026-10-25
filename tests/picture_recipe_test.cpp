#include "PictureRecipe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
  QFile file{path};
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

PictureRecipe recipeWithMask(const float value)
{
  PictureRecipe recipe;
  recipe.globalBlendMode = QStringLiteral("smart");
  recipe.previewWidth = 4;
  recipe.previewHeight = 2;
  PictureRecipeLayer layer;
  layer.id = 7;
  layer.name = QStringLiteral("Local");
  layer.blendMode = QStringLiteral("smart");
  layer.baseMask = rawlab::pipeline::Mask{4, 2, std::vector<float>(8, value)};
  layer.categoryMasks[1] = rawlab::pipeline::Mask{
    4, 2, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}};
  layer.categoryStrokes[2].push_back({{{0.25, 0.5}, {0.75, 0.5}}, 0.1f, 0.6f, 0.0f,
                                      QStringLiteral("Ellipse")});
  recipe.layers.push_back(std::move(layer));
  return recipe;
}

} // namespace

int main(int argc, char **argv)
{
  QCoreApplication app{argc, argv};
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;

  const QString missing = directory.filePath(QStringLiteral("missing.json"));
  if(PictureRecipeStore::loadAtPathResult(missing, 4, 2).outcome
     != PictureRecipeLoadOutcome::Missing) return 1;

  const QString unreadable = directory.filePath(QStringLiteral("not-a-file.json"));
  if(!QDir{}.mkpath(unreadable)
     || PictureRecipeStore::loadAtPathResult(unreadable, 4, 2).outcome
          != PictureRecipeLoadOutcome::Unreadable) return 1;

  const QString malformed = directory.filePath(QStringLiteral("malformed.json"));
  if(!writeFile(malformed, QByteArrayLiteral("{not-json"))
     || PictureRecipeStore::loadAtPathResult(malformed, 4, 2).outcome
          != PictureRecipeLoadOutcome::Invalid) return 1;

  const QString future = directory.filePath(QStringLiteral("future.json"));
  if(!writeFile(future, QByteArrayLiteral("{\"version\":12}"))
     || PictureRecipeStore::loadAtPathResult(future, 4, 2).outcome
          != PictureRecipeLoadOutcome::FutureVersion) return 1;

  const QString missingMask = directory.filePath(QStringLiteral("missing-mask.json"));
  if(!writeFile(missingMask, QByteArrayLiteral(
       "{\"version\":10,\"layers\":[{\"id\":1,\"baseMask\":\"gone.png\"}]}"))
     || PictureRecipeStore::loadAtPathResult(missingMask, 4, 2).outcome
          != PictureRecipeLoadOutcome::MissingMask) return 1;

  const QString corruptMask = directory.filePath(QStringLiteral("corrupt-mask.json"));
  if(!writeFile(directory.filePath(QStringLiteral("corrupt.png")), QByteArrayLiteral("not-a-png"))
     || !writeFile(corruptMask, QByteArrayLiteral(
       "{\"version\":10,\"layers\":[{\"id\":1,\"baseMask\":\"corrupt.png\"}]}"))
     || PictureRecipeStore::loadAtPathResult(corruptMask, 4, 2).outcome
          != PictureRecipeLoadOutcome::Invalid) return 1;

  const QString recipePath = directory.filePath(QStringLiteral("recipe.json"));
  const PictureRecipe original = recipeWithMask(0.25f);
  PictureRecipeStore::saveAtPath(recipePath, original);
  const auto loaded = PictureRecipeStore::loadAtPathResult(recipePath, 2, 4);
  if(!loaded.recipe || loaded.recipe->globalBlendMode != "smart" || loaded.recipe->layers.empty()
     || loaded.recipe->layers.front().blendMode != "smart") return 1;
  if(loaded.outcome != PictureRecipeLoadOutcome::Loaded || !loaded.recipe
     || loaded.recipe->previewWidth != 2 || loaded.recipe->previewHeight != 4
     || loaded.recipe->layers.front().categoryStrokes[2].front().tool != QStringLiteral("Ellipse")
     || loaded.recipe->layers.front().categoryMasks[1]->sampleNearest(0, 0) < 0.4f) return 1;

  QFile previousFile{recipePath};
  if(!previousFile.open(QIODevice::ReadOnly)) return 1;
  const QByteArray previousJson = previousFile.readAll();

  const QString probePath = directory.filePath(QStringLiteral("probe.json"));
  PictureRecipeStore::saveAtPath(probePath, recipeWithMask(0.75f));
  QFile probeFile{probePath};
  if(!probeFile.open(QIODevice::ReadOnly)) return 1;
  const QJsonObject probeRoot = QJsonDocument::fromJson(probeFile.readAll()).object();
  const QString probeMask = probeRoot.value(QStringLiteral("layers")).toArray().first().toObject()
                              .value(QStringLiteral("baseMask")).toString();
  const QString collisionName = probeMask;
  const QString expectedName = collisionName.startsWith(QStringLiteral("probe-"))
    ? QStringLiteral("recipe-") + collisionName.sliced(6) : QString{};
  if(expectedName.isEmpty() || !QDir{}.mkpath(directory.filePath(expectedName))) return 1;
  bool saveFailed = false;
  try { PictureRecipeStore::saveAtPath(recipePath, recipeWithMask(0.75f)); }
  catch(const std::exception &) { saveFailed = true; }
  QFile preserved{recipePath};
  if(!saveFailed || !preserved.open(QIODevice::ReadOnly) || preserved.readAll() != previousJson) return 1;

  std::cout << "Recipe outcomes, dimensions and atomic publication passed\n";
  return 0;
}
