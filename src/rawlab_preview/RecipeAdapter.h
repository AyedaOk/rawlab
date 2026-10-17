#pragma once
#include "PictureRecipe.h"
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

inline QString maskPng(const rawlab::pipeline::Mask &mask)
{
  QImage image{mask.width(),mask.height(),QImage::Format_Grayscale8};
  for(int y=0;y<mask.height();++y)for(int x=0;x<mask.width();++x)
    image.scanLine(y)[x]=static_cast<unsigned char>(std::clamp(mask.sampleNearest(x,y),0.0f,1.0f)*255.0f+0.5f);
  QByteArray bytes;QBuffer buffer{&bytes};buffer.open(QIODevice::WriteOnly);
  if(!image.save(&buffer,"PNG"))throw std::runtime_error{"Cannot encode recipe mask"};
  return QString::fromLatin1(bytes.toBase64());
}

inline std::optional<rawlab::pipeline::Mask> decodeMaskPng(const QJsonValue &value, int width, int height)
{
  const QByteArray data = QByteArray::fromBase64(value.toString().toLatin1());
  if(data.isEmpty()) return std::nullopt;
  const QImage image = QImage::fromData(data, "PNG").convertToFormat(QImage::Format_Grayscale8)
                         .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if(image.isNull()) throw std::runtime_error{"Invalid recipe mask PNG"};
  std::vector<float> values(static_cast<std::size_t>(width * height));
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
      values[static_cast<std::size_t>(y * width + x)] = image.constScanLine(y)[x] / 255.0f;
  return rawlab::pipeline::Mask{width, height, std::move(values)};
}

inline QJsonArray strokeArray(const std::vector<PictureRecipeStroke> &strokes)
{
  QJsonArray result;
  for(const auto &stroke : strokes)
  {
    QJsonArray points;
    for(const auto &point : stroke.points) points.push_back(QJsonArray{point.x(), point.y()});
    result.push_back(QJsonObject{{"tool", stroke.tool}, {"radius", stroke.radius}, {"hardness", stroke.hardness},
                                 {"value", stroke.value}, {"points", points}});
  }
  return result;
}

inline std::vector<PictureRecipeStroke> readStrokes(const QJsonArray &values)
{
  std::vector<PictureRecipeStroke> result;
  for(const auto &value : values)
  {
    const auto object = value.toObject();
    PictureRecipeStroke stroke;
    stroke.tool = object["tool"].toString("Brush");
    stroke.radius = static_cast<float>(std::clamp(object["radius"].toDouble(.03), .001, 1.0));
    stroke.hardness = static_cast<float>(std::clamp(object["hardness"].toDouble(.8), 0.0, 1.0));
    stroke.value = static_cast<float>(std::clamp(object["value"].toDouble(1.0), 0.0, 1.0));
    for(const auto &pointValue : object["points"].toArray())
    {
      const auto point = pointValue.toArray();
      if(point.size() == 2) stroke.points.emplace_back(point[0].toDouble(), point[1].toDouble());
    }
    if(!stroke.points.empty()) result.push_back(std::move(stroke));
  }
  return result;
}

// Pack a normalized 16-bit depth sample into R/G bytes. Browser PNG decoders retain
// both bytes; a grayscale16 PNG would be reduced to 8 bits by Canvas.
inline QString depthMapPng(const rawlab::pipeline::Mask &depth)
{
  QImage image{depth.width(), depth.height(), QImage::Format_RGB888};
  for(int y = 0; y < depth.height(); ++y) for(int x = 0; x < depth.width(); ++x)
  {
    const int value = static_cast<int>(std::lround(depth.sampleNearest(x, y) * 65535));
    auto *pixel = image.scanLine(y) + x * 3;
    pixel[0] = static_cast<unsigned char>(value >> 8);
    pixel[1] = static_cast<unsigned char>(value & 255);
    pixel[2] = 0;
  }
  QByteArray bytes; QBuffer buffer{&bytes}; buffer.open(QIODevice::WriteOnly);
  if(!image.save(&buffer, "PNG")) throw std::runtime_error{"Cannot encode depth map"};
  return QString::fromLatin1(bytes.toBase64());
}

inline rawlab::pipeline::Mask decodeDepthMap(const QJsonValue &value)
{
  auto bytes = QByteArray::fromBase64(value.toString().toLatin1());
  QBuffer buffer{&bytes}; buffer.open(QIODevice::ReadOnly);
  QImageReader reader{&buffer, "PNG"};
  const auto size = reader.size();
  if(!size.isValid() || size.width() > 2048 || size.height() > 2048
     || static_cast<qint64>(size.width()) * size.height() > 2 * 1024 * 1024)
    throw std::runtime_error{"Invalid or oversized depth map PNG"};
  const auto image = reader.read().convertToFormat(QImage::Format_RGB888);
  if(image.isNull()) throw std::runtime_error{"Invalid depth map PNG"};
  std::vector<float> values(static_cast<std::size_t>(image.width()) * image.height());
  for(int y = 0; y < image.height(); ++y) for(int x = 0; x < image.width(); ++x)
  {
    const auto *pixel = image.constScanLine(y) + x * 3;
    values[static_cast<std::size_t>(y * image.width() + x)] = (pixel[0] * 256 + pixel[1]) / 65535.0f;
  }
  return {image.width(), image.height(), std::move(values)};
}

inline rawlab::pipeline::Mask desktopMask(const QJsonObject &layer,int w,int h, const QJsonObject &depthMaps = {})
{
  const auto emptyMask = [w, h] { return rawlab::pipeline::Mask{w, h, std::vector<float>(static_cast<std::size_t>(w * h), 0.0f)}; };
  const auto applyOperations = [w, h](rawlab::pipeline::Mask mask, const QJsonArray &operations) {
    const float edge = std::min(w, h);
    for(const auto &value : operations)
    {
      const auto mark = value.toObject();
      const auto points = mark["points"].toArray();
      if(points.empty()) continue;
      const auto xy = [w, h](const QJsonValue &pointValue) {
        const auto point = pointValue.toArray();
        return std::pair{static_cast<float>(std::clamp(point[0].toDouble(), 0.0, 1.0) * w),
                         static_cast<float>(std::clamp(point[1].toDouble(), 0.0, 1.0) * h)};
      };
      auto from = xy(points.first());
      const auto to = xy(points.last());
      const QString tool = mark["tool"].toString(mark["value"].toDouble(1.0) <= 0.5 ? "Erase" : "Brush");
      const bool erase = tool == "Erase" || mark["erase"].toBool() || mark["value"].toDouble(1.0) <= 0.5;
      if(tool == "Brush" || tool == "Erase")
      {
        const float radius = std::clamp(mark["radius"].toDouble(.03), .001, 1.0) * edge;
        const float hardness = std::clamp(mark["hardness"].toDouble(.8), 0.0, 1.0);
        for(const auto &point : points)
        {
          const auto next = xy(point);
          mask.paintSegment(from.first, from.second, next.first, next.second, radius, hardness, erase ? 0.0f : 1.0f);
          from = next;
        }
        continue;
      }
      if(tool != "Gradient" && tool != "Ellipse") throw std::runtime_error{"Unknown mask tool"};
      const auto shape = tool == "Gradient"
        ? rawlab::pipeline::linearGradientMask(w, h, from.first, from.second, to.first, to.second)
        : rawlab::pipeline::ellipseMask(w, h, std::min(from.first, to.first), std::min(from.second, to.second),
                                        std::max(from.first, to.first), std::max(from.second, to.second));
      auto values = mask.values();
      for(std::size_t i = 0; i < values.size(); ++i)
        values[i] = erase ? std::max(0.0f, values[i] - shape.values()[i]) : std::max(values[i], shape.values()[i]);
      mask = {w, h, std::move(values)};
    }
    return mask;
  };
  auto mask=rawlab::pipeline::filledMask(w,h);
  if(layer.contains("depth"))
  {
    const auto depth = layer["depth"].toObject();
    const auto range = depth["range"].toArray();
    if(range.size() != 4) throw std::runtime_error{"Depth selection requires four range handles"};
    std::array<float, 4> bounds;
    for(int i = 0; i < 4; ++i)
    {
      if(!range[i].isDouble()) throw std::runtime_error{"Invalid depth range value"};
      bounds[i] = static_cast<float>(range[i].toDouble() / 100);
    }
    const auto map = depthMaps[depth["map"].toString()].toObject();
    mask = rawlab::pipeline::depthRangeMask(decodeDepthMap(map["png"]), w, h, bounds);
  }
  else if(!layer["aiMask"].toString().isEmpty())
  {
    const auto data=QByteArray::fromBase64(layer["aiMask"].toString().toLatin1());
    const auto image=QImage::fromData(data,"PNG").convertToFormat(QImage::Format_Grayscale8).scaled(w,h,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
    if(image.isNull())throw std::runtime_error{"Invalid mask PNG"};
    std::vector<float> values(w*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x)values[y*w+x]=image.constScanLine(y)[x]/255.0f;
    mask={w,h,std::move(values)};
  }
  const QJsonArray categoryMasks = layer["categoryMasks"].toArray();
  const QJsonArray categoryStrokes = layer["categoryStrokes"].toArray();
  bool hasCategoryData = false;
  for(int category = 0; category < 3; ++category)
    hasCategoryData = hasCategoryData
      || (category < categoryMasks.size() && !categoryMasks[category].toString().isEmpty())
      || (category < categoryStrokes.size() && !categoryStrokes[category].toArray().isEmpty());
  if(hasCategoryData && !layer.contains("depth"))
  {
    mask = emptyMask();
    for(int category = 0; category < 3; ++category)
    {
      const bool hasMask = category < categoryMasks.size() && !categoryMasks[category].toString().isEmpty();
      const bool hasStrokes = category < categoryStrokes.size() && !categoryStrokes[category].toArray().isEmpty();
      if(!hasMask && !hasStrokes) continue;
      auto contribution = hasMask
        ? decodeMaskPng(categoryMasks[category], w, h).value_or(emptyMask())
        : emptyMask();
      if(category < categoryStrokes.size()) contribution = applyOperations(std::move(contribution), categoryStrokes[category].toArray());
      auto values = mask.values();
      for(std::size_t i = 0; i < values.size(); ++i) values[i] = std::max(values[i], contribution.values()[i]);
      mask = {w, h, std::move(values)};
    }
  }
  else mask = applyOperations(std::move(mask), layer["marks"].toArray());
  const float edge = std::min(w, h);
  // Depth handles supply the fades; spatial feathering would threshold them away.
  if(!layer.contains("depth"))
    mask = rawlab::pipeline::featherMask(mask, static_cast<float>(layer["feather"].toDouble() / 1000.0) * edge);
  return rawlab::pipeline::blurMask(mask, static_cast<float>(layer["blur"].toDouble() / 1000.0) * edge);
}

inline PictureRecipe toQtRecipe(const QJsonObject &front,int width,int height)
{
  const auto layers=front["layers"].toArray();if(layers.empty())throw std::runtime_error{"Recipe has no Global layer"};
  PictureRecipe result;result.desktopEdits=front;result.previewWidth=width;result.previewHeight=height;
  result.sharpenModelId=front["sharpenModelId"].toString();result.preserveClipped=front["highlights"].toBool(true);
  const auto mode=[](QString value){return value=="AI"?SharpenMode::Ai:value=="Normal"?SharpenMode::Normal:SharpenMode::Off;};
  const auto g=layers[0].toObject();result.globalModelId=g["modelId"].toString();result.globalEnabled=g["enabled"].toBool(true);
  result.globalBlend=g["blend"].toDouble(100)/100;result.globalSharpenMode=mode(g["sharpen"].toString());result.globalSharpenEnabled=result.globalSharpenMode!=SharpenMode::Off;result.globalSharpenStrength=g["strength"].toDouble(100)/100;
  for(int i=1;i<layers.size();++i)
  {
    auto l=layers[i].toObject();PictureRecipeLayer layer;layer.id=l["id"].toInt(i);layer.name=l["name"].toString();layer.modelId=l["modelId"].toString();layer.enabled=l["enabled"].toBool(true);
    layer.blend=l["blend"].toDouble(100)/100;layer.sharpenMode=mode(l["sharpen"].toString());layer.sharpenEnabled=layer.sharpenMode!=SharpenMode::Off;layer.sharpenStrength=l["strength"].toDouble(100)/100;
    layer.inverted=l["inverted"].toBool();layer.showMask=l["showMask"].toBool(true);layer.feather=l["feather"].toDouble()/1000;layer.blur=l["blur"].toDouble()/1000;
    layer.baseMask = decodeMaskPng(l["aiMask"], width, height);
    if(l.contains("depth")) (void)desktopMask(l, 1, 1, front["depthMaps"].toObject());
    layer.strokes = readStrokes(l["strokes"].toArray());
    if(l.contains("marks"))
    {
      layer.strokes.clear();
      for(const auto &markValue : l["marks"].toArray())
      {
       const auto mark = markValue.toObject();
       PictureRecipeStroke stroke;
        stroke.tool = mark["tool"].toString("Brush");
         stroke.value = mark["tool"].toString() == "Erase" || mark["erase"].toBool() ? 0.0f : 1.0f;
        stroke.radius = static_cast<float>(std::clamp(mark["radius"].toDouble(.03), .001, 1.0));
        stroke.hardness = static_cast<float>(std::clamp(mark["hardness"].toDouble(.8), 0.0, 1.0));
        for(const auto &pointValue : mark["points"].toArray())
        {
          const auto point = pointValue.toArray();
          if(point.size() == 2) stroke.points.emplace_back(point[0].toDouble(), point[1].toDouble());
        }
         if(!stroke.points.empty()) layer.strokes.push_back(std::move(stroke));
       }
      }
    for(const auto &pointValue : l["points"].toArray())
    {
      const auto point = pointValue.toArray();
      if(point.size() == 3) layer.prompts.push_back({static_cast<float>(point[0].toDouble()), static_cast<float>(point[1].toDouble()), point[2].toBool(true)});
    }
    for(int category = 0; category < 3; ++category)
    {
      const auto masks = l["categoryMasks"].toArray();
      if(category < masks.size()) layer.categoryMasks[static_cast<std::size_t>(category)] = decodeMaskPng(masks[category], width, height);
      const auto strokes = l["categoryStrokes"].toArray();
      if(category < strokes.size()) layer.categoryStrokes[static_cast<std::size_t>(category)] = readStrokes(strokes[category].toArray());
    }
    result.layers.push_back(std::move(layer));
  }
  return result;
}

inline QJsonObject fromQtRecipe(const PictureRecipe &recipe)
{
  QJsonObject result = recipe.desktopEdits;
  const auto mode=[](SharpenMode mode){return mode==SharpenMode::Ai?"AI":mode==SharpenMode::Normal?"Normal":"Off";};
  QJsonArray layers;
  const QJsonArray existingLayers = result["layers"].toArray();
  QJsonObject global = existingLayers.isEmpty() ? QJsonObject{} : existingLayers.at(0).toObject();
  global.insert("id", 0); global.insert("name", "Global"); global.insert("modelId", recipe.globalModelId);
  global.insert("enabled", recipe.globalEnabled); global.insert("blend", recipe.globalBlend * 100);
  global.insert("sharpen", mode(recipe.globalSharpenMode));
  global.insert("strength", recipe.globalSharpenStrength * 100);
  global.insert("marks", QJsonArray{}); global.insert("points", QJsonArray{});
  layers.push_back(global);
  for(const auto &layer:recipe.layers)
  {
    QJsonObject l;
    for(const auto &existing : existingLayers)
      if(existing.toObject()["id"].toInt(-1) == layer.id) { l = existing.toObject(); break; }
    l.insert("id", layer.id); l.insert("name", layer.name); l.insert("modelId", layer.modelId);
    l.insert("enabled", layer.enabled); l.insert("blend", layer.blend * 100);
    l.insert("sharpen", mode(layer.sharpenMode)); l.insert("strength", layer.sharpenStrength * 100);
    l.insert("feather", layer.feather * 1000); l.insert("blur", layer.blur * 1000);
    l.insert("inverted", layer.inverted); l.insert("showMask", layer.showMask);
    if(layer.baseMask) l["aiMask"] = maskPng(*layer.baseMask);
    else l.remove("aiMask");
    l["strokes"] = strokeArray(layer.strokes);
    QJsonArray marks;
    for(const auto &stroke : layer.strokes)
    {
      QJsonArray points;
      for(const auto &point : stroke.points) points.push_back(QJsonArray{point.x(), point.y()});
       marks.push_back(QJsonObject{{"tool", stroke.tool.isEmpty() ? (stroke.value > 0.5f ? "Brush" : "Erase") : stroke.tool},
                                   {"erase", stroke.value <= 0.5f},
                                   {"radius", stroke.radius}, {"hardness", stroke.hardness}, {"points", points}});
    }
    l["marks"] = marks;
    QJsonArray prompts;
    for(const auto &prompt : layer.prompts)
      prompts.push_back(QJsonArray{prompt.x, prompt.y, prompt.foreground});
    l["points"] = prompts;
    QJsonArray categoryMasks, categoryStrokes;
    for(int category = 0; category < 3; ++category)
    {
      categoryMasks.push_back(layer.categoryMasks[static_cast<std::size_t>(category)]
        ? QJsonValue{maskPng(*layer.categoryMasks[static_cast<std::size_t>(category)])} : QJsonValue{});
      categoryStrokes.push_back(strokeArray(layer.categoryStrokes[static_cast<std::size_t>(category)]));
    }
    l["categoryMasks"] = categoryMasks;
    l["categoryStrokes"] = categoryStrokes;
    if(l.contains("depth")) (void)desktopMask(l, 1, 1, result["depthMaps"].toObject());
    layers.push_back(l);
  }
  result["layers"] = layers;
  result["highlights"] = recipe.preserveClipped;
  result["sharpenModelId"] = recipe.sharpenModelId;
  return result;
}
