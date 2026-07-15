#include "rawlab/raw/LibRawDecoder.h"

#include <libraw/libraw.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace rawlab::raw {
namespace {

CfaColor colorFromLibRaw(const int color)
{
  switch(color)
  {
    case 0: return CfaColor::Red;
    case 1: return CfaColor::Green;
    case 2: return CfaColor::Blue;
    case 3: return CfaColor::Green;
    default: return CfaColor::Unknown;
  }
}

void throwOnLibRawError(const int error, const char *operation)
{
  if(error == LIBRAW_SUCCESS) return;
  throw std::runtime_error(std::string{operation} + " failed: "
                           + libraw_strerror(error));
}

RawMetadata metadataFromLibRaw(LibRaw &processor)
{
  const libraw_iparams_t &id = processor.imgdata.idata;
  const libraw_image_sizes_t &sizes = processor.imgdata.sizes;
  const libraw_colordata_t &color = processor.imgdata.color;

  RawMetadata metadata;
  metadata.make = id.make;
  metadata.model = id.model;
  metadata.activeArea = {
    sizes.left_margin,
    sizes.top_margin,
    sizes.width,
    sizes.height,
  };
  metadata.whiteLevel = static_cast<float>(color.maximum);
  float neutralMax = 0.0f;
  for(std::size_t c = 0; c < metadata.asShotNeutral.size(); ++c)
  {
    metadata.asShotNeutral[c] = color.cam_mul[c] > 0.0f ? 1.0f / color.cam_mul[c] : 1.0f;
    neutralMax = std::max(neutralMax, metadata.asShotNeutral[c]);
  }
  if(neutralMax > 0.0f)
  {
    for(float &neutral : metadata.asShotNeutral) neutral /= neutralMax;
  }
  for(int cam = 0; cam < 3; ++cam)
  {
    for(int xyz = 0; xyz < 3; ++xyz)
    {
      metadata.colorMatrix1[static_cast<std::size_t>(cam * 3 + xyz)] = color.cam_xyz[cam][xyz];
    }
  }

  const bool hasSeparateBlack = color.cblack[0] != 0 || color.cblack[1] != 0
                                || color.cblack[2] != 0 || color.cblack[3] != 0;
  for(std::size_t i = 0; i < metadata.blackLevel.size(); ++i)
  {
    metadata.blackLevel[i] = hasSeparateBlack
      ? static_cast<float>(color.cblack[i])
      : static_cast<float>(color.black);
  }

  if(id.filters != 0 && id.filters != 9u)
  {
    metadata.cfa.isBayer = true;
    for(int y = 0; y < 2; ++y)
    {
      for(int x = 0; x < 2; ++x)
      {
        metadata.cfa.cells[y][x] = colorFromLibRaw(processor.COLOR(y, x));
      }
    }
  }

  return metadata;
}

RawImage rawImageFromLibRaw(LibRaw &processor)
{
  const libraw_image_sizes_t &sizes = processor.imgdata.sizes;
  const libraw_rawdata_t &rawdata = processor.imgdata.rawdata;
  if(!rawdata.raw_image)
  {
    throw std::runtime_error{"LibRaw did not expose a raw_image buffer"};
  }

  const std::size_t sampleCount = static_cast<std::size_t>(sizes.raw_width)
                                  * static_cast<std::size_t>(sizes.raw_height);
  std::vector<float> samples;
  samples.reserve(sampleCount);
  for(std::size_t i = 0; i < sampleCount; ++i)
  {
    samples.push_back(static_cast<float>(rawdata.raw_image[i]));
  }

  return RawImage{sizes.raw_width, sizes.raw_height, std::move(samples), metadataFromLibRaw(processor)};
}

} // namespace

RawInspectInfo LibRawDecoder::inspect(const std::filesystem::path &path) const
{
  LibRaw processor;
  throwOnLibRawError(processor.open_file(path.string().c_str()), "LibRaw open_file");
  throwOnLibRawError(processor.unpack(), "LibRaw unpack");

  const libraw_iparams_t &id = processor.imgdata.idata;
  const libraw_image_sizes_t &sizes = processor.imgdata.sizes;
  const libraw_colordata_t &color = processor.imgdata.color;
  const libraw_rawdata_t &rawdata = processor.imgdata.rawdata;

  RawInspectInfo info;
  info.path = path;
  info.make = id.make;
  info.model = id.model;
  info.normalizedMake = id.normalized_make;
  info.normalizedModel = id.normalized_model;
  info.rawWidth = sizes.raw_width;
  info.rawHeight = sizes.raw_height;
  info.visibleWidth = sizes.width;
  info.visibleHeight = sizes.height;
  info.activeLeft = sizes.left_margin;
  info.activeTop = sizes.top_margin;
  info.filters = id.filters;
  info.blackLevel = color.black;
  info.whiteLevel = color.maximum;
  info.hasRawImage = rawdata.raw_image != nullptr;
  info.hasColor3Image = rawdata.color3_image != nullptr;

  if(info.filters != 0 && info.filters != 9u)
  {
    info.cfa.isBayer = true;
    for(int y = 0; y < 2; ++y)
    {
      for(int x = 0; x < 2; ++x)
      {
        const CfaColor cfaColor = colorFromLibRaw(processor.COLOR(y, x));
        info.cfa.cells[y][x] = cfaColor;
        if(cfaColor == CfaColor::Red && info.redOriginX < 0)
        {
          info.redOriginX = x;
          info.redOriginY = y;
        }
      }
    }
  }

  if(info.hasRawImage)
  {
    const RawImage rawImage = rawImageFromLibRaw(processor);
    const auto [minIt, maxIt] = std::minmax_element(rawImage.samples().begin(), rawImage.samples().end());
    info.rawImageConverted = true;
    info.rawSampleMin = *minIt;
    info.rawSampleMax = *maxIt;
    for(int y = 0; y < 2 && y < rawImage.height(); ++y)
    {
      for(int x = 0; x < 2 && x < rawImage.width(); ++x)
      {
        info.firstCfaSamples[y][x] = rawImage.sample(x, y);
      }
    }
  }

  return info;
}

RawImage LibRawDecoder::decodeRawImage(const std::filesystem::path &path) const
{
  LibRaw processor;
  throwOnLibRawError(processor.open_file(path.string().c_str()), "LibRaw open_file");
  throwOnLibRawError(processor.unpack(), "LibRaw unpack");
  return rawImageFromLibRaw(processor);
}

}
