#pragma once

#include "rawlab/raw/RawInspectInfo.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace rawlab::raw {

struct ActiveArea
{
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
};

struct RawMetadata
{
  std::string make;
  std::string model;
  CfaPattern cfa;
  ActiveArea activeArea;
  std::array<float, 4> blackLevel = { 0.0f, 0.0f, 0.0f, 0.0f };
  float whiteLevel = 1.0f;
  std::array<float, 3> asShotNeutral = { 1.0f, 1.0f, 1.0f };
  std::array<float, 9> colorMatrix1 = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };
};

class RawImage
{
public:
  RawImage() = default;
  RawImage(int width, int height, std::vector<float> samples, RawMetadata metadata);

  int width() const { return width_; }
  int height() const { return height_; }
  const std::vector<float> &samples() const { return samples_; }
  std::vector<float> &samples() { return samples_; }
  const RawMetadata &metadata() const { return metadata_; }
  RawMetadata &metadata() { return metadata_; }

  float sample(int x, int y) const;
  void setSample(int x, int y, float value);

private:
  std::size_t offset(int x, int y) const;

  int width_ = 0;
  int height_ = 0;
  std::vector<float> samples_;
  RawMetadata metadata_;
};

}
