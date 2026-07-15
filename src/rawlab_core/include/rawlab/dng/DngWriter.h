#pragma once

#include "rawlab/raw/RawImage.h"

#include <array>
#include <cstdint>
#include <filesystem>

namespace rawlab::dng {

std::array<std::uint8_t, 4> cfaPatternBytes(const raw::CfaPattern &pattern);

class TiffDngWriter
{
public:
  void writeCfaDng(const std::filesystem::path &path, const raw::RawImage &image) const;
};

}
