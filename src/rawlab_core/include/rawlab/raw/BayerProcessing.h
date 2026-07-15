#pragma once

#include "rawlab/raw/RawImage.h"

#include <array>
#include <vector>

namespace rawlab::raw {

struct PackedBayerImage
{
  int width = 0;
  int height = 0;
  // Planar channel order matching Darktable RawNIND Bayer: R, G1, G2, B.
  std::vector<float> planes;
};

float normalizeRawSample(float rawValue, float black, float white);
float denormalizeRawSample(float normalizedValue, float black, float white);

PackedBayerImage packBayerForceRggb(const RawImage &image,
                                    std::array<float, 3> wbNorm = { 1.0f, 1.0f, 1.0f });

std::vector<float> remosaicCamRgbToCfa(const RawImage &source,
                                       const std::vector<float> &camRgbPlanes,
                                       float blendWithModel,
                                       std::array<float, 3> wbNorm = { 1.0f, 1.0f, 1.0f });

RawImage blendWithOriginal(const RawImage &original,
                           const RawImage &processed,
                           float processedAmount);

RawImage preserveClippedHighlights(const RawImage &original,
                                   const RawImage &processed);

}
