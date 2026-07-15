#include "rawlab/pipeline/Mask.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace rawlab::pipeline {

Mask::Mask(const int width, const int height, std::vector<float> values)
  : width_{width}, height_{height}, values_{std::move(values)}
{
  if(width_ <= 0 || height_ <= 0) throw std::invalid_argument{"Mask dimensions must be positive"};
  if(values_.size() != static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_))
  {
    throw std::invalid_argument{"Mask value count does not match dimensions"};
  }
  for(float &value : values_) value = std::clamp(value, 0.0f, 1.0f);
}

float Mask::sampleNearest(const int x, const int y) const
{
  if(x < 0 || y < 0 || x >= width_ || y >= height_)
  {
    throw std::out_of_range{"Mask sample coordinate out of range"};
  }
  return values_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
                 + static_cast<std::size_t>(x)];
}

float Mask::sampleNearestMapped(const int rawX, const int rawY, const int rawWidth, const int rawHeight) const
{
  if(rawWidth <= 0 || rawHeight <= 0) throw std::invalid_argument{"Raw dimensions must be positive"};
  if(rawX < 0 || rawY < 0 || rawX >= rawWidth || rawY >= rawHeight)
  {
    throw std::out_of_range{"Raw sample coordinate out of range"};
  }

  const int maskX = std::min(width_ - 1, rawX * width_ / rawWidth);
  const int maskY = std::min(height_ - 1, rawY * height_ / rawHeight);
  return sampleNearest(maskX, maskY);
}

Mask horizontalGradientMask(const int width, const int height)
{
  if(width <= 0 || height <= 0) throw std::invalid_argument{"Mask dimensions must be positive"};
  std::vector<float> values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
  for(int y = 0; y < height; ++y)
  {
    for(int x = 0; x < width; ++x)
    {
      values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]
        = width == 1 ? 1.0f : static_cast<float>(x) / static_cast<float>(width - 1);
    }
  }
  return Mask{width, height, std::move(values)};
}

Mask verticalSplitMask(const int width, const int height)
{
  if(width <= 0 || height <= 0) throw std::invalid_argument{"Mask dimensions must be positive"};
  std::vector<float> values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
  for(int y = 0; y < height; ++y)
  {
    for(int x = 0; x < width; ++x)
    {
      values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]
        = x >= width / 2 ? 1.0f : 0.0f;
    }
  }
  return Mask{width, height, std::move(values)};
}

}
