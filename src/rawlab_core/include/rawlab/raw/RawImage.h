#pragma once

#include "rawlab/raw/RawInspectInfo.h"

#include <array>
#include <cstdint>
#include <optional>
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
  std::optional<double> iso;
  std::optional<double> aperture;
  std::optional<double> exposureTimeSeconds;
  std::optional<double> exposureBiasEv;
  std::optional<double> focalLengthMm;
  std::optional<double> focalLength35mm;
  std::optional<std::int64_t> captureTimestamp;
  std::string artist;
  std::string description;
  std::string bodySerial;
  std::string lensMake;
  std::string lensModel;
  std::string lensSerial;
  std::optional<std::array<double, 4>> lensSpecification;
  CfaPattern cfa;
  ActiveArea activeArea;
  // LibRaw orientation: 0 is normal, 3 is 180, 5 is 90 CCW, 6 is 90 CW.
  int flip = 0;
  std::array<float, 4> blackLevel = { 0.0f, 0.0f, 0.0f, 0.0f };
  float whiteLevel = 1.0f;
  std::array<float, 3> asShotNeutral = { 1.0f, 1.0f, 1.0f };
  bool asShotNeutralValid = true;
  std::array<float, 9> cameraToXyz = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };
  std::array<float, 9> cameraToSrgb = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };
  std::array<float, 9> colorMatrix1 = {
    1.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
  };
  bool colorMatrix1Valid = true;
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
