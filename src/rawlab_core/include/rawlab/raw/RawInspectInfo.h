#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace rawlab::raw {

enum class CfaColor : std::uint8_t
{
  Red,
  Green,
  Blue,
  Unknown
};

struct CfaPattern
{
  CfaColor cells[2][2] = {
    { CfaColor::Unknown, CfaColor::Unknown },
    { CfaColor::Unknown, CfaColor::Unknown },
  };
  bool isBayer = false;
};

struct RawInspectInfo
{
  std::filesystem::path path;
  std::string make;
  std::string model;
  std::string normalizedMake;
  std::string normalizedModel;
  std::optional<double> iso;
  std::optional<double> aperture;
  std::optional<double> exposureTimeSeconds;
  std::optional<double> exposureBiasEv;
  std::optional<double> focalLengthMm;
  std::optional<std::int64_t> captureTimestamp;
  int rawWidth = 0;
  int rawHeight = 0;
  int visibleWidth = 0;
  int visibleHeight = 0;
  int activeLeft = 0;
  int activeTop = 0;
  std::uint32_t filters = 0;
  CfaPattern cfa;
  int redOriginX = -1;
  int redOriginY = -1;
  unsigned int blackLevel = 0;
  unsigned int whiteLevel = 0;
  bool hasRawImage = false;
  bool hasColor3Image = false;
  bool rawImageConverted = false;
  float rawSampleMin = 0.0f;
  float rawSampleMax = 0.0f;
  float firstCfaSamples[2][2] = {
    { 0.0f, 0.0f },
    { 0.0f, 0.0f },
  };
};

std::string toString(CfaColor color);
std::string toString(const CfaPattern &pattern);
CfaColor colorAt(const CfaPattern &pattern, int x, int y);
int siteIndex(int x, int y);
bool redOrigin(const CfaPattern &pattern, int &x, int &y);

}
