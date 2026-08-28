#include "rawlab/dng/DngWriter.h"

#include "rawlab/raw/RawInspectInfo.h"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef TIFFTAG_PREVIEWCOLORSPACE
#define TIFFTAG_PREVIEWCOLORSPACE 50970
#endif

namespace rawlab::dng {
namespace {

std::uint8_t dngColor(raw::CfaColor color)
{
  switch(color)
  {
    case raw::CfaColor::Red: return 0;
    case raw::CfaColor::Green: return 1;
    case raw::CfaColor::Blue: return 2;
    case raw::CfaColor::Unknown: break;
  }
  throw std::invalid_argument{"Unsupported CFA color for DNG export"};
}

std::vector<std::uint16_t> toUint16Cfa(const raw::RawImage &image, const float whiteLevel)
{
  std::vector<std::uint16_t> cfa;
  cfa.reserve(image.samples().size());
  for(const float sample : image.samples())
  {
    if(!std::isfinite(sample)) throw std::invalid_argument{"DNG CFA contains a non-finite sample"};
    const float clipped = std::clamp(sample, 0.0f, std::min(whiteLevel, 65535.0f));
    cfa.push_back(static_cast<std::uint16_t>(std::lround(clipped)));
  }
  return cfa;
}

bool validColorMetadata(const raw::RawMetadata &metadata)
{
  if(!metadata.colorMatrix1Valid || !metadata.asShotNeutralValid) return false;
  if(!std::all_of(metadata.colorMatrix1.begin(), metadata.colorMatrix1.end(),
                  [](const float value) { return std::isfinite(value); })) return false;
  if(!std::all_of(metadata.asShotNeutral.begin(), metadata.asShotNeutral.end(),
                  [](const float value) { return std::isfinite(value) && value > 0.0f; })) return false;
  const auto &m = metadata.colorMatrix1;
  const float determinant = m[0] * (m[4] * m[8] - m[5] * m[7])
                            - m[1] * (m[3] * m[8] - m[5] * m[6])
                            + m[2] * (m[3] * m[7] - m[4] * m[6]);
  return std::isfinite(determinant) && std::abs(determinant) >= 1.0e-8f;
}

void registerDngFields(TIFF *tif)
{
  static const TIFFFieldInfo fields[] = {
    {TIFFTAG_PREVIEWCOLORSPACE, 1, 1, TIFF_LONG, FIELD_CUSTOM, true, false,
     const_cast<char *>("PreviewColorSpace")},
    {TIFFTAG_CFAREPEATPATTERNDIM, 2, 2, TIFF_SHORT, FIELD_CUSTOM, true, false,
     const_cast<char *>("CFARepeatPatternDim")},
    {TIFFTAG_CFAPATTERN, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_BYTE, FIELD_CUSTOM, true, true,
     const_cast<char *>("CFAPattern")},
  };
  TIFFMergeFieldInfo(tif, fields, static_cast<std::uint32_t>(std::size(fields)));
}

void setSharedMetadata(TIFF *tif, const raw::RawMetadata &metadata)
{
  const std::uint8_t dngVersion[4] = {1, 4, 0, 0};
  const std::uint8_t dngBackwardVersion[4] = {1, 2, 0, 0};
  TIFFSetField(tif, TIFFTAG_DNGVERSION, dngVersion);
  TIFFSetField(tif, TIFFTAG_DNGBACKWARDVERSION, dngBackwardVersion);
  TIFFSetField(tif, TIFFTAG_MAKE, metadata.make.empty() ? "RawLab" : metadata.make.c_str());
  TIFFSetField(tif, TIFFTAG_MODEL,
               metadata.model.empty() ? "Synthetic Bayer CFA" : metadata.model.c_str());
  TIFFSetField(tif, TIFFTAG_UNIQUECAMERAMODEL,
               metadata.model.empty() ? "RawLab Synthetic Bayer CFA" : metadata.model.c_str());
  TIFFSetField(tif, TIFFTAG_ASSHOTNEUTRAL,
               static_cast<std::uint32_t>(metadata.asShotNeutral.size()),
               metadata.asShotNeutral.data());
  TIFFSetField(tif, TIFFTAG_COLORMATRIX1,
               static_cast<std::uint32_t>(metadata.colorMatrix1.size()),
               metadata.colorMatrix1.data());
  TIFFSetField(tif, TIFFTAG_CALIBRATIONILLUMINANT1, static_cast<std::uint16_t>(21));
  TIFFSetField(tif, TIFFTAG_BASELINEEXPOSURE, 0.0f);
}

void setJpegPreviewFields(TIFF *tif, const JpegPreview &preview,
                          const raw::RawMetadata *metadata,
                          const std::uint16_t subIfdCount)
{
  TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(preview.width));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(preview.height));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(8));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(3));
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<std::uint32_t>(preview.height));
  TIFFSetField(tif, TIFFTAG_PREVIEWCOLORSPACE, static_cast<std::uint32_t>(2));
  if(metadata) setSharedMetadata(tif, *metadata);
  std::array<toff_t, 2> offsets{};
  if(subIfdCount > 0) TIFFSetField(tif, TIFFTAG_SUBIFD, subIfdCount, offsets.data());
}

void writeJpegStrip(TIFF *tif, const JpegPreview &preview)
{
  if(TIFFWriteRawStrip(tif, 0, const_cast<std::uint8_t *>(preview.data.data()),
                       static_cast<tmsize_t>(preview.data.size())) < 0)
  {
    throw std::runtime_error{"TIFFWriteRawStrip failed for DNG preview"};
  }
}

bool validPreview(const JpegPreview &preview)
{
  if(preview.width <= 0 || preview.height <= 0 || preview.data.size() < 10
     || preview.data[0] != 0xff || preview.data[1] != 0xd8) return false;
  std::size_t offset = 2;
  while(offset + 3 < preview.data.size())
  {
    if(preview.data[offset] != 0xff) { ++offset; continue; }
    while(offset < preview.data.size() && preview.data[offset] == 0xff) ++offset;
    if(offset >= preview.data.size()) break;
    const std::uint8_t marker = preview.data[offset++];
    if(marker == 0xd9 || marker == 0xda) break;
    if(marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if(offset + 1 >= preview.data.size()) break;
    const std::size_t length = (static_cast<std::size_t>(preview.data[offset]) << 8)
                               | preview.data[offset + 1];
    if(length < 2 || offset + length > preview.data.size()) break;
    const bool startOfFrame = marker >= 0xc0 && marker <= 0xcf
                              && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
    if(startOfFrame)
    {
      if(length < 8) return false;
      const int height = (static_cast<int>(preview.data[offset + 3]) << 8)
                         | preview.data[offset + 4];
      const int width = (static_cast<int>(preview.data[offset + 5]) << 8)
                        | preview.data[offset + 6];
      const int components = preview.data[offset + 7];
      return width == preview.width && height == preview.height && components == 3;
    }
    offset += length;
  }
  return false;
}

void requireTiffOk(const int ok, const char *operation)
{
  if(ok == 1) return;
  throw std::runtime_error{std::string{operation} + " failed"};
}

} // namespace

std::array<std::uint8_t, 4> cfaPatternBytes(const raw::CfaPattern &pattern)
{
  if(!pattern.isBayer) throw std::invalid_argument{"DNG CFA writer requires a Bayer pattern"};
  const std::array<std::uint8_t, 4> result{
    dngColor(pattern.cells[0][0]),
    dngColor(pattern.cells[0][1]),
    dngColor(pattern.cells[1][0]),
    dngColor(pattern.cells[1][1]),
  };
  const int red = static_cast<int>(std::count(result.begin(), result.end(), 0));
  const int green = static_cast<int>(std::count(result.begin(), result.end(), 1));
  const int blue = static_cast<int>(std::count(result.begin(), result.end(), 2));
  if(red != 1 || green != 2 || blue != 1)
  {
    throw std::invalid_argument{"DNG CFA writer requires one red, two green, and one blue site"};
  }
  return result;
}

void TiffDngWriter::writeCfaDng(const std::filesystem::path &path, const raw::RawImage &image,
                                const DngPreviews *previews) const
{
  // Adapted from darktable's GPL-3.0-or-later dt_imageio_dng_write_cfa_bayer()
  // in src/imageio/imageio_dng.c. This first RawLab slice keeps only the
  // isolated single-IFD uint16 Bayer CFA payload path; previews and EXIF are delayed.
  if(image.width() <= 0 || image.height() <= 0) throw std::invalid_argument{"Invalid DNG image size"};
  if(!image.metadata().cfa.isBayer) throw std::invalid_argument{"DNG writer requires Bayer CFA metadata"};
  if(!validColorMetadata(image.metadata())) throw std::invalid_argument{"Invalid DNG color metadata"};
  if(!std::isfinite(image.metadata().whiteLevel) || image.metadata().whiteLevel < 1.0f)
  {
    throw std::invalid_argument{"Invalid DNG white level"};
  }
  for(const float black : image.metadata().blackLevel)
  {
    if(!std::isfinite(black) || black < 0.0f || black >= image.metadata().whiteLevel)
    {
      throw std::invalid_argument{"Invalid DNG black level"};
    }
  }
  const bool canonical = previews && validPreview(previews->thumbnail)
                         && validPreview(previews->display);
  if(previews && !canonical) throw std::invalid_argument{"Incomplete DNG JPEG previews"};

  TIFF *tif = TIFFOpen(path.string().c_str(), "wl");
  if(!tif) throw std::runtime_error{"Could not open DNG output for writing"};
  registerDngFields(tif);

  try
  {
    const raw::RawMetadata &metadata = image.metadata();
    const float white = std::clamp(metadata.whiteLevel, 1.0f, 65535.0f);
    const std::vector<std::uint16_t> cfa = toUint16Cfa(image, white);
    raw::ActiveArea active = metadata.activeArea;
    if(active.width <= 0 || active.height <= 0) active = {0, 0, image.width(), image.height()};
    if(active.left < 0 || active.top < 0 || active.width <= 0 || active.height <= 0
       || active.left + active.width > image.width()
       || active.top + active.height > image.height())
    {
      throw std::invalid_argument{"DNG active area lies outside the CFA image"};
    }

    if(canonical)
    {
      setJpegPreviewFields(tif, previews->thumbnail, &metadata, 2);
      writeJpegStrip(tif, previews->thumbnail);
      requireTiffOk(TIFFWriteDirectory(tif), "DNG thumbnail directory");
      TIFFCreateDirectory(tif);
      registerDngFields(tif);
    }
    else setSharedMetadata(tif, metadata);

    TIFFSetField(tif, TIFFTAG_SUBFILETYPE, static_cast<std::uint32_t>(0));
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(image.width()));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(image.height()));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(16));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(1));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    std::uint16_t orientation = ORIENTATION_TOPLEFT;
    if(metadata.flip == 3) orientation = ORIENTATION_BOTRIGHT;
    else if(metadata.flip == 5) orientation = ORIENTATION_LEFTBOT;
    else if(metadata.flip == 6) orientation = ORIENTATION_RIGHTTOP;
    TIFFSetField(tif, TIFFTAG_ORIENTATION, orientation);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    const std::uint16_t cfaRepeatDim[2] = { 2, 2 };
    if(TIFFSetField(tif, TIFFTAG_CFAREPEATPATTERNDIM, cfaRepeatDim) != 1)
    {
      throw std::runtime_error{"Could not write DNG CFA repeat dimensions"};
    }
    const std::array<std::uint8_t, 4> nativePattern = cfaPatternBytes(metadata.cfa);
    std::array<std::uint8_t, 4> cfaPattern{};
    const int phaseX = active.left & 1;
    const int phaseY = active.top & 1;
    for(int y = 0; y < 2; ++y)
    {
      for(int x = 0; x < 2; ++x)
      {
        cfaPattern[static_cast<std::size_t>(y * 2 + x)]
          = nativePattern[static_cast<std::size_t>(((y + phaseY) & 1) * 2
                                                   + ((x + phaseX) & 1))];
      }
    }
    if(TIFFSetField(tif, TIFFTAG_CFAPATTERN, static_cast<std::uint32_t>(cfaPattern.size()),
                    cfaPattern.data()) != 1)
    {
      throw std::runtime_error{"Could not write DNG CFA pattern"};
    }
    const std::uint8_t cfaPlaneColor[3] = { 0, 1, 2 };
    TIFFSetField(tif, TIFFTAG_CFAPLANECOLOR, static_cast<std::uint32_t>(3), cfaPlaneColor);
    TIFFSetField(tif, TIFFTAG_CFALAYOUT, static_cast<std::uint16_t>(1));

    const std::uint16_t blackRepeatDim[2] = { 2, 2 };
    TIFFSetField(tif, TIFFTAG_BLACKLEVELREPEATDIM, blackRepeatDim);
    std::array<float, 4> blackLevel{};
    for(int y = 0; y < 2; ++y)
    {
      for(int x = 0; x < 2; ++x)
      {
        blackLevel[static_cast<std::size_t>(y * 2 + x)]
          = metadata.blackLevel[static_cast<std::size_t>(((y + phaseY) & 1) * 2
                                                         + ((x + phaseX) & 1))];
      }
    }
    TIFFSetField(tif, TIFFTAG_BLACKLEVEL, static_cast<std::uint32_t>(blackLevel.size()), blackLevel.data());
    const std::uint32_t whiteLevel = static_cast<std::uint32_t>(std::lround(white));
    TIFFSetField(tif, TIFFTAG_WHITELEVEL, static_cast<std::uint32_t>(1), &whiteLevel);

    const int activeLeft = active.left;
    const int activeTop = active.top;
    const int activeWidth = active.width;
    const int activeHeight = active.height;
    const std::uint32_t activeArea[4] = {
      static_cast<std::uint32_t>(activeTop),
      static_cast<std::uint32_t>(activeLeft),
      static_cast<std::uint32_t>(activeTop + activeHeight),
      static_cast<std::uint32_t>(activeLeft + activeWidth),
    };
    TIFFSetField(tif, TIFFTAG_ACTIVEAREA, activeArea);
    const float defaultScale[2] = { 1.0f, 1.0f };
    const float defaultCropOrigin[2] = { 0.0f, 0.0f };
    const float defaultCropSize[2] = { static_cast<float>(activeWidth), static_cast<float>(activeHeight) };
    TIFFSetField(tif, TIFFTAG_DEFAULTSCALE, defaultScale);
    TIFFSetField(tif, TIFFTAG_DEFAULTCROPORIGIN, defaultCropOrigin);
    TIFFSetField(tif, TIFFTAG_DEFAULTCROPSIZE, defaultCropSize);

    for(int y = 0; y < image.height(); ++y)
    {
      const auto *row = cfa.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width());
      if(TIFFWriteScanline(tif, const_cast<std::uint16_t *>(row), static_cast<std::uint32_t>(y), 0) < 0)
      {
        throw std::runtime_error{"TIFFWriteScanline failed"};
      }
    }

    requireTiffOk(TIFFWriteDirectory(tif), "TIFFWriteDirectory");
    if(canonical)
    {
      TIFFCreateDirectory(tif);
      registerDngFields(tif);
      setJpegPreviewFields(tif, previews->display, nullptr, 0);
      writeJpegStrip(tif, previews->display);
      requireTiffOk(TIFFWriteDirectory(tif), "DNG display preview directory");
    }
  }
  catch(...)
  {
    TIFFClose(tif);
    throw;
  }

  TIFFClose(tif);
}

}
