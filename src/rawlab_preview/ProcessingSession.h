#pragma once
#include "ApplicationSettings.h"
#include "RecipeAdapter.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/ai/RawForgeAdapter.h"
#include "rawlab/image/RawPreview.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/ai/Sam2Segmenter.h"
#include <QImage>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

// One persistent desktop document. Cached CFA stays in C++, not in JavaScript.
class ProcessingSession
{
  QString sourceKey_;
  std::shared_ptr<rawlab::raw::RawImage> source_;
  std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> cache_;
  std::deque<QString> cacheOrder_;
  std::size_t cacheBytes_ = 0;
  std::map<QString, std::shared_ptr<rawlab::image::LinearRawPreview>> previewCache_;
  std::deque<QString> previewCacheOrder_;
  std::size_t previewCacheBytes_ = 0;
  struct ProcessedEntry
  {
    QString id;
    QString sourceKey;
    QString recipeKey;
    std::shared_ptr<rawlab::raw::RawImage> image;
  };
  std::deque<ProcessedEntry> processed_;
  std::shared_ptr<rawlab::ai::Sam2Segmenter> segmenter_;
  QString segmenterKey_;
  static QString fingerprint(const QString &path)
  {
    const QFileInfo file{path};
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path.toStdString(), error);
    const qint64 stamp = error ? static_cast<qint64>(file.lastModified().toMSecsSinceEpoch())
      : static_cast<qint64>(modified.time_since_epoch().count());
    QString identity = file.absoluteFilePath() + ':' + QString::number(file.size()) + ':' + QString::number(stamp);
#ifdef Q_OS_UNIX
    struct stat status {};
    if(::stat(QFile::encodeName(file.absoluteFilePath()).constData(), &status) == 0)
    {
#if defined(__APPLE__)
      identity += ':' + QString::number(status.st_dev) + ':' + QString::number(status.st_ino)
        + ':' + QString::number(status.st_ctimespec.tv_sec) + ':' + QString::number(status.st_ctimespec.tv_nsec);
#else
      identity += ':' + QString::number(status.st_dev) + ':' + QString::number(status.st_ino)
        + ':' + QString::number(status.st_ctim.tv_sec) + ':' + QString::number(status.st_ctim.tv_nsec);
#endif
    }
#endif
    return identity;
  }
  void touchCache(const QString &key)
  {
    std::erase(cacheOrder_, key);
    cacheOrder_.push_back(key);
  }
  void cacheImage(const QString &key, std::shared_ptr<rawlab::raw::RawImage> image)
  {
    constexpr std::size_t maxCacheBytes = 512ULL * 1024ULL * 1024ULL;
    if(const auto existing = cache_.find(key); existing != cache_.end())
      cacheBytes_ -= existing->second->samples().size() * sizeof(float);
    cacheBytes_ += image->samples().size() * sizeof(float);
    cache_[key] = std::move(image);
    touchCache(key);
    while(cache_.size() > 1 && (cache_.size() > 3 || cacheBytes_ > maxCacheBytes))
    {
      const QString oldest = cacheOrder_.front();
      cacheOrder_.pop_front();
      const auto entry = cache_.find(oldest);
      if(entry == cache_.end()) continue;
      cacheBytes_ -= entry->second->samples().size() * sizeof(float);
      cache_.erase(entry);
    }
  }
  std::shared_ptr<rawlab::image::LinearRawPreview> previewEndpoint(
    const QString &identity, const rawlab::raw::RawImage &image, const int size,
    const rawlab::image::PreviewQuality quality)
  {
    const QString key = identity + ':' + QString::number(size) + ':'
      + (quality == rawlab::image::PreviewQuality::High ? QStringLiteral("high") : QStringLiteral("fast"));
    if(const auto hit = previewCache_.find(key); hit != previewCache_.end())
    {
      std::erase(previewCacheOrder_, key);
      previewCacheOrder_.push_back(key);
      return hit->second;
    }
    auto preview = std::make_shared<rawlab::image::LinearRawPreview>(
      rawlab::image::renderLinearRawPreview(image, size, quality));
    previewCacheBytes_ += preview->pixels.size() * sizeof(float);
    previewCache_[key] = preview;
    previewCacheOrder_.push_back(key);
    constexpr std::size_t maxPreviewCacheBytes = 256ULL * 1024ULL * 1024ULL;
    while(previewCache_.size() > 1 && previewCacheBytes_ > maxPreviewCacheBytes)
    {
      const QString oldest = previewCacheOrder_.front();
      previewCacheOrder_.pop_front();
      const auto entry = previewCache_.find(oldest);
      if(entry == previewCache_.end()) continue;
      previewCacheBytes_ -= entry->second->pixels.size() * sizeof(float);
      previewCache_.erase(entry);
    }
    return preview;
  }
public:
  static QString sourceFingerprint(const QString &path) { return fingerprint(path); }
  std::shared_ptr<rawlab::raw::RawImage> source(const QString &path)
  {
    const auto key = fingerprint(path);
    if(key != sourceKey_ || !source_)
    {
      cache_.clear();
      cacheOrder_.clear();
      cacheBytes_ = 0;
      previewCache_.clear();
      previewCacheOrder_.clear();
      previewCacheBytes_ = 0;
      segmenter_.reset();
      source_ = std::make_shared<rawlab::raw::RawImage>(rawlab::raw::LibRawDecoder{}.decodeRawImage(path.toStdString()));
      sourceKey_ = key;
    }
    return source_;
  }
  QString rememberProcessed(const QString &path, const std::shared_ptr<rawlab::raw::RawImage> &image,
                             const QString &recipeKey = {}, const QString &sourceKey = {})
  {
    const QString capturedSourceKey = sourceKey.isEmpty() ? fingerprint(path) : sourceKey;
    const QString id = QString::fromLatin1(QCryptographicHash::hash(
      (capturedSourceKey + ':' + recipeKey + ':' + QUuid::createUuid().toString(QUuid::Id128)).toUtf8(),
      QCryptographicHash::Sha256).toHex());
    std::erase_if(processed_, [&id](const auto &entry) { return entry.id == id; });
    processed_.push_back({id, capturedSourceKey, recipeKey, image});
    while(processed_.size() > 2) processed_.pop_front();
    return id;
  }
  std::shared_ptr<rawlab::raw::RawImage> processed(const QString &path, const QString &resultId = {})
  {
    const QString sourceKey = fingerprint(path);
    const auto match = std::find_if(processed_.rbegin(), processed_.rend(), [&](const auto &entry) {
      return entry.sourceKey == sourceKey && (resultId.isEmpty() || entry.id == resultId);
    });
    if(match == processed_.rend()) throw std::runtime_error{"Processed result is stale or was evicted"};
    const ProcessedEntry entry = *match;
    processed_.erase(std::next(match).base());
    processed_.push_back(entry);
    return entry.image;
  }
  bool processedMatches(const QString &path, const QString &resultId, const QString &recipeKey) const
  {
    const QString sourceKey = fingerprint(path);
    const auto match = std::find_if(processed_.rbegin(), processed_.rend(), [&](const auto &entry) {
      return entry.sourceKey == sourceKey && entry.id == resultId;
    });
    return match != processed_.rend() && match->recipeKey == recipeKey;
  }
  void replaceProcessed(const QString &path, const QString &resultId, const QString &recipeKey,
                        const std::shared_ptr<rawlab::raw::RawImage> &image)
  {
    const QString sourceKey = fingerprint(path);
    const auto match = std::find_if(processed_.begin(), processed_.end(), [&](const auto &entry) {
      return entry.sourceKey == sourceKey && entry.id == resultId;
    });
    if(match == processed_.end()) throw std::runtime_error{"Processed result is stale or was evicted"};
    match->recipeKey = recipeKey;
    match->image = image;
  }
  rawlab::image::LinearRawPreview preview(const QString &path, const QJsonObject &recipe,
                                          const QString &resultId,
                                          const rawlab::raw::RawImage &fallback, const int size,
                                          const rawlab::image::PreviewQuality quality)
  {
    const auto original = source(path);
    const auto fallbackPreview = [&] {
      return *previewEndpoint(QStringLiteral("result:") + resultId, fallback, size, quality);
    };
    const auto layers = recipe["layers"].toArray();
    if(layers.size() > 1)
    {
      QJsonObject globalRecipe = recipe;
      globalRecipe["layers"] = QJsonArray{layers[0]};
      auto result = preview(path, globalRecipe, resultId + QStringLiteral(":global"),
                            fallback, size, quality);
      const float edge = static_cast<float>(std::min(result.width, result.height));
      for(int i = layers.size() - 1; i > 0; --i)
      {
        auto layer = layers[i].toObject();
        if(!layer["enabled"].toBool(true)) continue;
        const float layerBlend = static_cast<float>(
          std::clamp(layer["blend"].toDouble(100.0), 0.0, 100.0) / 100.0);
        auto mask = desktopMask(layer, result.width, result.height);
        mask = rawlab::pipeline::blurMask(rawlab::pipeline::featherMask(
          mask, static_cast<float>(layer["feather"].toDouble() / 1000.0) * edge),
          static_cast<float>(layer["blur"].toDouble() / 1000.0) * edge);
        layer["blend"] = 100;
        QJsonObject localRecipe = recipe;
        localRecipe["layers"] = QJsonArray{layer};
        const auto local = preview(path, localRecipe,
          resultId + QStringLiteral(":local:") + QString::number(i), fallback, size, quality);
        for(int y = 0; y < result.height; ++y)
          for(int x = 0; x < result.width; ++x)
          {
            float amount = mask.sampleNearest(x, y);
            if(layer["inverted"].toBool()) amount = 1.0f - amount;
            amount *= layerBlend;
            const std::size_t pixel = (static_cast<std::size_t>(y) * result.width + x) * 3U;
            for(std::size_t channel = 0; channel < 3; ++channel)
              result.pixels[pixel + channel] += (local.pixels[pixel + channel]
                - result.pixels[pixel + channel]) * amount;
          }
      }
      return result;
    }
    if(layers.isEmpty()) return fallbackPreview();
    const auto global = layers[0].toObject();
    if(!global["enabled"].toBool(true))
      return *previewEndpoint(QStringLiteral("original:") + sourceKey_, *original, size, quality);

    const auto settings = ApplicationSettingsStore{}.load();
    const auto model = ApplicationSettingsStore::modelById(settings, global["modelId"].toString());
    if(model.path.isEmpty() || !QFileInfo{model.path}.isFile())
      throw std::runtime_error{"Selected denoise model is missing; press Update after configuring it"};
    const QString execution = settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId)
      + ':' + settings.onnxRuntimeLibrary;
    const QString denoiseKey = fingerprint(model.path) + ':' + model.adapter + ':' + execution;
    const auto denoised = cache_.find(denoiseKey);
    if(denoised == cache_.end())
      throw std::runtime_error{"Denoising cache expired; press Update to process again"};

    const auto originalPreview = previewEndpoint(
      QStringLiteral("original:") + sourceKey_, *original, size, quality);
    const auto denoisedPreview = previewEndpoint(
      QStringLiteral("denoised:") + denoiseKey, *denoised->second, size, quality);
    const QString mode = recipe["outputKind"].toString("cfa") == "linear-rgb"
      ? global["sharpen"].toString("Off") : QStringLiteral("Off");
    std::shared_ptr<rawlab::image::LinearRawPreview> sharpenedPreview;
    if(mode != "Off")
    {
      QString sharpenPath;
      if(mode == "AI")
      {
        const QString sharpenId = recipe["sharpenModelId"].toString();
        if(sharpenId.isEmpty() && !settings.sharpenModels.isEmpty()) sharpenPath = settings.sharpenModels.front().path;
        else for(const auto &candidate : settings.sharpenModels) if(candidate.id == sharpenId) sharpenPath = candidate.path;
      }
      const QString sharpenKey = denoiseKey + ':' + mode + ':'
        + (sharpenPath.isEmpty() ? QStringLiteral("normal") : fingerprint(sharpenPath));
      const auto sharpened = cache_.find(sharpenKey);
      if(sharpened == cache_.end())
        throw std::runtime_error{"Sharpening cache expired; press Update to process again"};
      sharpenedPreview = previewEndpoint(
        QStringLiteral("sharpened:") + sharpenKey, *sharpened->second, size, quality);
    }

    rawlab::image::LinearRawPreview result = *denoisedPreview;
    const auto fraction = [](const QJsonValue value, const double fallbackValue) {
      return static_cast<float>(std::clamp(value.toDouble(fallbackValue), 0.0, 100.0) / 100.0);
    };
    const float strength = fraction(global["strength"], 30.0);
    const float blend = fraction(global["blend"], 100.0);
    for(std::size_t index = 0; index < result.pixels.size(); ++index)
    {
      float value = denoisedPreview->pixels[index];
      if(sharpenedPreview) value += (sharpenedPreview->pixels[index] - value) * strength;
      if(recipe["highlights"].toBool(true))
      {
        const std::size_t pixel = index - index % 3U;
        const float peak = std::max({originalPreview->pixels[pixel], originalPreview->pixels[pixel + 1],
                                     originalPreview->pixels[pixel + 2]});
        if(peak >= 0.98f) value = originalPreview->pixels[index];
      }
      result.pixels[index] = originalPreview->pixels[index]
        + (value - originalPreview->pixels[index]) * blend;
    }
    return result;
  }
  static QJsonObject models()
  {
    const auto settings = ApplicationSettingsStore{}.load();
    QJsonArray denoise, sharpen;
    for(const auto &m : settings.denoiseModels) denoise.push_back(QJsonObject{{"id", m.id}, {"name", m.name}, {"available", QFileInfo{m.path}.isFile()}});
    for(const auto &m : settings.sharpenModels) sharpen.push_back(QJsonObject{{"id", m.id}, {"name", m.name}, {"available", QFileInfo{m.path}.isFile()}});
    return {{"denoise", denoise}, {"sharpen", sharpen}, {"provider", settings.onnxProvider}};
  }
  rawlab::pipeline::Mask segment(const QString &path, const QJsonArray &points)
  {
    const auto original = source(path);
    const auto settings = ApplicationSettingsStore{}.load();
    const auto model = ApplicationSettingsStore::samModelById(settings, settings.activeSamModelId);
    const auto encoder = QDir{model.folder}.filePath("encoder.onnx"), decoder = QDir{model.folder}.filePath("decoder.onnx");
    if(model.folder.isEmpty() || !QFileInfo{encoder}.isFile() || !QFileInfo{decoder}.isFile()) throw std::runtime_error{"Configure and install a SAM model in RawLab Settings first"};
    const auto preview = rawlab::image::renderRawPreview(*original, 1024);
    const auto key = fingerprint(encoder) + fingerprint(decoder) + settings.onnxProvider + QString::number(settings.onnxDeviceId) + settings.onnxRuntimeLibrary;
    if(!segmenter_ || key != segmenterKey_)
    {
      segmenter_ = std::make_shared<rawlab::ai::Sam2Segmenter>(encoder.toStdString(), decoder.toStdString(), rawlab::ai::OnnxSessionOptions{settings.onnxProvider.toStdString(), settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()});
      segmenter_->encode(preview.image); segmenterKey_ = key;
    }
    std::vector<rawlab::ai::SamPointPrompt> prompts;
    for(const auto &value : points) {const auto point = value.toArray(); prompts.push_back({static_cast<float>(std::clamp(point[0].toDouble(),0.0,0.999999)*preview.image.width), static_cast<float>(std::clamp(point[1].toDouble(),0.0,0.999999)*preview.image.height), point.size()<3 || point[2].toBool(true)});}
    if(prompts.empty()) throw std::runtime_error{"Add a SAM point first"};
    segmenter_->resetRefinement();
    return segmenter_->segment(prompts);
  }
  std::shared_ptr<rawlab::raw::RawImage> process(const QString &path, const QJsonObject &recipe,
                                                  const rawlab::pipeline::RawProcessingPipeline::ProgressCallback &progress,
                                                  const ApplicationSettingsData *settingsSnapshot = nullptr,
                                                  const bool allowInference = true,
                                                  const std::function<void(const QString &, int, int)> &stageProgress = {})
  {
    const auto original = source(path);
    const auto layers = recipe["layers"].toArray();
    if(layers.isEmpty()) throw std::runtime_error{"Recipe requires a Global layer"};
    if(layers.size() > 1)
    {
      QJsonObject globalRecipe = recipe; globalRecipe["layers"] = QJsonArray{layers[0]};
       auto composed = process(path, globalRecipe, progress, settingsSnapshot, allowInference, stageProgress);
      const auto preview = rawlab::image::renderRawPreview(*original, 1024);
      const int w = preview.image.width, h = preview.image.height;
      const float edge = std::min(w, h);
      // Qt displays highest-priority local masks first; compose bottom to top.
      for(int i = layers.size() - 1; i > 0; --i)
      {
        auto layer = layers[i].toObject(); if(!layer["enabled"].toBool(true)) continue;
        const float blend = static_cast<float>(std::clamp(layer["blend"].toDouble(100),0.0,100.0)/100);
        auto mask = desktopMask(layer,w,h);
        mask=rawlab::pipeline::blurMask(rawlab::pipeline::featherMask(mask,static_cast<float>(layer["feather"].toDouble()/1000)*edge),static_cast<float>(layer["blur"].toDouble()/1000)*edge);
        layer["blend"]=100;
        auto localRecipe=recipe; localRecipe["layers"]=QJsonArray{layer};
         const auto local=process(path,localRecipe,progress,settingsSnapshot,allowInference,stageProgress);
        composed=std::make_shared<rawlab::raw::RawImage>(rawlab::pipeline::blendWithPreviewMask(*composed,*local,mask,preview.geometry,blend,layer["inverted"].toBool()));
      }
      return composed;
    }
    const auto global = layers[0].toObject();
    if(!global["enabled"].toBool(true)) return original;
    const auto settings = settingsSnapshot ? *settingsSnapshot : ApplicationSettingsStore{}.load();
    const QString id = global["modelId"].toString();
    const auto model = ApplicationSettingsStore::modelById(settings, id);
    if(model.path.isEmpty() || !QFileInfo{model.path}.isFile()) throw std::runtime_error{"Selected denoise model is missing. Configure or install it in RawLab Preferences."};
    const QString execution = settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId) + ':' + settings.onnxRuntimeLibrary;
    const QString key = fingerprint(model.path) + ':' + model.adapter + ':' + execution;
     if(!cache_.contains(key))
     {
       if(!allowInference) throw std::runtime_error{"Preview cache expired; press Update to process again"};
      rawlab::ai::OnnxSession session{model.path.toStdString(), {settings.onnxProvider.toStdString(), settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()}};
      auto denoised = model.adapter == "rawforge-rgb-v1"
         ? rawlab::ai::RawForgeAdapter{}.run(*original, session, original->metadata().iso.value_or(100.0), true,
              [&](int done, int total) { if(stageProgress) stageProgress("Denoising", done, total); else if(progress) progress(done, total); })
         : rawlab::pipeline::RawProcessingPipeline{}.runRawNindDenoise(*original, session,
              [&](int done, int total) { if(stageProgress) stageProgress("Denoising", done, total); else if(progress) progress(done, total); });
      cacheImage(key, std::make_shared<rawlab::raw::RawImage>(std::move(denoised)));
    }
    else touchCache(key);
    auto processed = cache_.at(key);
     const QString mode = recipe["outputKind"].toString("cfa") == "linear-rgb"
       ? global["sharpen"].toString("Off") : QStringLiteral("Off");
    const auto unit = [](QJsonValue v, double fallback) {
      const double value = v.toDouble(fallback);
      if(!std::isfinite(value) || value < 0 || value > 100) throw std::runtime_error{"Strength must be between 0 and 100"};
      return static_cast<float>(value / 100.0);
    };
    if(mode != "Off")
    {
      QString sharpenPath;
      if(mode == "AI")
      {
        const QString sharpenId = recipe["sharpenModelId"].toString();
        if(sharpenId.isEmpty() && !settings.sharpenModels.isEmpty()) sharpenPath = settings.sharpenModels.front().path;
        else for(const auto &m : settings.sharpenModels) if(m.id == sharpenId) sharpenPath = m.path;
        if(sharpenPath.isEmpty() || !QFileInfo{sharpenPath}.isFile()) throw std::runtime_error{"Selected AI sharpening model is missing"};
      }
      else if(mode != "Normal") throw std::runtime_error{"Unknown sharpening mode"};
      const QString sharpenKey = key + ':' + mode + ':' + (sharpenPath.isEmpty() ? "normal" : fingerprint(sharpenPath));
       if(!cache_.contains(sharpenKey))
       {
         if(!allowInference) throw std::runtime_error{"Sharpening cache expired; press Update to process again"};
        rawlab::raw::RawImage sharpened;
         if(mode == "Normal") sharpened = rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(*processed, 1.0f,
              [&](int done, int total) { if(stageProgress) stageProgress("Sharpening", done, total); else if(progress) progress(done, total); });
        else
        {
          rawlab::ai::OnnxSession session{sharpenPath.toStdString(), {settings.onnxProvider.toStdString(), settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()}};
           sharpened = rawlab::ai::RawForgeAdapter{}.run(*processed, session, original->metadata().iso.value_or(100.0), false,
              [&](int done, int total) { if(stageProgress) stageProgress("Sharpening", done, total); else if(progress) progress(done, total); });
        }
        cacheImage(sharpenKey, std::make_shared<rawlab::raw::RawImage>(std::move(sharpened)));
      }
      else touchCache(sharpenKey);
      processed = std::make_shared<rawlab::raw::RawImage>(rawlab::raw::blendWithOriginal(*processed, *cache_.at(sharpenKey), unit(global["strength"], 30)));
    }
     if(stageProgress) stageProgress("Composing preview", 1, 1);
     if(recipe["highlights"].toBool(true)) processed = std::make_shared<rawlab::raw::RawImage>(rawlab::raw::preserveClippedHighlights(*original, *processed));
    return std::make_shared<rawlab::raw::RawImage>(rawlab::raw::blendWithOriginal(*original, *processed, unit(global["blend"], 100)));
  }
};
