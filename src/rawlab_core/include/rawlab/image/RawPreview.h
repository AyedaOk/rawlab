#pragma once

#include "rawlab/image/PngImage.h"
#include "rawlab/raw/RawImage.h"

#include <utility>

namespace rawlab::image {

enum class PreviewQuality { Fast, High };

struct PreviewGeometry
{
  raw::ActiveArea rawArea;
  int previewWidth = 0;
  int previewHeight = 0;
  int flip = 0;

  std::pair<float, float> rawToPreview(float rawX, float rawY) const;
  std::pair<float, float> previewToRaw(float previewX, float previewY) const;
};

struct RawPreview
{
  RgbImage image;
  PreviewGeometry geometry;
};

RawPreview renderRawPreview(const raw::RawImage &source, int maxDimension = 2048,
                            PreviewQuality quality = PreviewQuality::Fast);

}
