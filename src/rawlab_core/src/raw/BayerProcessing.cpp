#include "rawlab/raw/BayerProcessing.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <stdexcept>

namespace rawlab::raw {
namespace {

int channelIndex(const CfaColor color)
{
  switch(color)
  {
    case CfaColor::Red: return 0;
    case CfaColor::Green: return 1;
    case CfaColor::Blue: return 2;
    case CfaColor::Unknown: break;
  }
  throw std::invalid_argument{"Unsupported CFA color"};
}

float siteBlack(const RawMetadata &metadata, const int x, const int y)
{
  return metadata.blackLevel[static_cast<std::size_t>(siteIndex(x, y))];
}

} // namespace

RawImage mildRawDenoise(const RawImage &original, const float amount)
{
  if(!std::isfinite(amount) || amount < 0 || amount > 1)
    throw std::invalid_argument{"Raw denoise amount must be between 0 and 1"};
  if(amount == 0) return original;
  int rx = 0, ry = 0;
  if(!redOrigin(original.metadata().cfa, rx, ry))
    throw std::invalid_argument{"Smart blend requires Bayer CFA"};
  auto output = original.samples();
  auto area = original.metadata().activeArea;
  if(area.width <= 0 || area.height <= 0) area = {0, 0, original.width(), original.height()};
  if(area.left < 0 || area.top < 0 || area.width > original.width() || area.height > original.height()
     || area.left > original.width()-area.width || area.top > original.height()-area.height)
    throw std::invalid_argument{"Invalid raw denoise active area"};
  for(const float black : original.metadata().blackLevel)
    if(!std::isfinite(black) || !std::isfinite(original.metadata().whiteLevel) || original.metadata().whiteLevel <= black)
      throw std::invalid_argument{"Invalid raw denoise black/white levels"};
  // Adapted from darktable rawdenoise.c and common/dwt.c, GPL-3.0-or-later,
  // Copyright (C) 2011-2026 darktable developers: square-root, five-scale hat wavelets.
  constexpr float noise[]{0.8002f, 0.2735f, 0.1202f, 0.0585f, 0.0291f};
  for(int py = 0; py < 2; ++py) for(int px = 0; px < 2; ++px)
  {
    const int w = (area.width - px + 1) / 2, h = (area.height - py + 1) / 2;
    if(w <= 0 || h <= 0) continue;
    std::vector<float> plane(std::size_t(w) * h), temp(plane.size()), detail(plane.size(), 0);
    for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x)
    {
      const int sx = area.left + 2*x + px, sy = area.top + 2*y + py;
      if(!std::isfinite(original.sample(sx, sy))) throw std::invalid_argument{"Nonfinite raw denoise sample"};
      plane[std::size_t(y)*w+x] = std::sqrt(std::max(0.0f, normalizeRawSample(
        original.sample(sx, sy), siteBlack(original.metadata(), sx, sy), original.metadata().whiteLevel)));
    }
    const auto reflect = [](int p, int n) { return p < 0 ? -p : p >= n ? 2*(n-1)-p : p; };
    for(int level = 0; level < 5; ++level)
    {
      const int dx = std::min(1 << level, w-1), dy = std::min(1 << level, h-1);
      for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x)
        temp[std::size_t(y)*w+x] = (2*plane[std::size_t(y)*w+x]
          + plane[std::size_t(reflect(y-dy,h))*w+x] + plane[std::size_t(reflect(y+dy,h))*w+x]) * 0.25f;
      // shortcut: fixed mild threshold; tune or estimate noise after camera/ISO comparisons.
      const float threshold = 0.01f * amount * noise[level];
      for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x)
      {
        const auto i = std::size_t(y)*w+x;
        const float coarse = (2*temp[i] + temp[std::size_t(y)*w+reflect(x-dx,w)]
          + temp[std::size_t(y)*w+reflect(x+dx,w)]) * 0.25f;
        const float diff = plane[i] - coarse;
        detail[i] += std::copysign(std::max(0.0f, std::abs(diff)-threshold), diff);
        plane[i] = coarse;
      }
    }
    for(int y = 0; y < h; ++y) for(int x = 0; x < w; ++x)
    {
      const int sx = area.left + 2*x + px, sy = area.top + 2*y + py;
      const float black = siteBlack(original.metadata(), sx, sy);
      const auto i = std::size_t(y)*w+x;
      const float v = std::max(0.0f, plane[i] + detail[i]);
      // Retain below-black samples and clipped sensor measurements exactly.
      if(original.sample(sx, sy) > black && original.sample(sx, sy) < original.metadata().whiteLevel - 0.5f)
        output[std::size_t(sy)*original.width()+sx] = denormalizeRawSample(v*v, black, original.metadata().whiteLevel);
    }
  }
  return {original.width(), original.height(), std::move(output), original.metadata()};
}

float normalizeRawSample(const float rawValue, const float black, const float white)
{
  const float range = white - black;
  if(range <= 0.0f) throw std::invalid_argument{"Raw normalization range must be positive"};
  return (rawValue - black) / range;
}

float denormalizeRawSample(const float normalizedValue, const float black, const float white)
{
  const float range = white - black;
  if(range <= 0.0f) throw std::invalid_argument{"Raw normalization range must be positive"};
  return normalizedValue * range + black;
}

PackedBayerImage packBayerForceRggb(const RawImage &image, const std::array<float, 3> wbNorm)
{
  int redX = 0;
  int redY = 0;
  if(!redOrigin(image.metadata().cfa, redX, redY))
  {
    throw std::invalid_argument{"Cannot pack non-Bayer CFA image"};
  }

  const int packedWidth = (image.width() - redX) / 2;
  const int packedHeight = (image.height() - redY) / 2;
  if(packedWidth <= 0 || packedHeight <= 0)
  {
    throw std::invalid_argument{"Image is too small for Bayer packing"};
  }

  PackedBayerImage packed;
  packed.width = packedWidth;
  packed.height = packedHeight;
  const std::size_t planeSize = static_cast<std::size_t>(packedWidth) * static_cast<std::size_t>(packedHeight);
  packed.planes.assign(4 * planeSize, 0.0f);

  for(int py = 0; py < packedHeight; ++py)
  {
    const int y0 = redY + 2 * py;
    for(int px = 0; px < packedWidth; ++px)
    {
      const int x0 = redX + 2 * px;
      for(int k = 0; k < 4; ++k)
      {
        const int dx = k & 1;
        const int dy = (k >> 1) & 1;
        const int x = x0 + dx;
        const int y = y0 + dy;
        const CfaColor color = colorAt(image.metadata().cfa, x, y);
        const int channel = channelIndex(color);
        const float black = siteBlack(image.metadata(), x, y);
        const float normalized = normalizeRawSample(image.sample(x, y), black, image.metadata().whiteLevel);
        packed.planes[static_cast<std::size_t>(k) * planeSize
                      + static_cast<std::size_t>(py) * static_cast<std::size_t>(packedWidth)
                      + static_cast<std::size_t>(px)] = normalized * wbNorm[static_cast<std::size_t>(channel)];
      }
    }
  }

  return packed;
}

std::vector<float> remosaicCamRgbToCfa(const RawImage &source,
                                       const std::vector<float> &camRgbPlanes,
                                       const float blendWithModel,
                                       const std::array<float, 3> wbNorm)
{
  const std::size_t planeSize = static_cast<std::size_t>(source.width()) * static_cast<std::size_t>(source.height());
  if(camRgbPlanes.size() != 3 * planeSize)
  {
    throw std::invalid_argument{"camRGB plane count does not match source dimensions"};
  }

  const float alpha = std::clamp(blendWithModel, 0.0f, 1.0f);
  const float invAlpha = 1.0f - alpha;
  std::vector<float> cfa(planeSize, 0.0f);

  for(int y = 0; y < source.height(); ++y)
  {
    for(int x = 0; x < source.width(); ++x)
    {
      const CfaColor color = colorAt(source.metadata().cfa, x, y);
      const int channel = channelIndex(color);
      const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width())
                                + static_cast<std::size_t>(x);
      const float modelValue = camRgbPlanes[static_cast<std::size_t>(channel) * planeSize + pixel];
      const float black = siteBlack(source.metadata(), x, y);
      const float normalized = modelValue / wbNorm[static_cast<std::size_t>(channel)];
      const float rawValue = denormalizeRawSample(normalized, black, source.metadata().whiteLevel);
      cfa[pixel] = alpha * rawValue + invAlpha * source.samples()[pixel];
    }
  }

  return cfa;
}

RawImage blendWithOriginal(const RawImage &original,
                           const RawImage &processed,
                           const float processedAmount)
{
  if(original.width() != processed.width() || original.height() != processed.height())
  {
    throw std::invalid_argument{"Cannot blend RawImage values with different dimensions"};
  }

  const float alpha = std::clamp(processedAmount, 0.0f, 1.0f);
  const float invAlpha = 1.0f - alpha;

  std::vector<float> samples(original.samples().size(), 0.0f);
  for(std::size_t i = 0; i < samples.size(); ++i)
  {
    samples[i] = invAlpha * original.samples()[i] + alpha * processed.samples()[i];
  }

  return RawImage{original.width(), original.height(), std::move(samples), original.metadata()};
}

RawImage preserveClippedHighlights(const RawImage &original,
                                   const RawImage &processed)
{
  if(original.width() != processed.width() || original.height() != processed.height())
  {
    throw std::invalid_argument{"Cannot preserve clipped highlights for RawImage values with different dimensions"};
  }

  std::vector<float> samples = processed.samples();
  const float clippedThreshold = original.metadata().whiteLevel - 0.5f;
  for(std::size_t i = 0; i < samples.size(); ++i)
  {
    if(original.samples()[i] >= clippedThreshold) samples[i] = original.samples()[i];
  }

  return RawImage{processed.width(), processed.height(), std::move(samples), processed.metadata()};
}

}
