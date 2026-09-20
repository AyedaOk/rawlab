#pragma once

#include "rawlab/raw/RawImage.h"
#include "rawlab/image/LensCorrection.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace rawlab::dng {

std::array<std::uint8_t, 4> cfaPatternBytes(const raw::CfaPattern &pattern);

struct JpegPreview
{
  std::vector<std::uint8_t> data;
  int width = 0;
  int height = 0;
};

struct DngPreviews
{
  JpegPreview thumbnail;
  JpegPreview display;
};

class TiffDngWriter
{
public:
  void writeCfaDng(const std::filesystem::path &path, const raw::RawImage &image,
                   const DngPreviews *previews = nullptr) const;
  // Writes a demosaiced, scene-linear RGB DNG. This is intentionally separate
  // from writeCfaDng: the output no longer contains the camera CFA samples.
  void writeLinearRgbDng(const std::filesystem::path &path,
                           const raw::RawImage &image,
                           const image::OpticsOptions &optics = {},
                           const DngPreviews *previews = nullptr) const;
};

}
