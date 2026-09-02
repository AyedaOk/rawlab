#include "rawlab/raw/LibRawDecoder.h"

#include <libraw/libraw.h>
#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace rawlab::raw {
namespace {

std::array<float, 9> inverse3x3(const std::array<float, 9> &matrix, bool &valid)
{
  const float determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7])
                            - matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6])
                            + matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
  if(std::abs(determinant) < 1e-8f)
  {
    valid = false;
    return {
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f,
    };
  }
  valid = true;
  const float reciprocal = 1.0f / determinant;
  return {
    (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * reciprocal,
    (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * reciprocal,
    (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * reciprocal,
    (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * reciprocal,
    (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * reciprocal,
    (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * reciprocal,
    (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * reciprocal,
    (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * reciprocal,
    (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * reciprocal,
  };
}

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

template<std::size_t Size>
std::string boundedString(const char (&value)[Size])
{
  const auto end = std::find(value, value + Size, '\0');
  return std::string{value, end};
}

std::optional<double> readExposureBias(const std::filesystem::path &path)
{
  TIFF *tif = TIFFOpen(path.string().c_str(), "r");
  if(!tif) return std::nullopt;

  std::optional<double> result;
  do
  {
    const tdir_t imageDirectory = TIFFCurrentDirectory(tif);
    toff_t exifDirectory = 0;
    if(TIFFGetField(tif, TIFFTAG_EXIFIFD, &exifDirectory) == 1
       && exifDirectory != 0 && TIFFReadEXIFDirectory(tif, exifDirectory) == 1)
    {
      float bias = 0.0f;
      if(TIFFGetField(tif, EXIFTAG_EXPOSUREBIASVALUE, &bias) == 1
         && std::isfinite(bias))
      {
        result = static_cast<double>(bias);
        break;
      }
    }
    // EXIF directories are metadata-only and do not have ImageLength.
    // Restore the image directory before continuing the IFD scan.
    TIFFSetDirectory(tif, imageDirectory);
  } while(TIFFReadDirectory(tif) == 1);
  TIFFClose(tif);
  return result;
}

void throwOnLibRawError(const int error, const char *operation)
{
  if(error == LIBRAW_SUCCESS) return;
  throw std::runtime_error(std::string{operation} + " failed: "
                           + libraw_strerror(error));
}

RawMetadata metadataFromLibRaw(LibRaw &processor, const std::filesystem::path &path)
{
  const libraw_iparams_t &id = processor.imgdata.idata;
  const libraw_image_sizes_t &sizes = processor.imgdata.sizes;
  const libraw_colordata_t &color = processor.imgdata.color;
  const libraw_imgother_t &other = processor.imgdata.other;
  const libraw_lensinfo_t &lens = processor.imgdata.lens;
  const libraw_shootinginfo_t &shooting = processor.imgdata.shootinginfo;

  RawMetadata metadata;
  metadata.make = id.make;
  metadata.model = id.model;
  const auto positiveValue = [](const float value) -> std::optional<double> {
    return std::isfinite(value) && value > 0.0f
      ? std::optional<double>{static_cast<double>(value)} : std::nullopt;
  };
  metadata.iso = positiveValue(other.iso_speed);
  metadata.aperture = positiveValue(other.aperture);
  metadata.exposureTimeSeconds = positiveValue(other.shutter);
  metadata.exposureBiasEv = readExposureBias(path);
  metadata.focalLengthMm = positiveValue(other.focal_len);
  metadata.focalLength35mm = positiveValue(static_cast<float>(lens.FocalLengthIn35mmFormat));
  if(other.timestamp > 0) metadata.captureTimestamp = static_cast<std::int64_t>(other.timestamp);
  metadata.artist = boundedString(other.artist);
  metadata.description = boundedString(other.desc);
  metadata.bodySerial = boundedString(shooting.BodySerial);
  metadata.lensMake = boundedString(lens.LensMake);
  metadata.lensModel = boundedString(lens.Lens);
  metadata.lensSerial = boundedString(lens.LensSerial);
  const std::array<double, 4> lensSpecification{
    lens.MinFocal, lens.MaxFocal, lens.MaxAp4MinFocal, lens.MaxAp4MaxFocal,
  };
  if(std::all_of(lensSpecification.begin(), lensSpecification.end(),
                 [](const double value) { return std::isfinite(value) && value > 0.0; }))
  {
    metadata.lensSpecification = lensSpecification;
  }
  metadata.activeArea = {
    sizes.left_margin,
    sizes.top_margin,
    sizes.width,
    sizes.height,
  };
  metadata.flip = sizes.flip;
  metadata.whiteLevel = static_cast<float>(color.maximum);
  float neutralMax = 0.0f;
  metadata.asShotNeutralValid = true;
  for(std::size_t c = 0; c < metadata.asShotNeutral.size(); ++c)
  {
    if(!std::isfinite(color.cam_mul[c]) || color.cam_mul[c] <= 0.0f)
    {
      metadata.asShotNeutralValid = false;
      metadata.asShotNeutral[c] = 1.0f;
    }
    else metadata.asShotNeutral[c] = 1.0f / color.cam_mul[c];
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
  metadata.cameraToXyz = inverse3x3(metadata.colorMatrix1, metadata.colorMatrix1Valid);
  for(int rgb = 0; rgb < 3; ++rgb)
  {
    for(int camera = 0; camera < 3; ++camera)
    {
      metadata.cameraToSrgb[static_cast<std::size_t>(rgb * 3 + camera)]
        = color.rgb_cam[rgb][camera];
    }
  }
  const bool hasSeparateBlack = color.cblack[0] != 0 || color.cblack[1] != 0
                                || color.cblack[2] != 0 || color.cblack[3] != 0;
  for(int y = 0; y < 2; ++y)
  {
    for(int x = 0; x < 2; ++x)
    {
      const int channel = processor.COLOR(y, x);
      metadata.blackLevel[static_cast<std::size_t>(siteIndex(x, y))]
        = static_cast<float>(color.black)
          + (hasSeparateBlack ? static_cast<float>(color.cblack[channel]) : 0.0f);
    }
  }

  if(id.filters > 999u && id.colors == 3)
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

RawImage rawImageFromLibRaw(LibRaw &processor, const std::filesystem::path &path)
{
  const libraw_image_sizes_t &sizes = processor.imgdata.sizes;
  const libraw_rawdata_t &rawdata = processor.imgdata.rawdata;
  if(!rawdata.raw_image)
  {
    throw std::runtime_error{"LibRaw did not expose a raw_image buffer"};
  }

  const std::size_t rowBytes = static_cast<std::size_t>(sizes.raw_width) * sizeof(std::uint16_t);
  if(sizes.raw_pitch < rowBytes)
  {
    throw std::runtime_error{"LibRaw raw pitch is smaller than the image row"};
  }
  const std::size_t sampleCount = static_cast<std::size_t>(sizes.raw_width)
                                  * static_cast<std::size_t>(sizes.raw_height);
  std::vector<float> samples;
  samples.reserve(sampleCount);
  const auto *bytes = reinterpret_cast<const std::byte *>(rawdata.raw_image);
  for(int y = 0; y < sizes.raw_height; ++y)
  {
    const auto *row = reinterpret_cast<const std::uint16_t *>(
      bytes + static_cast<std::size_t>(y) * sizes.raw_pitch);
    for(int x = 0; x < sizes.raw_width; ++x) samples.push_back(static_cast<float>(row[x]));
  }

  return RawImage{sizes.raw_width, sizes.raw_height, std::move(samples),
                  metadataFromLibRaw(processor, path)};
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
  const RawMetadata metadata = metadataFromLibRaw(processor, path);
  info.iso = metadata.iso;
  info.aperture = metadata.aperture;
  info.exposureTimeSeconds = metadata.exposureTimeSeconds;
  info.exposureBiasEv = metadata.exposureBiasEv;
  info.focalLengthMm = metadata.focalLengthMm;
  info.captureTimestamp = metadata.captureTimestamp;
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
    const RawImage rawImage = rawImageFromLibRaw(processor, path);
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

RawFileMetadata LibRawDecoder::readMetadata(const std::filesystem::path &path) const
{
  LibRaw processor;
  throwOnLibRawError(processor.open_file(path.string().c_str()), "LibRaw open_file");
  return {metadataFromLibRaw(processor, path), processor.imgdata.sizes.raw_width,
          processor.imgdata.sizes.raw_height};
}

RawImage LibRawDecoder::decodeRawImage(const std::filesystem::path &path) const
{
  LibRaw processor;
  throwOnLibRawError(processor.open_file(path.string().c_str()), "LibRaw open_file");
  throwOnLibRawError(processor.unpack(), "LibRaw unpack");
  return rawImageFromLibRaw(processor, path);
}

}
