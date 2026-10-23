#include "rawlab/raw/RawImage.h"

#include <cstddef>
#include <cmath>
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

ActiveArea RawImage::displayArea() const
{
  auto area = metadata_.activeArea;
  if(area.width <= 0 || area.height <= 0) area = {0, 0, width_, height_};
  if(metadata_.defaultCropOrigin && metadata_.defaultCropSize)
  {
    const auto &origin = *metadata_.defaultCropOrigin;
    const auto &size = *metadata_.defaultCropSize;
    for(int i = 0; i < 2; ++i)
      if(!std::isfinite(origin[i]) || !std::isfinite(size[i]) || origin[i] < 0 || size[i] < 1
         || origin[i] + size[i] > (i == 0 ? area.width : area.height))
        throw std::invalid_argument{"Invalid RAW default crop"};
    area.left += static_cast<int>(std::lround(origin[0]));
    area.top += static_cast<int>(std::lround(origin[1]));
    area.width = static_cast<int>(std::lround(origin[0] + size[0]) - std::lround(origin[0]));
    area.height = static_cast<int>(std::lround(origin[1] + size[1]) - std::lround(origin[1]));
  }
  return area;
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
