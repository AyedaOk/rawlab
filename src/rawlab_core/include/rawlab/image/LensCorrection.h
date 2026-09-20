#pragma once

#include "rawlab/raw/RawImage.h"

#include <string>
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

}
