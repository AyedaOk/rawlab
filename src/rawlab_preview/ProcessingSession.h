#pragma once
#include "ApplicationSettings.h"
#include "RecipeAdapter.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/ai/RawForgeAdapter.h"
#include "rawlab/image/RawPreview.h"
#include "rawlab/image/AmazeDemosaic.h"
#include "rawlab/image/LensCorrection.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/ai/Sam2Segmenter.h"
#include "rawlab/ai/DepthEstimator.h"
#include <QImage>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRect>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

// Persistent desktop processing state. Cached CFA stays in C++.
class ProcessingSession
{
  friend struct ProcessingSessionTest;
  QString sourceKey_;
  std::shared_ptr<rawlab::raw::RawImage> source_;
  std::deque<std::pair<QString, std::shared_ptr<rawlab::raw::RawImage>>> sources_;
  std::map<QString, std::shared_ptr<rawlab::raw::RawImage>> cache_;
  std::set<QString> cheapCache_;
  std::deque<QString> cacheOrder_;
  std::size_t cacheBytes_ = 0;
  std::map<QString, std::shared_ptr<rawlab::image::LinearRawPreview>> previewCache_;
  std::deque<QString> previewCacheOrder_;
  std::size_t previewCacheBytes_ = 0;
  struct ProcessedEntry
  {
    QString id;
    QString sourceKey;
    std::shared_ptr<rawlab::raw::RawImage> image;
  };
  std::deque<ProcessedEntry> processed_;
  std::shared_ptr<rawlab::ai::Sam2Segmenter> segmenter_;
  QString segmenterKey_;
  std::unique_ptr<rawlab::ai::DepthEstimator> depthEstimator_;
  QString depthModelKey_;
  std::deque<std::pair<QString, QJsonObject>> depthMaps_;
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
  void cacheImage(const QString &key, std::shared_ptr<rawlab::raw::RawImage> image, bool cheap = false)
  {
    constexpr std::size_t maxCacheBytes = 512ULL * 1024ULL * 1024ULL;
    if(const auto existing = cache_.find(key); existing != cache_.end())
      cacheBytes_ -= existing->second->samples().size() * sizeof(float);
    cacheBytes_ += image->samples().size() * sizeof(float);
    cache_[key] = std::move(image);
    if(cheap) cheapCache_.insert(key); else cheapCache_.erase(key);
    touchCache(key);
    while(cache_.size() > 1 && cacheBytes_ > maxCacheBytes)
    {
      const auto evictable = [&](const QString &candidate) {
        return candidate != key && cache_.at(candidate).use_count() == 1;
      };
      auto victim = std::find_if(cacheOrder_.begin(), cacheOrder_.end(), [&](const auto &candidate) {
        return cheapCache_.contains(candidate) && evictable(candidate);
      });
      if(victim == cacheOrder_.end()) victim = std::find_if(cacheOrder_.begin(), cacheOrder_.end(), evictable);
      // ponytail: in-use CFA may temporarily exceed the budget; trim on the next insertion.
      if(victim == cacheOrder_.end()) break;
      const QString oldest = *victim;
      cacheOrder_.erase(victim);
      const auto entry = cache_.find(oldest);
      if(entry == cache_.end()) continue;
      cacheBytes_ -= entry->second->samples().size() * sizeof(float);
      cache_.erase(entry);
      cheapCache_.erase(oldest);
    }
  }
  static std::shared_ptr<rawlab::raw::RawImage> crop(
    const std::shared_ptr<rawlab::raw::RawImage> &image, const QRect &region)
  {
    if(region.isNull()) return image;
    if(region.width() <= 0 || region.height() <= 0
       || !QRect{0, 0, image->width(), image->height()}.contains(region))
      throw std::invalid_argument{"Invalid CFA region"};
    auto metadata = image->metadata();
    for(int y = 0; y < 2; ++y) for(int x = 0; x < 2; ++x)
    {
      metadata.cfa.cells[y][x] = rawlab::raw::colorAt(image->metadata().cfa, x + region.x(), y + region.y());
      metadata.blackLevel[y * 2 + x] = image->metadata().blackLevel[
        rawlab::raw::siteIndex(x + region.x(), y + region.y())];
    }
    metadata.activeArea = {0, 0, region.width(), region.height()};
    std::vector<float> samples(static_cast<std::size_t>(region.width()) * region.height());
    for(int y = 0; y < region.height(); ++y)
      std::copy_n(image->samples().begin() + static_cast<std::size_t>(region.y() + y) * image->width() + region.x(),
        region.width(), samples.begin() + static_cast<std::size_t>(y) * region.width());
    return std::make_shared<rawlab::raw::RawImage>(region.width(), region.height(), std::move(samples), std::move(metadata));
  }
  void cachePreview(const QString &key, const std::shared_ptr<rawlab::image::LinearRawPreview> &preview)
  {
    previewCacheBytes_ += preview->pixels.size() * sizeof(float);
    previewCache_[key] = preview;
    previewCacheOrder_.push_back(key);
    constexpr std::size_t maxPreviewCacheBytes = 512ULL * 1024ULL * 1024ULL;
    while(previewCache_.size() > 3 && previewCacheBytes_ > maxPreviewCacheBytes)
    {
      const QString oldest = previewCacheOrder_.front();
      previewCacheOrder_.pop_front();
      const auto entry = previewCache_.find(oldest);
      if(entry == previewCache_.end()) continue;
      previewCacheBytes_ -= entry->second->pixels.size() * sizeof(float);
      previewCache_.erase(entry);
    }
  }
  std::shared_ptr<rawlab::image::LinearRawPreview> previewEndpoint(
    const QString &identity, const rawlab::raw::RawImage &image, const int size,
    const rawlab::image::PreviewQuality quality, const rawlab::raw::RawImage *highlightSource = nullptr)
  {
    const QString key = identity + ':' + QString::number(size) + ':'
      + (quality == rawlab::image::PreviewQuality::High ? QStringLiteral("high") : QStringLiteral("fast"))
      + (highlightSource ? QStringLiteral(":highlights") : QString{});
    if(const auto hit = previewCache_.find(key); hit != previewCache_.end())
    {
      std::erase(previewCacheOrder_, key);
      previewCacheOrder_.push_back(key);
      return hit->second;
    }
    auto preview = std::make_shared<rawlab::image::LinearRawPreview>(
      rawlab::image::renderLinearRawPreview(highlightSource
        ? rawlab::raw::preserveClippedHighlights(*highlightSource, image) : image, size, quality));
    cachePreview(key, preview);
    return preview;
  }
public:
  static QString sourceFingerprint(const QString &path) { return fingerprint(path); }
  std::shared_ptr<rawlab::raw::RawImage> source(const QString &path)
  {
    const auto key = fingerprint(path);
    if(key != sourceKey_ || !source_)
    {
      segmenter_.reset();
      const auto hit = std::find_if(sources_.begin(), sources_.end(), [&](const auto &entry) { return entry.first == key; });
      auto decoded = hit == sources_.end()
        ? std::make_shared<rawlab::raw::RawImage>(rawlab::raw::LibRawDecoder{}.decodeRawImage(path.toStdString())) : hit->second;
      if(hit != sources_.end()) sources_.erase(hit);
      source_ = std::move(decoded);
      sourceKey_ = key;
      sources_.emplace_back(key, source_);
      std::size_t bytes = 0;
      for(const auto &entry : sources_) bytes += entry.second->samples().size() * sizeof(float);
      while(sources_.size() > 1 && (sources_.size() > 2 || bytes > 256ULL * 1024ULL * 1024ULL))
      {
        bytes -= sources_.front().second->samples().size() * sizeof(float);
        sources_.pop_front();
      }
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
    processed_.push_back({id, capturedSourceKey, image});
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
  rawlab::image::LinearRawPreview preview(const QString &path, const QJsonObject &recipe,
                                          const QString &resultId,
                                          const rawlab::raw::RawImage &fallback, const int size,
                                          const rawlab::image::PreviewQuality quality,
                                          const std::function<void()> &checkCancelled = {})
  {
    if(checkCancelled) checkCancelled();
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
                             fallback, size, quality, checkCancelled);
      const int mw = result.width, mh = result.height;
      const auto geometry = rawlab::image::rawPreviewGeometry(*original, 1024);
      for(int i = layers.size() - 1; i > 0; --i)
      {
        auto layer = layers[i].toObject();
        if(!layer["enabled"].toBool(true)) continue;
        auto mask = desktopMask(layer, geometry.previewWidth, geometry.previewHeight, recipe["depthMaps"].toObject());
        QJsonObject localRecipe = recipe;
        localRecipe["layers"] = QJsonArray{layer};
        const auto local = preview(path, localRecipe,
          resultId + QStringLiteral(":local:") + QString::number(i), fallback, size, quality, checkCancelled);
        for(int y = 0; y < result.height; ++y)
          for(int x = 0; x < result.width; ++x)
          {
            float amount = mask.sampleNearestMapped(x, y, mw, mh);
            if(layer["inverted"].toBool()) amount = 1.0f - amount;
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
    const QString denoiseKey = sourceKey_ + ':' + fingerprint(model.path) + ':' + model.adapter + ':' + execution;
    const auto denoised = cache_.find(denoiseKey);
    if(denoised == cache_.end())
      throw std::runtime_error{"Denoising cache expired; press Update to process again"};

    const auto denoisedImage = denoised->second;
    touchCache(denoiseKey);
    const auto originalPreview = previewEndpoint(
      QStringLiteral("original:") + sourceKey_, *original, size, quality);
    if(checkCancelled) checkCancelled();
    const auto *highlightSource = recipe["highlights"].toBool(true) ? original.get() : nullptr;
    const auto denoisedPreview = previewEndpoint(
      QStringLiteral("denoised:") + denoiseKey, *denoisedImage, size, quality, highlightSource);
    if(checkCancelled) checkCancelled();
    QString mode = global["sharpen"].toString("Off");
    if(mode == "AI" && recipe["outputKind"].toString("cfa") != "linear-rgb") mode = "Off";
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
      if(mode == "Normal" && !cache_.contains(sharpenKey))
        cacheImage(sharpenKey, std::make_shared<rawlab::raw::RawImage>(
          rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(*denoisedImage, 1.0f,
            [&](int, int) { if(checkCancelled) checkCancelled(); })), true);
      if(mode == "AI" && !cache_.contains(sharpenKey))
      {
        if(sharpenPath.isEmpty() || !QFileInfo{sharpenPath}.isFile())
          throw std::runtime_error{"Selected AI sharpening model is missing"};
        rawlab::ai::OnnxSession session{sharpenPath.toStdString(), {settings.onnxProvider.toStdString(),
          settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()}};
        cacheImage(sharpenKey, std::make_shared<rawlab::raw::RawImage>(
          rawlab::ai::RawForgeAdapter{}.run(*denoisedImage, session,
            original->metadata().iso.value_or(100.0), false,
            [&](int, int) { if(checkCancelled) checkCancelled(); })));
      }
      const auto sharpened = cache_.find(sharpenKey);
      if(sharpened == cache_.end())
        throw std::runtime_error{"Sharpening cache expired; press Update to process again"};
      touchCache(sharpenKey);
      sharpenedPreview = previewEndpoint(
        QStringLiteral("sharpened:") + sharpenKey, *sharpened->second, size, quality, highlightSource);
      if(checkCancelled) checkCancelled();
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
      result.pixels[index] = originalPreview->pixels[index]
        + (value - originalPreview->pixels[index]) * blend;
    }
    return result;
  }
  rawlab::image::LinearRawPreview detail(const QString &path, const QJsonObject &recipe,
    const QRect &region, const rawlab::image::OpticsOptions &optics = {},
    const std::function<void()> &checkCancelled = {},
    const rawlab::image::PreviewQuality quality = rawlab::image::PreviewQuality::High)
  {
    if(checkCancelled) checkCancelled();
    const auto original = source(path);
    const auto geometry = rawlab::image::rawPreviewGeometry(*original, std::max(original->width(), original->height()));
    const int fullWidth = geometry.previewWidth, fullHeight = geometry.previewHeight;
    if(region.width() <= 0 || region.height() <= 0
       || static_cast<qint64>(region.width()) * region.height() > 16 * 1024 * 1024
       || !QRect{0, 0, fullWidth, fullHeight}.contains(region))
      throw std::invalid_argument{"Invalid detail region"};
    const QString identity = sourceKey_ + ':' + QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument{recipe}.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex())
      + ':' + QString::number(optics.chromaticAberration) + ':' + QString::number(optics.lensCorrection)
      + ':' + QString::number(static_cast<int>(quality));
    const auto settings = ApplicationSettingsStore{}.load();
    // Include model versions and execution settings so replacing a model cannot reuse old detail.
    QString execution = settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId) + ':' + settings.onnxRuntimeLibrary;
    for(const auto &model : settings.denoiseModels) execution += ':' + model.id + ':' + model.adapter + ':' + fingerprint(model.path);
    for(const auto &model : settings.sharpenModels) execution += ':' + model.id + ':' + fingerprint(model.path);
    rawlab::image::LinearRawPreview result{region.width(), region.height(),
      std::vector<float>(static_cast<std::size_t>(region.width()) * region.height() * 3), geometry};
    std::map<int, rawlab::pipeline::Mask> masks;
    const auto renderSource = [&](int x, int y, int width, int height) {
      if(checkCancelled) checkCancelled();
      const auto p0 = geometry.previewToRaw(x, y), p1 = geometry.previewToRaw(x + width, y + height);
      const QRect active{geometry.rawArea.left, geometry.rawArea.top, geometry.rawArea.width, geometry.rawArea.height};
      const int left = std::lround(std::min(p0.first, p1.first)), top = std::lround(std::min(p0.second, p1.second));
      const int right = std::lround(std::max(p0.first, p1.first)), bottom = std::lround(std::max(p0.second, p1.second));
      const auto working = rawlab::image::amazeWorkingArea(*original, {left, top, right - left, bottom - top});
      const QRect rawRegion = quality == rawlab::image::PreviewQuality::High
        ? QRect{working.left, working.top, working.width, working.height}
        : QRect{left - 2, top - 2, right - left + 4, bottom - top + 4}.intersected(active);
      const auto composed = process(path, recipe, [&](int, int) { if(checkCancelled) checkCancelled(); },
        &settings, false, [&](const QString &, int, int) { if(checkCancelled) checkCancelled(); }, rawRegion, &masks);
      const auto c0 = geometry.rawToPreview(rawRegion.x(), rawRegion.y());
      const auto c1 = geometry.rawToPreview(rawRegion.x() + rawRegion.width(), rawRegion.y() + rawRegion.height());
      auto linear = rawlab::image::renderLinearRawPreviewRegion(*composed,
        x - std::lround(std::min(c0.first, c1.first)), y - std::lround(std::min(c0.second, c1.second)), width, height, quality);
      return rawlab::image::LinearRgbImage{linear.width, linear.height, std::move(linear.pixels)};
    };
    constexpr int tileSize = 256;
    for(int y = region.y() / tileSize * tileSize; y <= region.bottom(); y += tileSize)
      for(int x = region.x() / tileSize * tileSize; x <= region.right(); x += tileSize)
      {
        if(checkCancelled) checkCancelled();
        const QRect tile{x, y, std::min(tileSize, fullWidth - x), std::min(tileSize, fullHeight - y)};
        const QString key = identity + execution + QStringLiteral(":detail:%1:%2").arg(x).arg(y);
        auto found = previewCache_.find(key);
        std::shared_ptr<rawlab::image::LinearRawPreview> pixels;
        if(found != previewCache_.end())
        {
          pixels = found->second;
          std::erase(previewCacheOrder_, key); previewCacheOrder_.push_back(key);
        }
        else
        {
          auto rgb = rawlab::image::renderOpticsRegion(fullWidth, fullHeight, x, y, tile.width(), tile.height(),
            original->metadata(), optics, renderSource, checkCancelled);
          pixels = std::make_shared<rawlab::image::LinearRawPreview>(
            rawlab::image::LinearRawPreview{rgb.width, rgb.height, std::move(rgb.pixels), geometry});
          cachePreview(key, pixels);
        }
        const QRect overlap = tile.intersected(region);
        for(int row = overlap.y(); row <= overlap.bottom(); ++row)
          std::copy_n(pixels->pixels.begin() + ((row - y) * tile.width() + overlap.x() - x) * 3,
            overlap.width() * 3, result.pixels.begin() + ((row - region.y()) * region.width() + overlap.x() - region.x()) * 3);
      }
    return result;
  }
  static QJsonObject models()
  {
    const auto settings = ApplicationSettingsStore{}.load();
    QJsonArray denoise, sharpen, depth;
    for(const auto &m : settings.denoiseModels) denoise.push_back(QJsonObject{{"id", m.id}, {"name", m.name}, {"path", m.path}, {"origin", m.origin}, {"available", QFileInfo{m.path}.isFile()}});
    for(const auto &m : settings.sharpenModels) sharpen.push_back(QJsonObject{{"id", m.id}, {"name", m.name}, {"path", m.path}, {"origin", m.origin}, {"available", QFileInfo{m.path}.isFile()}});
    for(const auto &m : settings.depthModels) depth.push_back(QJsonObject{{"id", m.id}, {"name", m.name}, {"path", m.path}, {"origin", m.origin}, {"available", QFileInfo{m.path}.isFile()}});
    return {{"denoise", denoise}, {"sharpen", sharpen}, {"depth", depth},
      {"activeDepthModelId", settings.activeDepthModelId}, {"provider", settings.onnxProvider}};
  }
  QJsonObject depth(const QString &path, const std::function<void()> &checkCancelled = {})
  {
    if(checkCancelled) checkCancelled();
    const auto original = source(path);
    const auto settings = ApplicationSettingsStore{}.load();
    const auto found = std::find_if(settings.depthModels.begin(), settings.depthModels.end(),
      [&](const auto &model) { return model.id == settings.activeDepthModelId; });
    if(found == settings.depthModels.end() || !QFileInfo{found->path}.isFile())
      throw std::runtime_error{"Configure a Depth Anything V2 model in Preferences first"};
    if(found->adapter != "depth-anything-v2-v1") throw std::runtime_error{"Unsupported depth model adapter"};
    const auto modelKey = fingerprint(found->path) + ':' + fingerprint(found->path + "_data")
      + ':' + settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId) + ':' + settings.onnxRuntimeLibrary;
    const auto key = QString::fromLatin1(QCryptographicHash::hash(
      (sourceKey_ + ':' + modelKey).toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto hit = std::find_if(depthMaps_.begin(), depthMaps_.end(), [&](const auto &entry) { return entry.first == key; });
    if(hit != depthMaps_.end())
    {
      const auto entry = *hit; depthMaps_.erase(hit); depthMaps_.push_back(entry);
      return entry.second;
    }
    if(!depthEstimator_ || depthModelKey_ != modelKey)
    {
      depthEstimator_.reset();
      depthEstimator_ = std::make_unique<rawlab::ai::DepthEstimator>(found->path.toStdString(),
        rawlab::ai::OnnxSessionOptions{settings.onnxProvider.toStdString(), settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()});
      depthModelKey_ = modelKey;
    }
    if(checkCancelled) checkCancelled();
    const auto preview = rawlab::image::renderRawPreview(*original, 1024);
    const auto map = depthEstimator_->estimate(preview.image);
    if(checkCancelled) checkCancelled();
    const QJsonObject result{{"key", key}, {"png", depthMapPng(map)}, {"modelId", found->id}};
    depthMaps_.emplace_back(key, result);
    while(depthMaps_.size() > 4) depthMaps_.pop_front();
    return result;
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
                                                   const std::function<void(const QString &, int, int)> &stageProgress = {},
                                                   const QRect &rawRegion = {},
                                                   std::map<int, rawlab::pipeline::Mask> *maskCache = nullptr)
  {
    const auto original = source(path);
    const auto layers = recipe["layers"].toArray();
    if(layers.isEmpty()) throw std::runtime_error{"Recipe requires a Global layer"};
    if(layers.size() > 1)
    {
      QJsonObject globalRecipe = recipe; globalRecipe["layers"] = QJsonArray{layers[0]};
       auto composed = process(path, globalRecipe, progress, settingsSnapshot, allowInference, stageProgress, rawRegion);
      auto geometry = rawlab::image::rawPreviewGeometry(*original, 1024);
      const int w = geometry.previewWidth, h = geometry.previewHeight;
      if(!rawRegion.isNull()) { geometry.rawArea.left -= rawRegion.x(); geometry.rawArea.top -= rawRegion.y(); }
      std::map<int, rawlab::pipeline::Mask> localMasks;
      if(!maskCache) maskCache = &localMasks;
      // Compose bottom to top. Each local blends with the source before covering lower layers.
      for(int i = layers.size() - 1; i > 0; --i)
      {
        auto layer = layers[i].toObject(); if(!layer["enabled"].toBool(true)) continue;
        if(!maskCache->contains(i))
          maskCache->emplace(i, desktopMask(layer, w, h, recipe["depthMaps"].toObject()));
        const auto &mask = maskCache->at(i);
        auto localRecipe=recipe; localRecipe["layers"]=QJsonArray{layer};
         const auto local=process(path,localRecipe,progress,settingsSnapshot,allowInference,stageProgress,rawRegion);
        composed=std::make_shared<rawlab::raw::RawImage>(rawlab::pipeline::blendWithPreviewMask(*composed,*local,mask,geometry,1.0f,layer["inverted"].toBool()));
      }
      return composed;
    }
    const auto global = layers[0].toObject();
    const auto originalRegion = crop(original, rawRegion);
    if(!global["enabled"].toBool(true)) return originalRegion;
    const auto settings = settingsSnapshot ? *settingsSnapshot : ApplicationSettingsStore{}.load();
    const QString id = global["modelId"].toString();
    const auto model = ApplicationSettingsStore::modelById(settings, id);
    if(model.path.isEmpty() || !QFileInfo{model.path}.isFile()) throw std::runtime_error{"Selected denoise model is missing. Configure or install it in RawLab Preferences."};
    const QString execution = settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId) + ':' + settings.onnxRuntimeLibrary;
    const QString key = sourceKey_ + ':' + fingerprint(model.path) + ':' + model.adapter + ':' + execution;
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
    const auto denoised = cache_.at(key);
    auto processed = crop(denoised, rawRegion);
     QString mode = global["sharpen"].toString("Off");
     if(mode == "AI" && recipe["outputKind"].toString("cfa") != "linear-rgb") mode = "Off";
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
          if(!allowInference && rawRegion.isNull() && mode != "Normal") throw std::runtime_error{"Sharpening cache expired; press Update to process again"};
        rawlab::raw::RawImage sharpened;
         if(mode == "Normal") sharpened = rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(*denoised, 1.0f,
              [&](int done, int total) { if(stageProgress) stageProgress("Sharpening", done, total); else if(progress) progress(done, total); });
        else
        {
          rawlab::ai::OnnxSession session{sharpenPath.toStdString(), {settings.onnxProvider.toStdString(), settings.onnxDeviceId, settings.onnxRuntimeLibrary.toStdString()}};
           sharpened = rawlab::ai::RawForgeAdapter{}.run(*denoised, session, original->metadata().iso.value_or(100.0), false,
              [&](int done, int total) { if(stageProgress) stageProgress("Sharpening", done, total); else if(progress) progress(done, total); });
        }
        cacheImage(sharpenKey, std::make_shared<rawlab::raw::RawImage>(std::move(sharpened)), mode == "Normal");
      }
      else touchCache(sharpenKey);
      processed = std::make_shared<rawlab::raw::RawImage>(rawlab::raw::blendWithOriginal(*processed, *crop(cache_.at(sharpenKey), rawRegion), unit(global["strength"], 30)));
    }
     if(stageProgress) stageProgress("Composing preview", 1, 1);
     if(recipe["highlights"].toBool(true)) processed = std::make_shared<rawlab::raw::RawImage>(rawlab::raw::preserveClippedHighlights(*originalRegion, *processed));
    return std::make_shared<rawlab::raw::RawImage>(rawlab::raw::blendWithOriginal(*originalRegion, *processed, unit(global["blend"], 100)));
  }
};
