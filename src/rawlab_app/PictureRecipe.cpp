#include "PictureRecipe.h"

#include "PictureLibraryStore.h"

#include "rawlab/image/PngImage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

float clampUnit(const double value)
{
  return static_cast<float>(std::clamp(value, 0.0, 1.0));
}

} // namespace

QString PictureRecipeStore::pathForSource(const QString &sourcePath)
{
  const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/recipes");
  QDir{}.mkpath(directory);
  const QFileInfo sourceInfo{sourcePath};
  const QByteArray identity = PictureLibraryStore::normalizedPath(sourcePath).toUtf8()
                              + QByteArray::number(sourceInfo.size())
                              + QByteArray::number(sourceInfo.lastModified().toMSecsSinceEpoch());
  const QString key = QString::fromLatin1(
    QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
  return directory + QLatin1Char('/') + key + QStringLiteral(".json");
}

rawlab::pipeline::Mask PictureRecipeStore::resizeMask(
  const rawlab::pipeline::Mask &source, const int targetWidth, const int targetHeight)
{
  if(targetWidth <= 0 || targetHeight <= 0) throw std::invalid_argument{"invalid target mask size"};
  if(source.width() == targetWidth && source.height() == targetHeight) return source;
  std::vector<float> values(static_cast<std::size_t>(targetWidth * targetHeight));
  for(int y = 0; y < targetHeight; ++y)
  {
    const float sourceY = (static_cast<float>(y) + 0.5f) * source.height() / targetHeight - 0.5f;
    const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, source.height() - 1);
    const int y1 = std::min(y0 + 1, source.height() - 1);
    const float fy = std::clamp(sourceY - std::floor(sourceY), 0.0f, 1.0f);
    for(int x = 0; x < targetWidth; ++x)
    {
      const float sourceX = (static_cast<float>(x) + 0.5f) * source.width() / targetWidth - 0.5f;
      const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, source.width() - 1);
      const int x1 = std::min(x0 + 1, source.width() - 1);
      const float fx = std::clamp(sourceX - std::floor(sourceX), 0.0f, 1.0f);
      const float top = source.sampleNearest(x0, y0) * (1.0f - fx)
                        + source.sampleNearest(x1, y0) * fx;
      const float bottom = source.sampleNearest(x0, y1) * (1.0f - fx)
                           + source.sampleNearest(x1, y1) * fx;
      values[static_cast<std::size_t>(y * targetWidth + x)] = top * (1.0f - fy) + bottom * fy;
    }
  }
  return {targetWidth, targetHeight, std::move(values)};
}

PictureRecipe PictureRecipeStore::scaled(
  const PictureRecipe &source, const int targetWidth, const int targetHeight)
{
  PictureRecipe result = source;
  result.previewWidth = targetWidth;
  result.previewHeight = targetHeight;
  for(PictureRecipeLayer &layer : result.layers)
  {
    if(layer.baseMask) layer.baseMask = resizeMask(*layer.baseMask, targetWidth, targetHeight);
    for(auto &categoryMask : layer.categoryMasks)
      if(categoryMask) categoryMask = resizeMask(*categoryMask, targetWidth, targetHeight);
  }
  return result;
}

void PictureRecipeStore::save(const QString &sourcePath, const PictureRecipe &recipe) const
{
  if(sourcePath.isEmpty() || recipe.previewWidth <= 0 || recipe.previewHeight <= 0)
  {
    throw std::invalid_argument{"cannot save an empty picture recipe"};
  }
  QJsonObject root;
   root[QStringLiteral("version")] = 8;
  root[QStringLiteral("previewWidth")] = recipe.previewWidth;
  root[QStringLiteral("previewHeight")] = recipe.previewHeight;
  root[QStringLiteral("globalModelId")] = recipe.globalModelId;
  root[QStringLiteral("globalBlend")] = recipe.globalBlend;
  root[QStringLiteral("globalEnabled")] = recipe.globalEnabled;
  root[QStringLiteral("globalSharpenEnabled")] = recipe.globalSharpenEnabled;
   root[QStringLiteral("globalSharpenStrength")] = recipe.globalSharpenStrength;
   const SharpenMode globalMode = recipe.globalSharpenMode == SharpenMode::Off
     && recipe.globalSharpenEnabled ? SharpenMode::Ai : recipe.globalSharpenMode;
   root[QStringLiteral("globalSharpenMode")] = static_cast<int>(globalMode);
  root[QStringLiteral("preserveClipped")] = recipe.preserveClipped;

  const QFileInfo recipeInfo{pathForSource(sourcePath)};
  const auto saveMask = [&recipeInfo](const PictureRecipeLayer &layer,
                                      const rawlab::pipeline::Mask &mask,
                                      const QString &suffix) {
    QByteArray maskBytes = QByteArray::number(mask.width()) + 'x'
                           + QByteArray::number(mask.height()) + ':';
    maskBytes.append(reinterpret_cast<const char *>(mask.values().data()),
                     static_cast<qsizetype>(mask.values().size() * sizeof(float)));
    const QString maskHash = QString::fromLatin1(
      QCryptographicHash::hash(maskBytes, QCryptographicHash::Sha1).toHex());
    const QString fileName = recipeInfo.completeBaseName()
                             + QStringLiteral("-layer-%1-%2-%3.png")
                                 .arg(layer.id).arg(suffix, maskHash);
    const QString maskPath = recipeInfo.dir().filePath(fileName);
    if(!QFileInfo::exists(maskPath)) rawlab::image::writeMaskPng(maskPath.toStdString(), mask);
    return fileName;
  };
  const auto saveStrokes = [](const std::vector<PictureRecipeStroke> &strokes) {
    QJsonArray result;
    for(const PictureRecipeStroke &stroke : strokes)
    {
      QJsonArray points;
      for(const QPointF &point : stroke.points) points.push_back(QJsonArray{point.x(), point.y()});
      result.push_back(QJsonObject{{QStringLiteral("radius"), stroke.radius},
                                   {QStringLiteral("hardness"), stroke.hardness},
                                   {QStringLiteral("value"), stroke.value},
                                   {QStringLiteral("points"), points}});
    }
    return result;
  };
  QJsonArray layers;
  for(const PictureRecipeLayer &layer : recipe.layers)
  {
    QJsonObject object{{QStringLiteral("id"), layer.id},
                        {QStringLiteral("name"), layer.name},
                        {QStringLiteral("modelId"), layer.modelId},
                        {QStringLiteral("blend"), layer.blend},
                        {QStringLiteral("sharpenEnabled"), layer.sharpenEnabled},
                         {QStringLiteral("sharpenStrength"), layer.sharpenStrength},
                         {QStringLiteral("sharpenMode"), static_cast<int>(
                           layer.sharpenMode == SharpenMode::Off && layer.sharpenEnabled
                             ? SharpenMode::Ai : layer.sharpenMode)},
                       {QStringLiteral("enabled"), layer.enabled},
                       {QStringLiteral("inverted"), layer.inverted},
                        {QStringLiteral("showMask"), layer.showMask},
                        {QStringLiteral("feather"), layer.feather},
                        {QStringLiteral("blur"), layer.blur}};
    QJsonArray prompts;
    for(const PictureRecipePrompt &prompt : layer.prompts)
    {
      prompts.push_back(QJsonObject{{QStringLiteral("x"), prompt.x},
                                    {QStringLiteral("y"), prompt.y},
                                    {QStringLiteral("foreground"), prompt.foreground}});
    }
    object[QStringLiteral("prompts")] = prompts;
    QJsonArray strokes;
    for(const PictureRecipeStroke &stroke : layer.strokes)
    {
      QJsonArray points;
      for(const QPointF &point : stroke.points) points.push_back(QJsonArray{point.x(), point.y()});
      strokes.push_back(QJsonObject{{QStringLiteral("radius"), stroke.radius},
                                    {QStringLiteral("hardness"), stroke.hardness},
                                    {QStringLiteral("value"), stroke.value},
                                    {QStringLiteral("points"), points}});
    }
    object[QStringLiteral("strokes")] = strokes;
    if(layer.baseMask)
    {
      const QString fileName = saveMask(layer, *layer.baseMask, QStringLiteral("base"));
      object[QStringLiteral("baseMask")] = fileName;
    }
    QJsonArray categoryMasks;
    QJsonArray categoryStrokes;
    for(int category = 0; category < 3; ++category)
    {
      if(layer.categoryMasks[static_cast<std::size_t>(category)])
        categoryMasks.push_back(saveMask(layer, *layer.categoryMasks[static_cast<std::size_t>(category)],
                                          QStringLiteral("category-%1").arg(category)));
      else categoryMasks.push_back(QJsonValue{});
      categoryStrokes.push_back(saveStrokes(layer.categoryStrokes[static_cast<std::size_t>(category)]));
    }
    object[QStringLiteral("categoryMasks")] = categoryMasks;
    object[QStringLiteral("categoryStrokes")] = categoryStrokes;
    layers.push_back(object);
  }
  root[QStringLiteral("layers")] = layers;

  QSaveFile file{recipeInfo.filePath()};
  if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error{"failed to open picture recipe"};
  file.write(QJsonDocument{root}.toJson(QJsonDocument::Indented));
  if(!file.commit()) throw std::runtime_error{"failed to commit picture recipe"};
}

void PictureRecipeStore::remove(const QString &sourcePath) const
{
  const QString recipePath = pathForSource(sourcePath);
  const QFileInfo recipeInfo{recipePath};
  QFile::remove(recipePath);
  const QStringList masks = recipeInfo.dir().entryList(
    {recipeInfo.completeBaseName() + QStringLiteral("-layer-*")}, QDir::Files);
  for(const QString &mask : masks) recipeInfo.dir().remove(mask);
}

std::optional<PictureRecipe> PictureRecipeStore::load(
  const QString &sourcePath, const int targetWidth, const int targetHeight) const
{
  QFile file{pathForSource(sourcePath)};
  if(!file.open(QIODevice::ReadOnly)) return std::nullopt;
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  if(root.isEmpty()) return std::nullopt;
  const int version = root.value(QStringLiteral("version")).toInt(1);
   if(version > 8) return std::nullopt;
  PictureRecipe recipe;
  recipe.previewWidth = targetWidth;
  recipe.previewHeight = targetHeight;
  recipe.globalModelId = root.value(QStringLiteral("globalModelId")).toString();
  recipe.globalBlend = static_cast<float>(root.value(QStringLiteral("globalBlend")).toDouble(1.0));
  recipe.globalEnabled = root.value(QStringLiteral("globalEnabled")).toBool(true);
  recipe.globalSharpenEnabled = version >= 6
    && root.value(QStringLiteral("globalSharpenEnabled")).toBool(false);
   recipe.globalSharpenStrength = clampUnit(
     root.value(QStringLiteral("globalSharpenStrength")).toDouble(1.0));
   if(version >= 8)
     recipe.globalSharpenMode = static_cast<SharpenMode>(std::clamp(
       root.value(QStringLiteral("globalSharpenMode")).toInt(0), 0, 2));
   else if(recipe.globalSharpenEnabled) recipe.globalSharpenMode = SharpenMode::Ai;
  recipe.preserveClipped = version >= 5
    ? root.value(QStringLiteral("preserveClipped")).toBool(true) : true;
  const QFileInfo recipeInfo{file.fileName()};

  for(const QJsonValue &value : root.value(QStringLiteral("layers")).toArray())
  {
    const QJsonObject object = value.toObject();
    PictureRecipeLayer layer;
    layer.id = object.value(QStringLiteral("id")).toInt();
    layer.name = object.value(QStringLiteral("name")).toString();
    layer.modelId = object.value(QStringLiteral("modelId")).toString(recipe.globalModelId);
    layer.blend = static_cast<float>(object.value(QStringLiteral("blend")).toDouble(1.0));
    layer.sharpenEnabled = version >= 4
      && object.value(QStringLiteral("sharpenEnabled")).toBool(false);
    layer.sharpenStrength = clampUnit(
      object.value(QStringLiteral("sharpenStrength")).toDouble(1.0));
    if(version >= 8)
      layer.sharpenMode = static_cast<SharpenMode>(std::clamp(
        object.value(QStringLiteral("sharpenMode")).toInt(0), 0, 2));
    else if(layer.sharpenEnabled) layer.sharpenMode = SharpenMode::Ai;
    layer.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    layer.inverted = object.value(QStringLiteral("inverted")).toBool(false);
    layer.showMask = object.value(QStringLiteral("showMask")).toBool(true);
    layer.feather = clampUnit(object.value(QStringLiteral("feather")).toDouble(0.0));
    layer.blur = clampUnit(object.value(QStringLiteral("blur")).toDouble(0.0));

    const QString maskName = object.value(version >= 2 ? QStringLiteral("baseMask")
                                                       : QStringLiteral("samMask")).toString();
    if(!maskName.isEmpty())
    {
      try
      {
        rawlab::pipeline::Mask mask = rawlab::image::readMaskPng(
          recipeInfo.dir().filePath(maskName).toStdString());
        layer.baseMask = resizeMask(mask, targetWidth, targetHeight);
      }
      catch(const std::exception &) {}
    }
    int sourceWidth = root.value(QStringLiteral("previewWidth")).toInt();
    int sourceHeight = root.value(QStringLiteral("previewHeight")).toInt();
    if((sourceWidth <= 0 || sourceHeight <= 0) && !maskName.isEmpty())
    {
      try
      {
        const auto original = rawlab::image::readMaskPng(recipeInfo.dir().filePath(maskName).toStdString());
        sourceWidth = original.width();
        sourceHeight = original.height();
      }
      catch(const std::exception &) {}
    }
    if(sourceWidth <= 0) sourceWidth = targetWidth;
    if(sourceHeight <= 0) sourceHeight = targetHeight;
    if(version < 2 && maskName.isEmpty() && std::max(sourceWidth, sourceHeight) == 1440)
    {
      // Legacy v1 omitted dimensions; the current canvas represents the old 1800-pixel canvas.
      const float historicalScale = 1800.0f / std::max(sourceWidth, sourceHeight);
      sourceWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * historicalScale)));
      sourceHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * historicalScale)));
    }
    const float radiusScale = static_cast<float>(std::max(1, std::min(sourceWidth, sourceHeight)));

    for(const QJsonValue &promptValue : object.value(QStringLiteral("prompts")).toArray())
    {
      const QJsonObject prompt = promptValue.toObject();
      const double x = prompt.value(QStringLiteral("x")).toDouble();
      const double y = prompt.value(QStringLiteral("y")).toDouble();
      layer.prompts.push_back({clampUnit(version >= 2 ? x : x / sourceWidth),
                               clampUnit(version >= 2 ? y : y / sourceHeight),
                               prompt.value(QStringLiteral("foreground")).toBool(true)});
    }
    for(const QJsonValue &strokeValue : object.value(QStringLiteral("strokes")).toArray())
    {
      const QJsonObject strokeObject = strokeValue.toObject();
      PictureRecipeStroke stroke;
      const double radius = strokeObject.value(QStringLiteral("radius")).toDouble(30.0);
      stroke.radius = clampUnit(version >= 2 ? radius : radius / radiusScale);
      stroke.hardness = clampUnit(strokeObject.value(QStringLiteral("hardness")).toDouble(0.8));
      stroke.value = clampUnit(strokeObject.value(QStringLiteral("value")).toDouble(1.0));
      for(const QJsonValue &pointValue : strokeObject.value(QStringLiteral("points")).toArray())
      {
        const QJsonArray point = pointValue.toArray();
        if(point.size() != 2) continue;
        const double x = point[0].toDouble();
        const double y = point[1].toDouble();
        stroke.points.push_back({clampUnit(version >= 2 ? x : x / sourceWidth),
                                 clampUnit(version >= 2 ? y : y / sourceHeight)});
      }
      layer.strokes.push_back(std::move(stroke));
    }
    if(version >= 7)
    {
      const QJsonArray categoryMasks = object.value(QStringLiteral("categoryMasks")).toArray();
      const QJsonArray categoryStrokes = object.value(QStringLiteral("categoryStrokes")).toArray();
      for(int category = 0; category < 3; ++category)
      {
        const std::size_t index = static_cast<std::size_t>(category);
        const QString categoryMaskName = category < categoryMasks.size()
          ? categoryMasks[category].toString() : QString{};
        if(!categoryMaskName.isEmpty())
        {
          try
          {
            layer.categoryMasks[index] = resizeMask(
              rawlab::image::readMaskPng(recipeInfo.dir().filePath(categoryMaskName).toStdString()),
              targetWidth, targetHeight);
          }
          catch(const std::exception &) {}
        }
        const QJsonArray strokes = category < categoryStrokes.size()
          ? categoryStrokes[category].toArray() : QJsonArray{};
        for(const QJsonValue &strokeValue : strokes)
        {
          const QJsonObject strokeObject = strokeValue.toObject();
          PictureRecipeStroke stroke;
          stroke.radius = clampUnit(strokeObject.value(QStringLiteral("radius")).toDouble());
          stroke.hardness = clampUnit(strokeObject.value(QStringLiteral("hardness")).toDouble(0.8));
          stroke.value = clampUnit(strokeObject.value(QStringLiteral("value")).toDouble(1.0));
          for(const QJsonValue &pointValue : strokeObject.value(QStringLiteral("points")).toArray())
          {
            const QJsonArray point = pointValue.toArray();
            if(point.size() != 2) continue;
            stroke.points.push_back({clampUnit(point[0].toDouble()),
                                     clampUnit(point[1].toDouble())});
          }
          layer.categoryStrokes[index].push_back(std::move(stroke));
        }
      }
    }
    recipe.layers.push_back(std::move(layer));
  }
  return recipe;
}
