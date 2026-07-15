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

std::vector<std::uint16_t> toUint16Cfa(const raw::RawImage &image)
{
  std::vector<std::uint16_t> cfa;
  cfa.reserve(image.samples().size());
  for(const float sample : image.samples())
  {
    const float clipped = std::clamp(sample, 0.0f, 65535.0f);
    cfa.push_back(static_cast<std::uint16_t>(std::lround(clipped)));
  }
  return cfa;
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
  return {
    dngColor(pattern.cells[0][0]),
    dngColor(pattern.cells[0][1]),
    dngColor(pattern.cells[1][0]),
    dngColor(pattern.cells[1][1]),
  };
}

void TiffDngWriter::writeCfaDng(const std::filesystem::path &path, const raw::RawImage &image) const
{
  // Adapted from darktable's GPL-3.0-or-later dt_imageio_dng_write_cfa_bayer()
  // in src/imageio/imageio_dng.c. This first RawLab slice keeps only the
  // isolated single-IFD uint16 Bayer CFA payload path; previews and EXIF are delayed.
  if(image.width() <= 0 || image.height() <= 0) throw std::invalid_argument{"Invalid DNG image size"};
  if(!image.metadata().cfa.isBayer) throw std::invalid_argument{"DNG writer requires Bayer CFA metadata"};

  TIFF *tif = TIFFOpen(path.string().c_str(), "wl");
  if(!tif) throw std::runtime_error{"Could not open DNG output for writing"};

  try
  {
    const raw::RawMetadata &metadata = image.metadata();
    const std::vector<std::uint16_t> cfa = toUint16Cfa(image);

    const std::uint8_t dngVersion[4] = { 1, 4, 0, 0 };
    const std::uint8_t dngBackwardVersion[4] = { 1, 4, 0, 0 };
    TIFFSetField(tif, TIFFTAG_DNGVERSION, dngVersion);
    TIFFSetField(tif, TIFFTAG_DNGBACKWARDVERSION, dngBackwardVersion);

    TIFFSetField(tif, TIFFTAG_MAKE, metadata.make.empty() ? "RawLab" : metadata.make.c_str());
    TIFFSetField(tif, TIFFTAG_MODEL, metadata.model.empty() ? "Synthetic Bayer CFA" : metadata.model.c_str());
    TIFFSetField(tif, TIFFTAG_UNIQUECAMERAMODEL, metadata.model.empty() ? "RawLab Synthetic Bayer CFA" : metadata.model.c_str());

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<std::uint32_t>(image.width()));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<std::uint32_t>(image.height()));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(16));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(1));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    const std::uint16_t cfaRepeatDim[2] = { 2, 2 };
    TIFFSetField(tif, TIFFTAG_CFAREPEATPATTERNDIM, cfaRepeatDim);
    const std::array<std::uint8_t, 4> cfaPattern = cfaPatternBytes(metadata.cfa);
    TIFFSetField(tif, TIFFTAG_CFAPATTERN, static_cast<std::uint32_t>(cfaPattern.size()), cfaPattern.data());
    const std::uint8_t cfaPlaneColor[3] = { 0, 1, 2 };
    TIFFSetField(tif, TIFFTAG_CFAPLANECOLOR, static_cast<std::uint32_t>(3), cfaPlaneColor);
    TIFFSetField(tif, TIFFTAG_CFALAYOUT, static_cast<std::uint16_t>(1));

    const std::uint16_t blackRepeatDim[2] = { 2, 2 };
    TIFFSetField(tif, TIFFTAG_BLACKLEVELREPEATDIM, blackRepeatDim);
    TIFFSetField(tif, TIFFTAG_BLACKLEVEL, static_cast<std::uint32_t>(metadata.blackLevel.size()), metadata.blackLevel.data());
    const std::uint32_t whiteLevel = static_cast<std::uint32_t>(std::max(metadata.whiteLevel, 1.0f));
    TIFFSetField(tif, TIFFTAG_WHITELEVEL, static_cast<std::uint32_t>(1), &whiteLevel);

    const int activeLeft = metadata.activeArea.width > 0 ? metadata.activeArea.left : 0;
    const int activeTop = metadata.activeArea.height > 0 ? metadata.activeArea.top : 0;
    const int activeWidth = metadata.activeArea.width > 0 ? metadata.activeArea.width : image.width();
    const int activeHeight = metadata.activeArea.height > 0 ? metadata.activeArea.height : image.height();
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

    TIFFSetField(tif, TIFFTAG_ASSHOTNEUTRAL, static_cast<std::uint32_t>(metadata.asShotNeutral.size()), metadata.asShotNeutral.data());
    TIFFSetField(tif, TIFFTAG_COLORMATRIX1, static_cast<std::uint32_t>(metadata.colorMatrix1.size()), metadata.colorMatrix1.data());
    TIFFSetField(tif, TIFFTAG_CALIBRATIONILLUMINANT1, static_cast<std::uint16_t>(21));

    for(int y = 0; y < image.height(); ++y)
    {
      const auto *row = cfa.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width());
      if(TIFFWriteScanline(tif, const_cast<std::uint16_t *>(row), static_cast<std::uint32_t>(y), 0) < 0)
      {
        throw std::runtime_error{"TIFFWriteScanline failed"};
      }
    }

    requireTiffOk(TIFFWriteDirectory(tif), "TIFFWriteDirectory");
  }
  catch(...)
  {
    TIFFClose(tif);
    throw;
  }

  TIFFClose(tif);
}

}
