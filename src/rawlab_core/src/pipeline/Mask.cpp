#include "rawlab/pipeline/Mask.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <limits>

namespace rawlab::pipeline {

namespace {

std::vector<float> squaredDistanceTransform(const std::vector<float> &source,
                                            const int width, const int height,
                                            const bool featureInside)
{
  constexpr float infinity = 1.0e20f;
  std::vector<float> intermediate(source.size(), infinity);
  std::vector<float> result(source.size(), infinity);
  const int maximum = std::max(width, height);
  std::vector<int> sites(static_cast<std::size_t>(maximum));
  std::vector<float> boundaries(static_cast<std::size_t>(maximum + 1));

  const auto transformLine = [&](const auto valueAt, const int length, const auto write) {
    int count = 0;
    for(int q = 0; q < length; ++q)
    {
      const float value = valueAt(q);
      if(value >= infinity / 2.0f) continue;
      float boundary = 0.0f;
      while(count > 0)
      {
        const int previous = sites[static_cast<std::size_t>(count - 1)];
        boundary = ((value + static_cast<float>(q * q))
                    - (valueAt(previous) + static_cast<float>(previous * previous)))
                   / static_cast<float>(2 * (q - previous));
        if(boundary > boundaries[static_cast<std::size_t>(count - 1)]) break;
        --count;
      }
      sites[static_cast<std::size_t>(count)] = q;
      boundaries[static_cast<std::size_t>(count)] = count == 0
        ? -infinity : boundary;
      ++count;
      boundaries[static_cast<std::size_t>(count)] = infinity;
    }
    if(count == 0) return;
    int site = 0;
    for(int q = 0; q < length; ++q)
    {
      while(site + 1 < count && boundaries[static_cast<std::size_t>(site + 1)] < q) ++site;
      const int nearest = sites[static_cast<std::size_t>(site)];
      const float delta = static_cast<float>(q - nearest);
      write(q, delta * delta + valueAt(nearest));
    }
  };

  for(int y = 0; y < height; ++y)
  {
    const std::size_t row = static_cast<std::size_t>(y * width);
    transformLine([&](const int x) {
      const bool inside = source[row + static_cast<std::size_t>(x)] >= 0.5f;
      return inside == featureInside ? 0.0f : infinity;
    }, width, [&](const int x, const float value) {
      intermediate[row + static_cast<std::size_t>(x)] = value;
    });
  }
  for(int x = 0; x < width; ++x)
  {
    transformLine([&](const int y) {
      return intermediate[static_cast<std::size_t>(y * width + x)];
    }, height, [&](const int y, const float value) {
      result[static_cast<std::size_t>(y * width + x)] = value;
    });
  }
  return result;
}

} // namespace

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

float Mask::sampleBilinear(const float x, const float y) const
{
  if(width_ <= 0 || height_ <= 0) throw std::runtime_error{"Cannot sample an empty mask"};
  const float boundedX = std::clamp(x, 0.0f, static_cast<float>(width_ - 1));
  const float boundedY = std::clamp(y, 0.0f, static_cast<float>(height_ - 1));
  const int x0 = static_cast<int>(std::floor(boundedX));
  const int y0 = static_cast<int>(std::floor(boundedY));
  const int x1 = std::min(x0 + 1, width_ - 1);
  const int y1 = std::min(y0 + 1, height_ - 1);
  const float fx = boundedX - static_cast<float>(x0);
  const float fy = boundedY - static_cast<float>(y0);
  const float top = sampleNearest(x0, y0) * (1.0f - fx) + sampleNearest(x1, y0) * fx;
  const float bottom = sampleNearest(x0, y1) * (1.0f - fx) + sampleNearest(x1, y1) * fx;
  return top * (1.0f - fy) + bottom * fy;
}

float Mask::sampleBilinearMapped(const int rawX, const int rawY,
                                 const int rawWidth, const int rawHeight) const
{
  if(rawWidth <= 0 || rawHeight <= 0) throw std::invalid_argument{"Raw dimensions must be positive"};
  if(rawX < 0 || rawY < 0 || rawX >= rawWidth || rawY >= rawHeight)
  {
    throw std::out_of_range{"Raw sample coordinate out of range"};
  }
  const float x = (static_cast<float>(rawX) + 0.5f) * width_ / rawWidth - 0.5f;
  const float y = (static_cast<float>(rawY) + 0.5f) * height_ / rawHeight - 0.5f;
  return sampleBilinear(x, y);
}

void Mask::paintSegment(const float x0,
                        const float y0,
                        const float x1,
                        const float y1,
                        const float radius,
                        const float hardness,
                        const float value)
{
  if(radius <= 0.0f) throw std::invalid_argument{"Brush radius must be positive"};
  if(width_ <= 0 || height_ <= 0) throw std::runtime_error{"Cannot paint an empty mask"};

  const float target = std::clamp(value, 0.0f, 1.0f);
  const float hardRadius = radius * std::clamp(hardness, 0.0f, 1.0f);
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float lengthSquared = dx * dx + dy * dy;
  const int left = std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - radius)));
  const int top = std::max(0, static_cast<int>(std::floor(std::min(y0, y1) - radius)));
  const int right = std::min(width_ - 1, static_cast<int>(std::ceil(std::max(x0, x1) + radius)));
  const int bottom = std::min(height_ - 1, static_cast<int>(std::ceil(std::max(y0, y1) + radius)));

  for(int y = top; y <= bottom; ++y)
  {
    for(int x = left; x <= right; ++x)
    {
      const float px = static_cast<float>(x) + 0.5f;
      const float py = static_cast<float>(y) + 0.5f;
      const float projection = lengthSquared > 0.0f
        ? std::clamp(((px - x0) * dx + (py - y0) * dy) / lengthSquared, 0.0f, 1.0f)
        : 0.0f;
      const float nearestX = x0 + projection * dx;
      const float nearestY = y0 + projection * dy;
      const float distance = std::hypot(px - nearestX, py - nearestY);
      if(distance > radius) continue;

      float alpha = 1.0f;
      if(distance > hardRadius && hardRadius < radius)
      {
        alpha = (radius - distance) / (radius - hardRadius);
      }
      const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
                                + static_cast<std::size_t>(x);
      values_[index] += (target - values_[index]) * std::clamp(alpha, 0.0f, 1.0f);
    }
  }
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

Mask filledMask(const int width, const int height, const float value)
{
  if(width <= 0 || height <= 0) throw std::invalid_argument{"Mask dimensions must be positive"};
  return Mask{width, height,
              std::vector<float>(static_cast<std::size_t>(width)
                                 * static_cast<std::size_t>(height), value)};
}

Mask featherMask(const Mask &source, const float radius)
{
  if(radius <= 0.0f) return source;
  const bool hasInside = std::any_of(source.values().begin(), source.values().end(),
                                    [](const float value) { return value >= 0.5f; });
  const bool hasOutside = std::any_of(source.values().begin(), source.values().end(),
                                     [](const float value) { return value < 0.5f; });
  if(!hasInside || !hasOutside) return source;
  const auto distanceToInside = squaredDistanceTransform(
    source.values(), source.width(), source.height(), true);
  const auto distanceToOutside = squaredDistanceTransform(
    source.values(), source.width(), source.height(), false);
  std::vector<float> values(source.values().size());
  for(std::size_t i = 0; i < values.size(); ++i)
  {
    const bool inside = source.values()[i] >= 0.5f;
    const float distance = std::max(0.0f, std::sqrt(
      inside ? distanceToOutside[i] : distanceToInside[i]) - 0.5f);
    const float transition = 0.5f * std::min(distance / radius, 1.0f);
    values[i] = inside ? 0.5f + transition : 0.5f - transition;
  }
  return {source.width(), source.height(), std::move(values)};
}

Mask blurMask(const Mask &source, const float radius)
{
  const int extent = static_cast<int>(std::ceil(radius));
  if(extent <= 0) return source;
  const int width = source.width();
  const int height = source.height();
  std::vector<float> horizontal(source.values().size());
  std::vector<float> result(source.values().size());
  for(int y = 0; y < height; ++y)
  {
    std::vector<double> prefix(static_cast<std::size_t>(width + 1));
    for(int x = 0; x < width; ++x)
    {
      prefix[static_cast<std::size_t>(x + 1)] = prefix[static_cast<std::size_t>(x)]
        + source.values()[static_cast<std::size_t>(y * width + x)];
    }
    for(int x = 0; x < width; ++x)
    {
      const int first = std::max(0, x - extent);
      const int last = std::min(width - 1, x + extent);
      horizontal[static_cast<std::size_t>(y * width + x)] = static_cast<float>(
        (prefix[static_cast<std::size_t>(last + 1)] - prefix[static_cast<std::size_t>(first)])
        / static_cast<double>(last - first + 1));
    }
  }
  for(int x = 0; x < width; ++x)
  {
    std::vector<double> prefix(static_cast<std::size_t>(height + 1));
    for(int y = 0; y < height; ++y)
    {
      prefix[static_cast<std::size_t>(y + 1)] = prefix[static_cast<std::size_t>(y)]
        + horizontal[static_cast<std::size_t>(y * width + x)];
    }
    for(int y = 0; y < height; ++y)
    {
      const int first = std::max(0, y - extent);
      const int last = std::min(height - 1, y + extent);
      result[static_cast<std::size_t>(y * width + x)] = static_cast<float>(
        (prefix[static_cast<std::size_t>(last + 1)] - prefix[static_cast<std::size_t>(first)])
        / static_cast<double>(last - first + 1));
    }
  }
  return {width, height, std::move(result)};
}

}
