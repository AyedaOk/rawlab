#pragma once

#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <filesystem>

namespace rawlab::raw {

struct RawFileMetadata
{
  RawMetadata metadata;
  int rawWidth = 0;
  int rawHeight = 0;
};

class LibRawDecoder
{
public:
  RawInspectInfo inspect(const std::filesystem::path &path) const;
  RawFileMetadata readMetadata(const std::filesystem::path &path) const;
  RawImage decodeRawImage(const std::filesystem::path &path) const;
};

}
