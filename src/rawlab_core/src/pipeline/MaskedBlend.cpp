#include "rawlab/pipeline/MaskedBlend.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace rawlab::pipeline {

raw::RawImage blendWithMask(const raw::RawImage &original,
                            const raw::RawImage &processed,
                            const Mask &mask,
                            const float blendAmount)
{
  if(original.width() != processed.width() || original.height() != processed.height())
  {
    throw std::invalid_argument{"Cannot mask-blend RawImage values with different dimensions"};
  }
  if(mask.width() != original.width() || mask.height() != original.height())
  {
    throw std::invalid_argument{"Raw-space mask dimensions must match RawImage dimensions"};
  }

  const float blend = std::clamp(blendAmount, 0.0f, 1.0f);
  std::vector<float> samples(original.samples().size(), 0.0f);
  for(int y = 0; y < original.height(); ++y)
  {
    for(int x = 0; x < original.width(); ++x)
    {
      const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(original.width())
                              + static_cast<std::size_t>(x);
      const float alpha = blend * mask.sampleNearest(x, y);
      samples[idx] = (1.0f - alpha) * original.samples()[idx] + alpha * processed.samples()[idx];
    }
  }

  return raw::RawImage{original.width(), original.height(), std::move(samples), original.metadata()};
}

raw::RawImage blendWithMappedMask(const raw::RawImage &original,
                                  const raw::RawImage &processed,
                                  const Mask &mask,
                                  const float blendAmount)
{
  if(original.width() != processed.width() || original.height() != processed.height())
  {
    throw std::invalid_argument{"Cannot mask-blend RawImage values with different dimensions"};
  }

  const float blend = std::clamp(blendAmount, 0.0f, 1.0f);
  std::vector<float> samples(original.samples().size(), 0.0f);
  for(int y = 0; y < original.height(); ++y)
  {
    for(int x = 0; x < original.width(); ++x)
    {
      const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(original.width())
                              + static_cast<std::size_t>(x);
      const float alpha = blend * mask.sampleNearestMapped(x, y, original.width(), original.height());
      samples[idx] = (1.0f - alpha) * original.samples()[idx] + alpha * processed.samples()[idx];
    }
  }

  return raw::RawImage{original.width(), original.height(), std::move(samples), original.metadata()};
}

raw::RawImage blendWithPreviewMask(const raw::RawImage &base,
                                   const raw::RawImage &processed,
                                   const Mask &mask,
                                   const image::PreviewGeometry &geometry,
                                   const float blendAmount,
                                   const bool inverted)
{
  if(base.width() != processed.width() || base.height() != processed.height())
  {
    throw std::invalid_argument{"Cannot mask-blend RawImage values with different dimensions"};
  }
  if(mask.width() != geometry.previewWidth || mask.height() != geometry.previewHeight)
  {
    throw std::invalid_argument{"Preview mask dimensions do not match preview geometry"};
  }

  const float blend = std::clamp(blendAmount, 0.0f, 1.0f);
  std::vector<float> samples = base.samples();
  const int left = std::clamp(geometry.rawArea.left, 0, base.width());
  const int top = std::clamp(geometry.rawArea.top, 0, base.height());
  const int right = std::clamp(left + geometry.rawArea.width, left, base.width());
  const int bottom = std::clamp(top + geometry.rawArea.height, top, base.height());
  for(int y = top; y < bottom; ++y)
  {
    for(int x = left; x < right; ++x)
    {
      const auto [previewX, previewY] = geometry.rawToPreview(static_cast<float>(x) + 0.5f,
                                                              static_cast<float>(y) + 0.5f);
      const int maskX = std::clamp(static_cast<int>(previewX), 0, mask.width() - 1);
      const int maskY = std::clamp(static_cast<int>(previewY), 0, mask.height() - 1);
      float maskValue = mask.sampleNearest(maskX, maskY);
      if(inverted) maskValue = 1.0f - maskValue;
      const float alpha = blend * maskValue;
      const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(base.width())
                              + static_cast<std::size_t>(x);
      samples[idx] = (1.0f - alpha) * base.samples()[idx] + alpha * processed.samples()[idx];
    }
  }
  return raw::RawImage{base.width(), base.height(), std::move(samples), base.metadata()};
}

}
