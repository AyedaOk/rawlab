#pragma once

#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <filesystem>

namespace rawlab::raw {

class LibRawDecoder
{
public:
  RawInspectInfo inspect(const std::filesystem::path &path) const;
  RawImage decodeRawImage(const std::filesystem::path &path) const;
};

}
