#include "rawlab/image/LensCorrection.h"

#include "rawlab/image/AmazeDemosaic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <deque>
#include <cstdint>
#include <tuple>
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
    loaded = result->database->LoadDirectory(configured.toUtf8().constData()) || loaded;
  const QDir applicationDirectory{QCoreApplication::applicationDirPath()};
  for(const QString &relative : {QStringLiteral("../share/lensfun"), QStringLiteral("../Resources/lensfun")})
  {
    const QDir root{applicationDirectory.filePath(relative)};
    for(const QString &version : root.entryList({QStringLiteral("version_*")}, QDir::Dirs | QDir::NoDotAndDotDot))
      loaded = result->database->LoadDirectory(root.filePath(version).toUtf8().constData()) || loaded;
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

namespace {
#ifdef RAWLAB_HAS_LENSFUN
struct RegionCorrection
{
  std::shared_ptr<Match> profile;
  std::unique_ptr<lfModifier, decltype(&lf_modifier_destroy)> modifier{nullptr, lf_modifier_destroy};
  int width, height, left = 0, top = 0, cropWidth, cropHeight;
  bool warp = false, vignette = false;

  void map(int x, int y, int count, float *coordinates) const
  {
#if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
    const bool mapped = modifier->ApplySubpixelGeometryDistortion(x, y, count, 1, coordinates);
#else
    const bool mapped = lf_modifier_apply_subpixel_geometry_distortion(modifier.get(), x, y, count, 1, coordinates);
#endif
    if(!mapped) throw std::runtime_error{"Lensfun could not map image geometry"};
  }

  RegionCorrection(int w, int h, const raw::RawMetadata &metadata, const OpticsOptions &options,
                   const std::function<void()> &check)
    : profile{match(metadata)}, width{w}, height{h}, cropWidth{w}, cropHeight{h}
  {
    if(!profile->lens) throw std::runtime_error{"No unambiguous Lensfun profile"};
#if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
    const int requested = (options.chromaticAberration ? LF_MODIFY_TCA : 0)
      | (options.lensCorrection ? LF_MODIFY_DISTORTION : 0)
      | (options.lensCorrection && metadata.aperture ? LF_MODIFY_VIGNETTING : 0);
    int enabled = 0;
    modifier.reset(modifierFor(*profile, metadata, w, h, requested, enabled).release());
    warp = enabled & (LF_MODIFY_DISTORTION | LF_MODIFY_TCA);
    vignette = enabled & LF_MODIFY_VIGNETTING;
#else
    modifier.reset(lf_modifier_create(profile->lens, metadata.focalLengthMm.value_or(0.0),
      profile->camera ? profile->camera->CropFactor : 1.0f, w, h, LF_PF_F32, false));
    if(!modifier) throw std::runtime_error{"Could not initialize Lensfun"};
    if(options.lensCorrection)
    {
      warp = lf_modifier_enable_distortion_correction(modifier.get()) != 0;
      if(warp)
      {
        const float scale = lf_modifier_get_auto_scale(modifier.get(), false);
        if(std::isfinite(scale) && scale > 0.0f) lf_modifier_enable_scaling(modifier.get(), scale);
      }
      if(metadata.aperture)
        vignette = lf_modifier_enable_vignetting_correction(modifier.get(), *metadata.aperture, 1000.0f) != 0;
    }
    if(options.chromaticAberration) warp = lf_modifier_enable_tca_correction(modifier.get()) != 0 || warp;
#endif
    if(!modifier) throw std::runtime_error{"Could not initialize Lensfun"};
    if(!warp) return;

    // Match applyOpticsCorrections' centered crop without allocating a full RGB
    // image or integral mask. Each invalid pixel can only shrink this rectangle.
    const int steps = std::max(w, h) / 2;
    const auto crop = [&](int step) {
      const float scale = 1.0f - 2.0f * step / std::max(w, h);
      return std::pair{std::max(2, static_cast<int>(std::floor(w * scale))),
                       std::max(2, static_cast<int>(std::floor(h * scale)))};
    };
    int step = 0;
    std::vector<float> coordinates(static_cast<std::size_t>(w) * 6);
    for(int y = 0; y < h; ++y)
    {
      if(check && y % 32 == 0) check();
      map(0, y, w, coordinates.data());
      for(int x = 0; x < w; ++x)
        for(int channel = 0; channel < 3; ++channel)
        {
          const float sx = coordinates[x * 6 + channel * 2], sy = coordinates[x * 6 + channel * 2 + 1];
          if(!std::isfinite(sx) || !std::isfinite(sy)) throw std::runtime_error{"Invalid Lensfun coordinates"};
          if(sx >= 0 && sy >= 0 && sx <= w - 1 && sy <= h - 1) continue;
          if(x < left || x >= left + cropWidth || y < top || y >= top + cropHeight) continue;
          int lo = step + 1, hi = steps + 1;
          while(lo < hi)
          {
            const int mid = lo + (hi - lo) / 2;
            const auto [cw, ch] = crop(mid);
            const bool inside = x >= (w - cw) / 2 && x < (w - cw) / 2 + cw
              && y >= (h - ch) / 2 && y < (h - ch) / 2 + ch;
            if(inside) lo = mid + 1; else hi = mid;
          }
          if(lo > steps) throw std::runtime_error{"Lens correction has no valid centered crop"};
          step = lo;
          std::tie(cropWidth, cropHeight) = crop(step);
          left = (w - cropWidth) / 2; top = (h - cropHeight) / 2;
        }
    }
  }

  LinearRgbImage render(int x, int y, int w, int h,
    const std::function<LinearRgbImage(int, int, int, int)> &source,
    const std::function<void()> &check) const
  {
    const bool cropped = cropWidth != width || cropHeight != height;
    const auto outputX = [&](int px) { return cropped ? left + static_cast<float>(px) * (cropWidth - 1) / std::max(1, width - 1) : static_cast<float>(px); };
    const auto outputY = [&](int py) { return cropped ? top + static_cast<float>(py) * (cropHeight - 1) / std::max(1, height - 1) : static_cast<float>(py); };
    const int mx = static_cast<int>(std::floor(outputX(x))), my = static_cast<int>(std::floor(outputY(y)));
    const int mw = static_cast<int>(std::ceil(outputX(x + w - 1))) - mx + 1;
    const int mh = static_cast<int>(std::ceil(outputY(y + h - 1))) - my + 1;
    std::vector<float> coordinates(warp ? static_cast<std::size_t>(mw) * mh * 6 : 0);
    int sx = mx, sy = my, right = mx + mw - 1, bottom = my + mh - 1;
    if(warp)
    {
      sx = width - 1; sy = height - 1; right = bottom = 0;
      // Keep Lensfun's full-row origin: its vectorized mapping can otherwise
      // shift detail pixels relative to the full-image correction.
      std::vector<float> fullRow(static_cast<std::size_t>(width) * 6);
      for(int row = 0; row < mh; ++row)
      {
        if(check && row % 32 == 0) check();
        float *line = coordinates.data() + static_cast<std::size_t>(row) * mw * 6;
        map(0, my + row, width, fullRow.data());
        std::copy_n(fullRow.data() + mx * 6, mw * 6, line);
        for(int i = 0; i < mw * 3; ++i)
        {
          const float px = line[i * 2], py = line[i * 2 + 1];
          if(!std::isfinite(px) || !std::isfinite(py)) throw std::runtime_error{"Invalid Lensfun coordinates"};
          if(px < 0 || py < 0 || px > width - 1 || py > height - 1) continue;
          sx = std::min(sx, static_cast<int>(std::floor(px))); sy = std::min(sy, static_cast<int>(std::floor(py)));
          right = std::max(right, static_cast<int>(std::ceil(px))); bottom = std::max(bottom, static_cast<int>(std::ceil(py)));
        }
      }
    }
    auto input = source(sx, sy, std::max(1, right - sx + 1), std::max(1, bottom - sy + 1));
    if(vignette)
    {
#if LF_VERSION_MAJOR == 0 && LF_VERSION_MINOR < 4
      modifier->ApplyColorModification(input.pixels.data(), sx, sy, input.width, input.height,
        LF_CR_3(RED, GREEN, BLUE), input.width * 3 * static_cast<int>(sizeof(float)));
#else
      lf_modifier_apply_color_modification(modifier.get(), input.pixels.data(), sx, sy, input.width, input.height,
        LF_CR_3(RED, GREEN, BLUE), input.width * 3 * static_cast<int>(sizeof(float)));
#endif
    }
    if(!warp) return input;
    LinearRgbImage corrected{mw, mh, std::vector<float>(static_cast<std::size_t>(mw) * mh * 3)};
    for(std::size_t i = 0; i < corrected.pixels.size(); ++i)
      corrected.pixels[i] = bilinear(input, coordinates[i * 2] - sx, coordinates[i * 2 + 1] - sy, i % 3);
    if(!cropped) return corrected;
    LinearRgbImage result{w, h, std::vector<float>(static_cast<std::size_t>(w) * h * 3)};
    for(int row = 0; row < h; ++row)
      for(int col = 0; col < w; ++col)
        for(int channel = 0; channel < 3; ++channel)
          result.pixels[(static_cast<std::size_t>(row) * w + col) * 3 + channel]
            = bilinear(corrected, outputX(x + col) - mx, outputY(y + row) - my, channel);
    return result;
  }
};
#endif
}

LinearRgbImage renderOpticsRegion(int fullWidth, int fullHeight, int x, int y, int width, int height,
  const raw::RawMetadata &metadata, const OpticsOptions &options,
  const std::function<LinearRgbImage(int, int, int, int)> &source,
  const std::function<void()> &checkCancelled)
{
  if(x < 0 || y < 0 || width <= 0 || height <= 0
     || static_cast<std::int64_t>(x) + width > fullWidth || static_cast<std::int64_t>(y) + height > fullHeight)
    throw std::invalid_argument{"Invalid corrected preview region"};
  if(checkCancelled) checkCancelled();
  if(!options.chromaticAberration && !options.lensCorrection) return source(x, y, width, height);
#ifndef RAWLAB_HAS_LENSFUN
  (void)metadata;
  throw std::runtime_error{"Lensfun is not available in this build"};
#else
  const QString key = QString::fromStdString(metadata.make + '\n' + metadata.model + '\n'
    + metadata.lensMake + '\n' + metadata.lensModel) + '\n' + qEnvironmentVariable("RAWLAB_LENSFUN_DB")
    + QStringLiteral(":%1:%2:%3:%4:%5:%6").arg(fullWidth).arg(fullHeight)
      .arg(metadata.focalLengthMm.value_or(0), 0, 'g', 17).arg(metadata.aperture.value_or(0), 0, 'g', 17)
      .arg(options.chromaticAberration).arg(options.lensCorrection);
  static std::mutex mutex;
  static std::deque<std::pair<QString, std::shared_ptr<RegionCorrection>>> cache;
  const std::scoped_lock lock{mutex};
  auto found = std::find_if(cache.begin(), cache.end(), [&](const auto &entry) { return entry.first == key; });
  const auto geometry = found == cache.end()
    ? std::make_shared<RegionCorrection>(fullWidth, fullHeight, metadata, options, checkCancelled) : found->second;
  if(found != cache.end()) cache.erase(found);
  cache.emplace_back(key, geometry);
  while(cache.size() > 4) cache.pop_front();
  return geometry->render(x, y, width, height, source, checkCancelled);
#endif
}

}
