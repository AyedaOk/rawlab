#pragma once

#include <vector>

namespace rawlab::pipeline {

class Mask
{
public:
  Mask() = default;
  Mask(int width, int height, std::vector<float> values);

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<float> &values() const { return values_; }

  float sampleNearest(int x, int y) const;
  float sampleNearestMapped(int rawX, int rawY, int rawWidth, int rawHeight) const;

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<float> values_;
};

Mask horizontalGradientMask(int width, int height);
Mask verticalSplitMask(int width, int height);

}
