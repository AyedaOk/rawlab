#include "rawlab/pipeline/LayerComposition.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rawlab::pipeline {

raw::RawImage composeDenoiseLayers(const raw::RawImage &source,
                                   const GlobalLayerInput &global,
                                   const std::span<const LocalLayerInput> localsBottomToTop,
                                   const image::PreviewGeometry &geometry)
{
  if(geometry.previewWidth <= 0 || geometry.previewHeight <= 0
     || geometry.rawArea.width <= 0 || geometry.rawArea.height <= 0)
  {
    throw std::invalid_argument{"Invalid preview geometry"};
  }
  const auto validateImage = [&source](const raw::RawImage &image) {
    if(image.width() != source.width() || image.height() != source.height())
    {
      throw std::invalid_argument{"Cannot compose RawImage values with different dimensions"};
    }
  };
  if(global.enabled && global.processed) validateImage(*global.processed);
  for(const LocalLayerInput &local : localsBottomToTop)
  {
    if(!local.enabled || !local.processed || !local.mask) continue;
    validateImage(*local.processed);
    if(local.mask->width() != geometry.previewWidth || local.mask->height() != geometry.previewHeight)
    {
      throw std::invalid_argument{"Preview mask dimensions do not match preview geometry"};
    }
  }

  std::vector<float> samples = source.samples();
  const unsigned available = std::max(1U, std::thread::hardware_concurrency());
  const int workers = samples.size() < 500'000
    ? 1 : std::min<int>({source.height(), static_cast<int>(available), 4});
  const auto processRows = [&](const int begin, const int end) {
    const float globalBlend = std::clamp(global.blend, 0.0f, 1.0f);
    const int left = std::clamp(geometry.rawArea.left, 0, source.width());
    const int top = std::clamp(geometry.rawArea.top, 0, source.height());
    const int right = std::clamp(left + geometry.rawArea.width, left, source.width());
    const int bottom = std::clamp(top + geometry.rawArea.height, top, source.height());
    for(int y = begin; y < end; ++y)
    {
      for(int x = 0; x < source.width(); ++x)
      {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width())
                                  + static_cast<std::size_t>(x);
        const float original = source.samples()[index];
        float value = original;
        if(global.enabled && global.processed)
        {
          value = (1.0f - globalBlend) * original + globalBlend * global.processed->samples()[index];
        }
        if(x >= left && x < right && y >= top && y < bottom)
        {
          for(const LocalLayerInput &local : localsBottomToTop)
          {
            if(!local.enabled || !local.processed || !local.mask) continue;
            const auto [previewX, previewY] = geometry.rawToPreview(
              static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            float maskValue = local.mask->sampleBilinear(previewX - 0.5f, previewY - 0.5f);
            if(local.inverted) maskValue = 1.0f - maskValue;
            const float localBlend = std::clamp(local.blend, 0.0f, 1.0f);
            const float localValue = (1.0f - localBlend) * original
                                     + localBlend * local.processed->samples()[index];
            value = (1.0f - maskValue) * value + maskValue * localValue;
          }
        }
        samples[index] = value;
      }
    }
  };
  if(workers == 1) processRows(0, source.height());
  else
  {
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers));
    for(int worker = 0; worker < workers; ++worker)
    {
      const int begin = source.height() * worker / workers;
      const int end = source.height() * (worker + 1) / workers;
      threads.emplace_back(processRows, begin, end);
    }
    for(std::thread &thread : threads) thread.join();
  }
  return raw::RawImage{source.width(), source.height(), std::move(samples), source.metadata()};
}

}
