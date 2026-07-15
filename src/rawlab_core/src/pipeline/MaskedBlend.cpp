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

}
