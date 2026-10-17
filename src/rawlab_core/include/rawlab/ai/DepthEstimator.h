#pragma once

#include "rawlab/ai/OnnxSession.h"
#include "rawlab/image/PngImage.h"

namespace rawlab::ai {

class DepthEstimator
{
public:
  explicit DepthEstimator(const std::filesystem::path &modelPath, const OnnxSessionOptions &options = {});
  // Relative depth in oriented image coordinates: 0 is near, 1 is far.
  pipeline::Mask estimate(const image::RgbImage &image) const;

private:
  OnnxSession session_;
};

}
