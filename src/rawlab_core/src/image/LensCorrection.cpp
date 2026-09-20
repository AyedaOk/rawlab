#include "rawlab/image/LensCorrection.h"

#include "rawlab/image/AmazeDemosaic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

#ifdef RAWLAB_HAS_LENSFUN
#include <lensfun/lensfun.h>
#include <QCoreApplication>
#include <QDir>
#endif

namespace rawlab::image {
namespace {

LinearRgbImage baseLinearRgb(const raw::RawImage &image)
{
  auto area = image.metadata().activeArea;
  if(area.width <= 0 || area.height <= 0) area = {0, 0, image.width(), image.height()};
  const auto demosaiced = demosaicAmaze(image, area);
  LinearRgbImage result{area.width, area.height, {}};
  result.pixels.reserve(demosaiced.size() * 3U);
  const auto &matrix = image.metadata().cameraToSrgb;
  for(const auto &camera : demosaiced)
  {
    for(int row = 0; row < 3; ++row)
    {
      const float value = matrix[static_cast<std::size_t>(row * 3)] * camera[0]
        + matrix[static_cast<std::size_t>(row * 3 + 1)] * camera[1]
        + matrix[static_cast<std::size_t>(row * 3 + 2)] * camera[2];
      if(!std::isfinite(value)) throw std::invalid_argument{"Linear RGB contains a non-finite sample"};
      result.pixels.push_back(value);
    }
  }
  return result;
}

#ifdef RAWLAB_HAS_LENSFUN
float bilinear(const LinearRgbImage &image, const float x, const float y, const int channel)
{
  if(x < 0.0f || y < 0.0f || x > image.width - 1.0f || y > image.height - 1.0f) return 0.0f;
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1), y1 = std::min(y0 + 1, image.height - 1);
  const float fx = x - x0, fy = y - y0;
  const auto at = [&](int px, int py) { return image.pixels[(static_cast<std::size_t>(py) * image.width + px) * 3U + channel]; };
  return (at(x0,y0)*(1-fx)+at(x1,y0)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy;
}

struct Match
{
  std::unique_ptr<lfDatabase> database{std::make_unique<lfDatabase>()};
  const lfCamera *camera = nullptr;
  const lfLens *lens = nullptr;
};

std::shared_ptr<Match> match(const raw::RawMetadata &metadata)
{
  const QString configured = qEnvironmentVariable("RAWLAB_LENSFUN_DB");
  const QString key = QString::fromStdString(metadata.make) + '\n'
    + QString::fromStdString(metadata.model) + '\n' + QString::fromStdString(metadata.lensMake)
    + '\n' + QString::fromStdString(metadata.lensModel) + '\n' + configured;
  static std::mutex cacheMutex;
  static std::map<QString, std::shared_ptr<Match>> cache;
  const std::scoped_lock lock{cacheMutex};
  if(const auto found = cache.find(key); found != cache.end()) return found->second;

  auto result = std::make_shared<Match>();
  if(!result->database) return result;
  bool loaded = result->database->Load() == LF_NO_ERROR;
  if(!configured.isEmpty())
    loaded = result->database->LoadDirectory(configured.toUtf8().constData()) == LF_NO_ERROR || loaded;
  const QDir applicationDirectory{QCoreApplication::applicationDirPath()};
  for(const QString &relative : {QStringLiteral("../share/lensfun"), QStringLiteral("../Resources/lensfun")})
  {
    const QDir root{applicationDirectory.filePath(relative)};
    for(const QString &version : root.entryList({QStringLiteral("version_*")}, QDir::Dirs | QDir::NoDotAndDotDot))
      loaded = result->database->LoadDirectory(root.filePath(version).toUtf8().constData()) == LF_NO_ERROR || loaded;
  }
  if(!loaded || metadata.make.empty() || metadata.model.empty() || metadata.lensModel.empty())
  { cache[key] = result; return result; }
  const lfCamera **cameras = result->database->FindCamerasExt(metadata.make.c_str(), metadata.model.c_str(), 0);
  if(!cameras || !cameras[0] || cameras[1])
  { if(cameras) lf_free(cameras); cache[key] = result; return result; }
  result->camera = cameras[0];
  const lfLens **lenses = result->database->FindLenses(result->camera,
    metadata.lensMake.empty() ? nullptr : metadata.lensMake.c_str(), metadata.lensModel.c_str(), LF_SEARCH_SORT_AND_UNIQUIFY);
  lf_free(cameras);
  if(!lenses || !lenses[0] || lenses[1])
  { if(lenses) lf_free(lenses); result->camera=nullptr; cache[key] = result; return result; }
  result->lens = lenses[0];
  lf_free(lenses);
  cache[key] = result;
  return result;
}

#if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
std::unique_ptr<lfModifier> modifierFor(const Match &found, const raw::RawMetadata &metadata,
                                        const int width, const int height, const int flags,
                                        int &enabled)
{
  const float focal = static_cast<float>(metadata.focalLengthMm.value_or(0.0));
  auto modifier = std::make_unique<lfModifier>(found.lens,
    found.camera ? found.camera->CropFactor : 1.0f, width, height);
  const float aperture = static_cast<float>(metadata.aperture.value_or(0.0));
  enabled = modifier->Initialize(found.lens, LF_PF_F32, focal, aperture, 1000.0f,
    flags & (LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY) ? 0.0f : 1.0f,
    found.lens->Type, flags, false);
  return modifier;
}
#endif
#endif

}

OpticsStatus opticsStatus(const raw::RawMetadata &metadata, const int width, const int height)
{
  OpticsStatus status;
#ifdef RAWLAB_HAS_LENSFUN
  status.lensfunAvailable = true;
  auto found = match(metadata);
  if(!found->lens)
  {
    status.message = metadata.lensModel.empty() ? "Lens metadata is missing" : "No unambiguous Lensfun profile";
    return status;
  }
  status.profileMatched = true;
  const float focal = static_cast<float>(metadata.focalLengthMm.value_or(0.0));
  if(focal <= 0.0f) { status.message = "Focal length is missing"; return status; }
  #if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
  const int requested = LF_MODIFY_TCA | LF_MODIFY_DISTORTION
    | (metadata.aperture ? LF_MODIFY_VIGNETTING : 0);
  int enabled = 0;
  auto modifier = modifierFor(*found, metadata, width, height, requested, enabled);
  #else
  std::unique_ptr<lfModifier, decltype(&lf_modifier_destroy)> modifier{
    lf_modifier_create(found->lens, focal, found->camera ? found->camera->CropFactor : 1.0f,
                       width, height, LF_PF_F32, false), lf_modifier_destroy};
  #endif
  if(!modifier) { status.message = "Could not initialize Lensfun"; return status; }
  #if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
  status.chromaticAberrationAvailable = enabled & LF_MODIFY_TCA;
  status.lensCorrectionAvailable = enabled & (LF_MODIFY_DISTORTION | LF_MODIFY_VIGNETTING);
  #else
  status.chromaticAberrationAvailable = lf_modifier_enable_tca_correction(modifier.get()) != 0;
  status.lensCorrectionAvailable = lf_modifier_enable_distortion_correction(modifier.get()) != 0;
  if(metadata.aperture) status.lensCorrectionAvailable =
    lf_modifier_enable_vignetting_correction(modifier.get(), static_cast<float>(*metadata.aperture), 1000.0f) != 0
    || status.lensCorrectionAvailable;
  #endif
  const char *maker = lf_mlstr_get(found->lens->Maker);
  const char *model = lf_mlstr_get(found->lens->Model);
  status.message = std::string{maker ? maker : ""}
    + (maker && *maker && model && *model ? " " : "")
    + (model && *model ? model : "Lensfun profile");
#else
  (void)metadata; (void)width; (void)height;
  status.message = "Lensfun is not available in this build";
#endif
  return status;
}

LinearRgbImage applyOpticsCorrections(LinearRgbImage source, const raw::RawMetadata &metadata,
                                      const OpticsOptions &options, OpticsStatus *status)
{
  OpticsStatus current;
  if(status) { current = opticsStatus(metadata, source.width, source.height); *status = current; }
  if(!options.chromaticAberration && !options.lensCorrection) return source;
#ifndef RAWLAB_HAS_LENSFUN
  throw std::runtime_error{current.message};
#else
  auto found = match(metadata);
  if(!found->lens) throw std::runtime_error{current.message.empty()
    ? "No unambiguous Lensfun profile" : current.message};
  #if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
  const int requested = (options.chromaticAberration ? LF_MODIFY_TCA : 0)
    | (options.lensCorrection ? LF_MODIFY_DISTORTION : 0)
    | (options.lensCorrection && metadata.aperture ? LF_MODIFY_VIGNETTING : 0);
  int enabled = 0;
  auto modifier = modifierFor(*found, metadata, source.width, source.height, requested, enabled);
  #else
  const float focal = static_cast<float>(metadata.focalLengthMm.value_or(0.0));
  std::unique_ptr<lfModifier, decltype(&lf_modifier_destroy)> modifier{
    lf_modifier_create(found->lens, focal, found->camera ? found->camera->CropFactor : 1.0f,
                       source.width, source.height, LF_PF_F32, false), lf_modifier_destroy};
  #endif
  if(!modifier) throw std::runtime_error{"Could not initialize Lensfun"};
  bool geometry = false, subpixel = false;
  #if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
  geometry = options.lensCorrection && (enabled & LF_MODIFY_DISTORTION);
  subpixel = options.chromaticAberration && (enabled & LF_MODIFY_TCA);
  if(options.lensCorrection && (enabled & LF_MODIFY_VIGNETTING))
    modifier->ApplyColorModification(source.pixels.data(), 0.0f, 0.0f,
      source.width, source.height, LF_CR_3(RED, GREEN, BLUE), source.width * 3 * static_cast<int>(sizeof(float)));
  #else
  if(options.lensCorrection)
  {
    geometry = lf_modifier_enable_distortion_correction(modifier.get()) != 0;
    if(geometry)
    {
      const float scale = lf_modifier_get_auto_scale(modifier.get(), false);
      if(std::isfinite(scale) && scale > 0.0f) lf_modifier_enable_scaling(modifier.get(), scale);
    }
    if(metadata.aperture)
      lf_modifier_enable_vignetting_correction(modifier.get(), static_cast<float>(*metadata.aperture), 1000.0f);
    lf_modifier_apply_color_modification(modifier.get(), source.pixels.data(), 0.0f, 0.0f,
      source.width, source.height, LF_CR_3(RED, GREEN, BLUE), source.width * 3 * static_cast<int>(sizeof(float)));
  }
  if(options.chromaticAberration) subpixel = lf_modifier_enable_tca_correction(modifier.get()) != 0;
  #endif
  if(!geometry && !subpixel) return source;
  LinearRgbImage corrected{source.width, source.height, std::vector<float>(source.pixels.size())};
  std::vector<float> coordinates(static_cast<std::size_t>(source.width) * 6U);
  std::vector<std::uint8_t> valid(static_cast<std::size_t>(source.width) * source.height, 1);
  for(int y = 0; y < source.height; ++y)
  {
    #if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
    const bool mapped = modifier->ApplySubpixelGeometryDistortion(0.0f,
      static_cast<float>(y), source.width, 1, coordinates.data());
    #else
    const bool mapped = lf_modifier_apply_subpixel_geometry_distortion(modifier.get(), 0.0f,
      static_cast<float>(y), source.width, 1, coordinates.data());
    #endif
    if(!mapped) throw std::runtime_error{"Lensfun could not map image geometry"};
    for(int x = 0; x < source.width; ++x)
    {
      for(int channel = 0; channel < 3; ++channel)
      {
        const std::size_t coordinate = static_cast<std::size_t>(x) * 6U + static_cast<std::size_t>(channel) * 2U;
        const float mappedX = coordinates[coordinate];
        const float mappedY = coordinates[coordinate + 1];
        if(mappedX < 0.0f || mappedY < 0.0f
           || mappedX > source.width - 1.0f || mappedY > source.height - 1.0f)
          valid[static_cast<std::size_t>(y) * source.width + x] = 0;
        corrected.pixels[(static_cast<std::size_t>(y) * source.width + x) * 3U + channel]
          = bilinear(source, mappedX, mappedY, channel);
      }
    }
  }

  if(std::all_of(valid.begin(), valid.end(), [](const std::uint8_t value) { return value != 0; }))
    return corrected;

  const int stride = source.width + 1;
  std::vector<int> invalidPrefix(static_cast<std::size_t>(stride) * (source.height + 1), 0);
  for(int y = 0; y < source.height; ++y)
    for(int x = 0; x < source.width; ++x)
      invalidPrefix[static_cast<std::size_t>(y + 1) * stride + x + 1]
        = invalidPrefix[static_cast<std::size_t>(y) * stride + x + 1]
        + invalidPrefix[static_cast<std::size_t>(y + 1) * stride + x]
        - invalidPrefix[static_cast<std::size_t>(y) * stride + x]
        + (valid[static_cast<std::size_t>(y) * source.width + x] ? 0 : 1);
  const auto invalidCount = [&](const int left, const int top, const int width, const int height) {
    const int right = left + width, bottom = top + height;
    return invalidPrefix[static_cast<std::size_t>(bottom) * stride + right]
      - invalidPrefix[static_cast<std::size_t>(top) * stride + right]
      - invalidPrefix[static_cast<std::size_t>(bottom) * stride + left]
      + invalidPrefix[static_cast<std::size_t>(top) * stride + left];
  };
  int cropWidth = source.width, cropHeight = source.height, cropLeft = 0, cropTop = 0;
  const int steps = std::max(source.width, source.height) / 2;
  for(int step = 1; step <= steps; ++step)
  {
    const float scale = 1.0f - 2.0f * static_cast<float>(step)
      / static_cast<float>(std::max(source.width, source.height));
    const int width = std::max(2, static_cast<int>(std::floor(source.width * scale)));
    const int height = std::max(2, static_cast<int>(std::floor(source.height * scale)));
    const int left = (source.width - width) / 2, top = (source.height - height) / 2;
    if(invalidCount(left, top, width, height) == 0)
    {
      cropWidth = width; cropHeight = height; cropLeft = left; cropTop = top;
      break;
    }
  }
  if(invalidCount(cropLeft, cropTop, cropWidth, cropHeight) != 0)
    throw std::runtime_error{"Lens correction has no valid centered crop"};

  LinearRgbImage cropped{source.width, source.height, std::vector<float>(source.pixels.size())};
  for(int y = 0; y < cropped.height; ++y)
    for(int x = 0; x < cropped.width; ++x)
    {
      const float sampleX = static_cast<float>(cropLeft)
        + static_cast<float>(x) * static_cast<float>(cropWidth - 1) / std::max(1, cropped.width - 1);
      const float sampleY = static_cast<float>(cropTop)
        + static_cast<float>(y) * static_cast<float>(cropHeight - 1) / std::max(1, cropped.height - 1);
      for(int channel = 0; channel < 3; ++channel)
        cropped.pixels[(static_cast<std::size_t>(y) * cropped.width + x) * 3U + channel]
          = bilinear(corrected, sampleX, sampleY, channel);
    }
  return cropped;
#endif
}

LinearRgbImage renderLinearRgb(const raw::RawImage &image, const OpticsOptions &options,
                               OpticsStatus *status)
{
  return applyOpticsCorrections(baseLinearRgb(image), image.metadata(), options, status);
}

}
