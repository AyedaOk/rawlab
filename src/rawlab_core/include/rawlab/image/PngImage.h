#pragma once

#include "rawlab/pipeline/Mask.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace rawlab::image {

struct RgbImage
{
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

RgbImage readRgbPng(const std::filesystem::path &path);
pipeline::Mask readMaskPng(const std::filesystem::path &path);
void writeMaskPng(const std::filesystem::path &path, const pipeline::Mask &mask);

}
