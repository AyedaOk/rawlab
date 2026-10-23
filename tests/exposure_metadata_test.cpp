#include "rawlab/dng/DngWriter.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/image/RawPreview.h"

#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <tiffio.h>

int main(int argc, char **argv)
{
  QTemporaryDir directory;
  if(!directory.isValid()) return 1;
  rawlab::raw::RawMetadata metadata;
  metadata.make = "RawLab";
  metadata.model = "Metadata Test";
  metadata.cfa.isBayer = true;
  metadata.cfa.cells[0][0] = rawlab::raw::CfaColor::Red;
  metadata.cfa.cells[0][1] = metadata.cfa.cells[1][0] = rawlab::raw::CfaColor::Green;
  metadata.cfa.cells[1][1] = rawlab::raw::CfaColor::Blue;
  metadata.activeArea = {0, 0, 64, 64};
  metadata.whiteLevel = 4095;
  metadata.calibrationIlluminant1 = 17;
  metadata.colorMatrix2 = std::array<float, 9>{.8f, -.1f, .2f, .1f, .9f, .1f, 0, .1f, 1};
  metadata.calibrationIlluminant2 = 21;
  metadata.forwardMatrix1 = std::array<float, 9>{.3f, .3f, .4f, .1f, .6f, .3f, 0, .1f, .8f};
  metadata.forwardMatrix2 = std::array<float, 9>{.4f, .4f, .2f, .1f, .8f, .1f, 0, .1f, .9f};
  metadata.defaultCropOrigin = std::array<float, 2>{3, 5};
  metadata.defaultCropSize = std::array<float, 2>{58, 54};
  rawlab::raw::LibRawDecoder decoder;
  const auto path = directory.filePath("roundtrip.dng").toStdString();
  for(const auto bias : {std::optional<double>{}, std::optional<double>{-1.0},
                         std::optional<double>{0.0}, std::optional<double>{1.0 / 3.0}})
  {
    metadata.exposureBiasEv = bias;
    metadata.xResolution = bias ? std::optional<double>{300} : std::nullopt;
    metadata.yResolution = metadata.xResolution;
    metadata.resolutionUnit = bias ? std::optional<std::uint16_t>{2} : std::nullopt;
    // Deliberately different from EXIF exposure compensation.
    metadata.baselineExposureEv = 0.5;
    rawlab::raw::RawImage image{64, 64, std::vector<float>(64 * 64, 1024), metadata};
    rawlab::dng::TiffDngWriter{}.writeCfaDng(path, image);
    const auto actual = decoder.readMetadata(path).metadata;
    const auto sameMatrix = [](const auto &a, const auto &b) {
      if(!a || !b) return false;
      for(std::size_t i = 0; i < a->size(); ++i)
        if(std::abs((*a)[i] - (*b)[i]) > 1e-6f) return false;
      return true;
    };
    if(!sameMatrix(actual.colorMatrix2, metadata.colorMatrix2)
       || !sameMatrix(actual.forwardMatrix1, metadata.forwardMatrix1)
       || !sameMatrix(actual.forwardMatrix2, metadata.forwardMatrix2)
       || actual.calibrationIlluminant2 != metadata.calibrationIlluminant2
       || actual.defaultCropOrigin != metadata.defaultCropOrigin
       || actual.defaultCropSize != metadata.defaultCropSize) return 1;
    const auto decoded = decoder.decodeRawImage(path);
    const auto area = decoded.displayArea();
    if(area.left != 3 || area.top != 5 || area.width != 58 || area.height != 54
       || decoded.samples() != image.samples()) return 1;
    const auto preview = rawlab::image::renderRawPreview(decoded, 128);
    if(preview.image.width != 58 || preview.image.height != 54
       || preview.geometry.rawArea.left != 3 || preview.geometry.rawArea.top != 5) return 1;
    if(actual.xResolution != metadata.xResolution || actual.yResolution != metadata.yResolution
       || actual.resolutionUnit != metadata.resolutionUnit) return 1;
    if(actual.exposureBiasEv.has_value() != bias.has_value()
       || (bias && std::abs(*bias - *actual.exposureBiasEv) > 1e-6)
       || !actual.baselineExposureEv || std::abs(*actual.baselineExposureEv - 0.5) > 1e-6
       || !actual.colorMatrix1Valid || actual.calibrationIlluminant1 != 17)
    {
      std::cerr << "Independent EXIF exposure bias / DNG baseline round trip failed\n";
      return 1;
    }
  }
  // Exercise every uint16 value and large predictor jumps through LibRaw's independent decoder.
  for(const int width : {256, 257})
  {
    auto full = metadata;
    full.activeArea = {0, 0, width, 256};
    full.defaultCropOrigin.reset();
    full.defaultCropSize.reset();
    std::vector<float> samples(width * 256);
    for(std::size_t i = 0; i < samples.size(); ++i)
      samples[i] = static_cast<std::uint16_t>(i * 40503u);
    rawlab::raw::RawImage image{width, 256, samples, full};
    rawlab::dng::TiffDngWriter{}.writeCfaDng(path, image);
    if(decoder.decodeRawImage(path).samples() != samples) return 1;
    TIFF *tif = TIFFOpen(path.c_str(), "r");
    if(!tif) return 1;
    std::uint16_t compression = 0;
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression);
    TIFFClose(tif);
    if(compression != COMPRESSION_JPEG) return 1;
  }
  // Optional user-owned RAW fixture: never bundle photographs without permission.
  if(argc > 1)
  {
    const auto source = decoder.decodeRawImage(argv[1]);
    if(!source.metadata().exposureBiasEv) return 1;
    rawlab::dng::TiffDngWriter{}.writeCfaDng(path, source);
    const auto result = decoder.readMetadata(path).metadata;
    if(!result.exposureBiasEv
       || std::abs(*result.exposureBiasEv - *source.metadata().exposureBiasEv) > 1e-6) return 1;
    std::cout << "RAW -> DNG exposure compensation: " << *result.exposureBiasEv << " EV\n";
  }
}
