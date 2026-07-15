#include "rawlab/raw/RawImage.h"

#include <cstddef>
#include <utility>

namespace rawlab::raw {

RawImage::RawImage(const int width,
                   const int height,
                   std::vector<float> samples,
                   RawMetadata metadata)
  : width_{width}, height_{height}, samples_{std::move(samples)}, metadata_{std::move(metadata)}
{
  if(width_ <= 0 || height_ <= 0) throw std::invalid_argument{"RawImage dimensions must be positive"};
  if(samples_.size() != static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_))
  {
    throw std::invalid_argument{"RawImage sample count does not match dimensions"};
  }
}

float RawImage::sample(const int x, const int y) const
{
  return samples_[offset(x, y)];
}

void RawImage::setSample(const int x, const int y, const float value)
{
  samples_[offset(x, y)] = value;
}

std::size_t RawImage::offset(const int x, const int y) const
{
  if(x < 0 || y < 0 || x >= width_ || y >= height_)
  {
    throw std::out_of_range{"RawImage sample coordinate out of range"};
  }
  return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
}

}
