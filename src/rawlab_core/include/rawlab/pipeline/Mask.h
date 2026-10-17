#pragma once

#include <array>
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
  float sampleBilinear(float x, float y) const;
  float sampleBilinearMapped(int rawX, int rawY, int rawWidth, int rawHeight) const;
  void paintSegment(float x0, float y0, float x1, float y1,
                    float radius, float hardness, float value);

private:
  int width_ = 0;
  int height_ = 0;
  std::vector<float> values_;
};

Mask horizontalGradientMask(int width, int height);
Mask linearGradientMask(int width, int height, float x0, float y0, float x1, float y1);
Mask ellipseMask(int width, int height, float left, float top, float right, float bottom);
Mask verticalSplitMask(int width, int height);
Mask filledMask(int width, int height, float value = 0.0f);
Mask featherMask(const Mask &source, float radius);
Mask blurMask(const Mask &source, float radius);
Mask depthRangeMask(const Mask &depth, int width, int height, const std::array<float, 4> &range);

}
