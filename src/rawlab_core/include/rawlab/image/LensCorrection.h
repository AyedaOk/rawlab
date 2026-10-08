#pragma once

#include "rawlab/raw/RawImage.h"

#include <string>
#include <functional>
#include <vector>

namespace rawlab::image {

struct OpticsOptions
{
  bool chromaticAberration = false;
  bool lensCorrection = false;
};

struct OpticsStatus
{
  bool lensfunAvailable = false;
  bool profileMatched = false;
  bool chromaticAberrationAvailable = false;
  bool lensCorrectionAvailable = false;
  std::string message;
};

struct LinearRgbImage
{
  int width = 0;
  int height = 0;
  std::vector<float> pixels;
};

LinearRgbImage renderLinearRgb(const raw::RawImage &image,
                               const OpticsOptions &options = {},
                               OpticsStatus *status = nullptr);
LinearRgbImage applyOpticsCorrections(LinearRgbImage source,
                                      const raw::RawMetadata &metadata,
                                      const OpticsOptions &options,
                                      OpticsStatus *status = nullptr);
OpticsStatus opticsStatus(const raw::RawMetadata &metadata, int width, int height);

// Render a viewport in full-image coordinates. The source callback receives the
// unwarped rectangle needed for interpolation, not the whole photograph.
LinearRgbImage renderOpticsRegion(int fullWidth, int fullHeight, int x, int y, int width, int height,
  const raw::RawMetadata &metadata, const OpticsOptions &options,
  const std::function<LinearRgbImage(int, int, int, int)> &source,
  const std::function<void()> &checkCancelled = {});

}
