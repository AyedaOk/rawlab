#include "rawlab/core/Version.h"
#include "rawlab/ai/RawForgeAdapter.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/image/AmazeDemosaic.h"
#include "rawlab/image/RawPreview.h"
#include "rawlab/image/LensCorrection.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/pipeline/LayerComposition.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <tiffio.h>

namespace {

bool nearlyEqual(const float a, const float b)
{
  return std::fabs(a - b) < 0.0001f;
}

rawlab::raw::CfaPattern makeRggb()
{
  rawlab::raw::CfaPattern pattern;
  pattern.isBayer = true;
  pattern.cells[0][0] = rawlab::raw::CfaColor::Red;
  pattern.cells[0][1] = rawlab::raw::CfaColor::Green;
  pattern.cells[1][0] = rawlab::raw::CfaColor::Green;
  pattern.cells[1][1] = rawlab::raw::CfaColor::Blue;
  return pattern;
}

rawlab::raw::CfaPattern makeBggr()
{
  rawlab::raw::CfaPattern pattern;
  pattern.isBayer = true;
  pattern.cells[0][0] = rawlab::raw::CfaColor::Blue;
  pattern.cells[0][1] = rawlab::raw::CfaColor::Green;
  pattern.cells[1][0] = rawlab::raw::CfaColor::Green;
  pattern.cells[1][1] = rawlab::raw::CfaColor::Red;
  return pattern;
}

rawlab::raw::CfaPattern makeGrbg()
{
  rawlab::raw::CfaPattern pattern = makeRggb();
  pattern.cells[0][0] = rawlab::raw::CfaColor::Green;
  pattern.cells[0][1] = rawlab::raw::CfaColor::Red;
  pattern.cells[1][0] = rawlab::raw::CfaColor::Blue;
  pattern.cells[1][1] = rawlab::raw::CfaColor::Green;
  return pattern;
}

rawlab::raw::CfaPattern makeGbrg()
{
  rawlab::raw::CfaPattern pattern = makeRggb();
  pattern.cells[0][0] = rawlab::raw::CfaColor::Green;
  pattern.cells[0][1] = rawlab::raw::CfaColor::Blue;
  pattern.cells[1][0] = rawlab::raw::CfaColor::Red;
  pattern.cells[1][1] = rawlab::raw::CfaColor::Green;
  return pattern;
}

rawlab::raw::RawImage makeSyntheticRggbImage()
{
  rawlab::raw::RawMetadata metadata;
  metadata.cfa = makeRggb();
  metadata.activeArea = { 0, 0, 4, 4 };
  metadata.blackLevel = { 0.0f, 0.0f, 0.0f, 0.0f };
  metadata.whiteLevel = 16.0f;
  metadata.iso = 640.0;
  metadata.aperture = 2.8;
  metadata.exposureTimeSeconds = 1.0 / 125.0;
  metadata.exposureBiasEv = -0.3;
  metadata.focalLengthMm = 50.0;
  metadata.focalLength35mm = 75.0;
  metadata.captureTimestamp = 1'700'000'000;
  metadata.artist = "RawLab Artist";
  metadata.description = "RawLab EXIF test";
  metadata.bodySerial = "BODY-123";
  metadata.lensMake = "Test Optics";
  metadata.lensModel = "Test Lens 50mm";
  metadata.lensSerial = "LENS-456";
  metadata.lensSpecification = std::array<double, 4>{50.0, 50.0, 2.8, 2.8};
  return rawlab::raw::RawImage{
    4,
    4,
    std::vector<float>{
      1.0f,  2.0f,  3.0f,  4.0f,
      5.0f,  6.0f,  7.0f,  8.0f,
      9.0f,  10.0f, 11.0f, 12.0f,
      13.0f, 14.0f, 15.0f, 16.0f,
    },
    metadata,
  };
}

rawlab::raw::RawImage makeFlatPreviewImage(
  const rawlab::raw::CfaPattern &pattern,
  const std::array<float, 3> asShotNeutral = { 1.0f, 1.0f, 1.0f })
{
  rawlab::raw::RawMetadata metadata;
  metadata.cfa = pattern;
  metadata.activeArea = { 0, 0, 16, 16 };
  metadata.blackLevel = { 10.0f, 12.0f, 14.0f, 16.0f };
  metadata.whiteLevel = 1024.0f;
  metadata.asShotNeutral = asShotNeutral;
  metadata.cameraToSrgb = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
  std::vector<float> samples(16 * 16);
  for(int y = 0; y < 16; ++y)
    for(int x = 0; x < 16; ++x)
    {
      const int site = rawlab::raw::siteIndex(x, y);
      const float black = metadata.blackLevel[static_cast<std::size_t>(site)];
       const int channel = pattern.cells[y & 1][x & 1] == rawlab::raw::CfaColor::Red ? 0
         : pattern.cells[y & 1][x & 1] == rawlab::raw::CfaColor::Green ? 1 : 2;
       samples[static_cast<std::size_t>(y * 16 + x)] =
         black + 0.4f * asShotNeutral[static_cast<std::size_t>(channel)]
                  * (metadata.whiteLevel - black);
    }
  return rawlab::raw::RawImage{16, 16, std::move(samples), metadata};
}

} // namespace

int main()
{
  const char *stage = "CFA utilities";
  struct Progress
  {
    const char *&stage;
    ~Progress() { if(stage) std::fprintf(stderr, "Smoke test stopped at: %s\n", stage); }
  } progress{stage};
  if(rawlab::core::version() != std::string_view{"0.2.3"}) return 1;

  const rawlab::raw::CfaPattern rggb = makeRggb();
  if(rawlab::raw::toString(rggb) != "RGGB") return 1;
  if(rawlab::dng::cfaPatternBytes(rggb) != std::array<std::uint8_t, 4>{ 0, 1, 1, 2 }) return 1;
  if(rawlab::raw::colorAt(rggb, 0, 0) != rawlab::raw::CfaColor::Red) return 1;
  if(rawlab::raw::colorAt(rggb, 1, 0) != rawlab::raw::CfaColor::Green) return 1;
  if(rawlab::raw::colorAt(rggb, 1, 1) != rawlab::raw::CfaColor::Blue) return 1;
  int redX = -1;
  int redY = -1;
  if(!rawlab::raw::redOrigin(rggb, redX, redY) || redX != 0 || redY != 0) return 1;

  const rawlab::raw::CfaPattern bggr = makeBggr();
  if(rawlab::raw::toString(bggr) != "BGGR") return 1;
  if(!rawlab::raw::redOrigin(bggr, redX, redY) || redX != 1 || redY != 1) return 1;

  if(!nearlyEqual(rawlab::raw::normalizeRawSample(6.0f, 2.0f, 10.0f), 0.5f)) return 1;
  if(!nearlyEqual(rawlab::raw::denormalizeRawSample(0.5f, 2.0f, 10.0f), 6.0f)) return 1;

  stage = "Fast preview";
  const rawlab::raw::RawImage image = makeSyntheticRggbImage();
#ifdef RAWLAB_HAS_LENSFUN
  stage = "Lensfun profile matching";
  auto opticsMetadata = image.metadata();
  opticsMetadata.make = "Nikon Corporation";
  opticsMetadata.model = "Nikon D750";
  opticsMetadata.lensMake = "Nikon";
  opticsMetadata.lensModel = "Nikon AF-S Nikkor 16-35mm f/4G ED VR";
  opticsMetadata.focalLengthMm = 16.0;
  opticsMetadata.aperture = 4.0;
  const auto optics = rawlab::image::opticsStatus(opticsMetadata, 64, 64);
  if(!optics.profileMatched || !optics.chromaticAberrationAvailable
     || !optics.lensCorrectionAvailable) return 1;
  auto opticsImage = makeFlatPreviewImage(rggb);
  opticsImage.metadata() = opticsMetadata;
  opticsImage.metadata().activeArea = {0, 0, opticsImage.width(), opticsImage.height()};
  const auto corrected = rawlab::image::renderLinearRgb(opticsImage, {true, true});
  if(corrected.width != opticsImage.width() || corrected.height != opticsImage.height()
      || corrected.pixels.size() != static_cast<std::size_t>(corrected.width * corrected.height * 3)
      || !std::all_of(corrected.pixels.begin(), corrected.pixels.end(), [](float value) {
        return std::isfinite(value) && value > 0.0001f;
      })) return 1;
#endif
  // A flat, neutral field must stay neutral after low-resolution Fast PPG.
  // Per-site black levels make this also exercise normalization before CFA interpolation.
  for(const rawlab::raw::CfaPattern pattern : { rggb, bggr })
  {
    const auto linearPreview = rawlab::image::renderLinearRawPreview(
      makeFlatPreviewImage(pattern), 8, rawlab::image::PreviewQuality::Fast);
    if(linearPreview.width != 8 || linearPreview.height != 8
       || linearPreview.pixels.size() != 8U * 8U * 3U
       || !std::all_of(linearPreview.pixels.begin(), linearPreview.pixels.end(), [](float value) {
         return std::isfinite(value);
       })) return 1;
    const rawlab::image::RawPreview preview = rawlab::image::renderRawPreview(
      makeFlatPreviewImage(pattern), 8, rawlab::image::PreviewQuality::Fast);
    if(preview.image.width != 8 || preview.image.height != 8)
    {
      std::fprintf(stderr, "Unexpected Fast preview dimensions: %d x %d\n",
                   preview.image.width, preview.image.height);
      return 1;
    }
    for(std::size_t i = 0; i < preview.image.pixels.size(); i += 3)
    {
      const std::uint8_t red = preview.image.pixels[i];
      const std::uint8_t green = preview.image.pixels[i + 1];
      const std::uint8_t blue = preview.image.pixels[i + 2];
      if(red < 165 || green < 165 || blue < 165)
      {
        std::fprintf(stderr, "Flat preview too dark: %d %d %d\n", red, green, blue);
        return 1;
      }
      if(std::abs(static_cast<int>(red) - static_cast<int>(green)) > 1
         || std::abs(static_cast<int>(green) - static_cast<int>(blue)) > 1) return 1;
    }
    const auto highPreview = rawlab::image::renderRawPreview(
      makeFlatPreviewImage(pattern), 8, rawlab::image::PreviewQuality::High);
    for(std::size_t i = 0; i < highPreview.image.pixels.size(); i += 3)
      if(std::abs(static_cast<int>(highPreview.image.pixels[i])
                  - static_cast<int>(highPreview.image.pixels[i + 1])) > 1
         || std::abs(static_cast<int>(highPreview.image.pixels[i + 1])
                     - static_cast<int>(highPreview.image.pixels[i + 2])) > 1) return 1;
  }
  stage = "normal sharpening";
  // Strong reduction follows the half-size preview path even when High Quality is selected.
  auto reducedSource = makeFlatPreviewImage(rggb);
  for(int y = 0; y < 16; ++y)
    for(int x = 0; x < 16; ++x)
      reducedSource.setSample(x, y, 100.0f + static_cast<float>((x * 37 + y * 59) % 700));
  for(int flip : {0, 3, 5, 6})
  {
    reducedSource.metadata().flip = flip;
    const auto reduced = rawlab::image::renderRawPreview(reducedSource, 1, rawlab::image::PreviewQuality::High);
    const auto reducedFast = rawlab::image::renderRawPreview(reducedSource, 1, rawlab::image::PreviewQuality::Fast);
    if(reduced.image.pixels != reducedFast.image.pixels) return 1;
  }
  const rawlab::raw::RawImage unchanged =
    rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(image, 0.0f);
  if(unchanged.samples() != image.samples()) return 1;
  const rawlab::raw::RawImage sharpened =
    rawlab::pipeline::RawProcessingPipeline{}.runNormalSharpen(image, 1.0f);
  if(nearlyEqual(sharpened.sample(1, 1), image.sample(1, 1)))
  {
    std::fprintf(stderr, "Sharpening test did not change the chosen sample\n");
    return 1;
  }
  stage = "EXIF round trip";
  const std::filesystem::path exifDngPath = std::filesystem::temp_directory_path()
                                                  / "rawlab-exif-test.dng";
  rawlab::dng::TiffDngWriter{}.writeCfaDng(exifDngPath, image);
  TIFF *exifTiff = TIFFOpen(exifDngPath.string().c_str(), "r");
  if(!exifTiff) return 1;
  char *artist = nullptr;
  char *description = nullptr;
  float baselineExposure = 0.0f;
  toff_t exifOffset = 0;
  const bool hasBaselineMetadata = TIFFGetField(exifTiff, TIFFTAG_ARTIST, &artist) == 1
                                   && TIFFGetField(exifTiff, TIFFTAG_IMAGEDESCRIPTION,
                                                    &description) == 1
                                   && TIFFGetField(exifTiff, TIFFTAG_BASELINEEXPOSURE,
                                                  &baselineExposure) == 1
                                   && TIFFGetField(exifTiff, TIFFTAG_EXIFIFD, &exifOffset) == 1
                                   && artist && description && exifOffset != 0
                                   && std::string_view{artist} == "RawLab Artist"
                                   && std::string_view{description} == "RawLab EXIF test"
                                   && nearlyEqual(baselineExposure, 0.0f);
  if(!hasBaselineMetadata || TIFFReadEXIFDirectory(exifTiff, exifOffset) != 1)
  {
    std::fprintf(stderr, "Missing baseline metadata (baseline=%g)\n", baselineExposure);
    TIFFClose(exifTiff);
    return 1;
  }
  std::uint8_t *exifVersion = nullptr;
  float exposureTime = 0.0f;
  float exposureBias = 0.0f;
  float aperture = 0.0f;
  float focalLength = 0.0f;
  std::uint16_t isoCount = 0;
  std::uint16_t *iso = nullptr;
  char *captureTime = nullptr;
  std::uint16_t focalLength35mm = 0;
  char *bodySerial = nullptr;
  char *lensMake = nullptr;
  char *lensModel = nullptr;
  char *lensSerial = nullptr;
  float *lensSpecification = nullptr;
  const bool hasExifMetadata = TIFFGetField(exifTiff, EXIFTAG_EXIFVERSION, &exifVersion) == 1
                                && TIFFGetField(exifTiff, EXIFTAG_EXPOSURETIME,
                                                &exposureTime) == 1
                                && TIFFGetField(exifTiff, EXIFTAG_EXPOSUREBIASVALUE,
                                                &exposureBias) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_FNUMBER, &aperture) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_ISOSPEEDRATINGS,
                                               &isoCount, &iso) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_DATETIMEORIGINAL,
                                               &captureTime) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_FOCALLENGTH,
                                               &focalLength) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_FOCALLENGTHIN35MMFILM,
                                               &focalLength35mm) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_BODYSERIALNUMBER,
                                               &bodySerial) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_LENSMAKE, &lensMake) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_LENSMODEL, &lensModel) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_LENSSERIALNUMBER,
                                               &lensSerial) == 1
                               && TIFFGetField(exifTiff, EXIFTAG_LENSSPECIFICATION,
                                               &lensSpecification) == 1;
  const bool validExifMetadata = hasExifMetadata && exifVersion && iso && captureTime
                                 && bodySerial && lensMake && lensModel && lensSerial
                                 && lensSpecification
                                 && std::string_view{reinterpret_cast<char *>(exifVersion), 4}
                                  == "0232"
                                  && nearlyEqual(exposureTime, 1.0f / 125.0f)
                                  && nearlyEqual(exposureBias, -0.3f)
                                 && nearlyEqual(aperture, 2.8f) && isoCount == 1 && iso[0] == 640
                                 && std::string_view{captureTime}.size() == 19
                                 && nearlyEqual(focalLength, 50.0f) && focalLength35mm == 75
                                 && std::string_view{bodySerial} == "BODY-123"
                                 && std::string_view{lensMake} == "Test Optics"
                                 && std::string_view{lensModel} == "Test Lens 50mm"
                                 && std::string_view{lensSerial} == "LENS-456"
                                 && nearlyEqual(lensSpecification[0], 50.0f)
                                 && nearlyEqual(lensSpecification[3], 2.8f);
  if(!validExifMetadata)
    std::fprintf(stderr, "EXIF found=%d exposure=%g bias=%g aperture=%g ISO=%u focal=%g lens=%g,%g\n",
                 hasExifMetadata, exposureTime, exposureBias, aperture, iso ? iso[0] : 0,
                 focalLength, lensSpecification ? lensSpecification[0] : -1,
                 lensSpecification ? lensSpecification[3] : -1);
  TIFFClose(exifTiff);
  std::filesystem::remove(exifDngPath);
  if(!validExifMetadata) return 1;
  stage = "Bayer processing";
  const rawlab::raw::PackedBayerImage packed = rawlab::raw::packBayerForceRggb(image);
  if(packed.width != 2 || packed.height != 2) return 1;

  const std::vector<float> expectedPacked{
    1.0f / 16.0f, 3.0f / 16.0f, 9.0f / 16.0f, 11.0f / 16.0f,
    2.0f / 16.0f, 4.0f / 16.0f, 10.0f / 16.0f, 12.0f / 16.0f,
    5.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 15.0f / 16.0f,
    6.0f / 16.0f, 8.0f / 16.0f, 14.0f / 16.0f, 16.0f / 16.0f,
  };
  if(packed.planes.size() != expectedPacked.size()) return 1;
  for(std::size_t i = 0; i < expectedPacked.size(); ++i)
  {
    if(!nearlyEqual(packed.planes[i], expectedPacked[i])) return 1;
  }

  std::vector<float> camRgb(3 * 16, 0.0f);
  for(std::size_t i = 0; i < 16; ++i)
  {
    camRgb[i] = 0.25f;
    camRgb[16 + i] = 0.5f;
    camRgb[32 + i] = 0.75f;
  }
  const std::vector<float> remosaiced = rawlab::raw::remosaicCamRgbToCfa(image, camRgb, 1.0f);
  const std::vector<float> expectedRemosaic{
    4.0f, 8.0f, 4.0f, 8.0f,
    8.0f, 12.0f, 8.0f, 12.0f,
    4.0f, 8.0f, 4.0f, 8.0f,
    8.0f, 12.0f, 8.0f, 12.0f,
  };
  for(std::size_t i = 0; i < expectedRemosaic.size(); ++i)
  {
    if(!nearlyEqual(remosaiced[i], expectedRemosaic[i])) return 1;
  }

  stage = "RawForge color conversion";
  rawlab::raw::RawMetadata rawForgeMetadata;
  rawForgeMetadata.cfa = rggb;
  rawForgeMetadata.whiteLevel = 1.0f;
  rawForgeMetadata.cameraToXyz = {
    1.0f, 0.1f, 0.0f,
    0.0f, 1.0f, 0.2f,
    0.3f, 0.0f, 1.0f,
  };
  std::vector<float> constantCfa(64);
  for(int y = 0; y < 8; ++y)
    for(int x = 0; x < 8; ++x)
    {
      const auto color = rawlab::raw::colorAt(rggb, x, y);
      constantCfa[static_cast<std::size_t>(y * 8 + x)] = color == rawlab::raw::CfaColor::Red
        ? 0.2f : color == rawlab::raw::CfaColor::Green ? 0.4f : 0.6f;
    }
  const rawlab::raw::RawImage rawForgeInput{8, 8, constantCfa, rawForgeMetadata};
  const std::vector<float> rec2020 = rawlab::ai::RawForgeAdapter::demosaicToLinearRec2020(
    rawForgeInput);
  const std::array<float, 3> expectedRec2020{
    1.7166512f * 0.24f - 0.3556708f * 0.52f - 0.2533663f * 0.66f,
    -0.6666844f * 0.24f + 1.6164812f * 0.52f + 0.0157685f * 0.66f,
    0.0176399f * 0.24f - 0.0427706f * 0.52f + 0.9421031f * 0.66f,
  };
  for(int channel = 0; channel < 3; ++channel)
    if(!nearlyEqual(rec2020[static_cast<std::size_t>(channel) * 64 + 27],
                    expectedRec2020[static_cast<std::size_t>(channel)])) return 1;
  const rawlab::raw::RawImage rawForgeRoundTrip =
    rawlab::ai::RawForgeAdapter::remosaicFromLinearRec2020(rawForgeInput, rec2020);
  for(std::size_t i = 0; i < constantCfa.size(); ++i)
    if(!nearlyEqual(rawForgeRoundTrip.samples()[i], constantCfa[i])) return 1;

  stage = "blend and highlights";
  rawlab::raw::RawMetadata blendMetadata = image.metadata();
  rawlab::raw::RawImage processed{
    4,
    4,
    std::vector<float>{
      11.0f, 12.0f, 13.0f, 14.0f,
      15.0f, 16.0f, 17.0f, 18.0f,
      19.0f, 20.0f, 21.0f, 22.0f,
      23.0f, 24.0f, 25.0f, 26.0f,
    },
    blendMetadata,
  };
  const rawlab::raw::RawImage blended = rawlab::raw::blendWithOriginal(image, processed, 0.25f);
  if(!nearlyEqual(blended.sample(0, 0), 3.5f)) return 1;
  if(!nearlyEqual(blended.sample(3, 3), 18.5f)) return 1;
  const rawlab::raw::RawImage blendZero = rawlab::raw::blendWithOriginal(image, processed, 0.0f);
  if(!nearlyEqual(blendZero.sample(2, 2), image.sample(2, 2))) return 1;
  const rawlab::raw::RawImage blendOne = rawlab::raw::blendWithOriginal(image, processed, 1.0f);
  if(!nearlyEqual(blendOne.sample(2, 2), processed.sample(2, 2))) return 1;

  rawlab::raw::RawMetadata highlightMetadata = image.metadata();
  highlightMetadata.whiteLevel = 16.0f;
  rawlab::raw::RawImage clippedOriginal{
    2,
    2,
    std::vector<float>{ 16.0f, 12.0f, 15.6f, 4.0f },
    highlightMetadata,
  };
  rawlab::raw::RawImage clippedProcessed{
    2,
    2,
    std::vector<float>{ 1.0f, 2.0f, 3.0f, 4.0f },
    highlightMetadata,
  };
  const rawlab::raw::RawImage preserved = rawlab::raw::preserveClippedHighlights(clippedOriginal, clippedProcessed);
  if(!nearlyEqual(preserved.sample(0, 0), 16.0f)) return 1;
  if(!nearlyEqual(preserved.sample(1, 0), 2.0f)) return 1;
  if(!nearlyEqual(preserved.sample(0, 1), 15.6f)) return 1;
  if(!nearlyEqual(preserved.sample(1, 1), 4.0f)) return 1;

  stage = "masks";
  const rawlab::pipeline::Mask mask{2, 2, std::vector<float>{ -1.0f, 0.5f, 1.0f, 2.0f }};
  if(!nearlyEqual(mask.sampleNearest(0, 0), 0.0f)) return 1;
  if(!nearlyEqual(mask.sampleNearest(1, 0), 0.5f)) return 1;
  if(!nearlyEqual(mask.sampleNearest(0, 1), 1.0f)) return 1;
  if(!nearlyEqual(mask.sampleNearest(1, 1), 1.0f)) return 1;

  const rawlab::pipeline::Mask gradient = rawlab::pipeline::horizontalGradientMask(4, 1);
  if(!nearlyEqual(gradient.sampleNearest(0, 0), 0.0f)) return 1;
  if(!nearlyEqual(gradient.sampleNearest(3, 0), 1.0f)) return 1;

  const rawlab::pipeline::Mask split = rawlab::pipeline::verticalSplitMask(4, 2);
  if(!nearlyEqual(split.sampleNearest(0, 0), 0.0f)) return 1;
  if(!nearlyEqual(split.sampleNearest(1, 1), 0.0f)) return 1;
  if(!nearlyEqual(split.sampleNearest(2, 0), 1.0f)) return 1;
  if(!nearlyEqual(split.sampleNearest(3, 1), 1.0f)) return 1;

  const rawlab::pipeline::Mask feathered = rawlab::pipeline::featherMask(
    rawlab::pipeline::verticalSplitMask(9, 3), 2.0f);
  if(!nearlyEqual(feathered.sampleNearest(1, 1), 0.0f)) return 1;
  if(!nearlyEqual(feathered.sampleNearest(3, 1), 0.375f)) return 1;
  if(!nearlyEqual(feathered.sampleNearest(4, 1), 0.625f)) return 1;
  if(!nearlyEqual(feathered.sampleNearest(6, 1), 1.0f)) return 1;
  const rawlab::pipeline::Mask blurred = rawlab::pipeline::blurMask(
    rawlab::pipeline::verticalSplitMask(9, 3), 1.0f);
  if(!nearlyEqual(blurred.sampleNearest(3, 1), 1.0f / 3.0f)) return 1;
  if(!nearlyEqual(blurred.sampleNearest(4, 1), 2.0f / 3.0f)) return 1;
  if(!nearlyEqual(mask.sampleBilinear(0.5f, 0.5f), 0.625f)) return 1;

  rawlab::pipeline::Mask painted = rawlab::pipeline::filledMask(9, 9);
  painted.paintSegment(4.5f, 4.5f, 4.5f, 4.5f, 2.0f, 1.0f, 1.0f);
  if(!nearlyEqual(painted.sampleNearest(4, 4), 1.0f)) return 1;
  if(!nearlyEqual(painted.sampleNearest(0, 0), 0.0f)) return 1;
  painted.paintSegment(4.5f, 4.5f, 4.5f, 4.5f, 1.0f, 1.0f, 0.0f);
  if(!nearlyEqual(painted.sampleNearest(4, 4), 0.0f)) return 1;
  rawlab::pipeline::Mask softBrush = rawlab::pipeline::filledMask(9, 9);
  softBrush.paintSegment(4.5f, 4.5f, 4.5f, 4.5f, 3.0f, 0.0f, 1.0f);
  if(!(softBrush.sampleNearest(6, 4) > 0.0f && softBrush.sampleNearest(6, 4) < 1.0f)) return 1;

  const rawlab::pipeline::Mask previewMask{2, 2, std::vector<float>{ 0.0f, 0.25f, 0.75f, 1.0f }};
  if(!nearlyEqual(previewMask.sampleNearestMapped(0, 0, 4, 4), 0.0f)) return 1;
  if(!nearlyEqual(previewMask.sampleNearestMapped(1, 1, 4, 4), 0.0f)) return 1;
  if(!nearlyEqual(previewMask.sampleNearestMapped(2, 0, 4, 4), 0.25f)) return 1;
  if(!nearlyEqual(previewMask.sampleNearestMapped(0, 2, 4, 4), 0.75f)) return 1;
  if(!nearlyEqual(previewMask.sampleNearestMapped(3, 3, 4, 4), 1.0f)) return 1;

  rawlab::raw::RawImage smallOriginal{
    2,
    2,
    std::vector<float>{ 10.0f, 10.0f, 10.0f, 10.0f },
    highlightMetadata,
  };
  rawlab::raw::RawImage smallProcessed{
    2,
    2,
    std::vector<float>{ 20.0f, 20.0f, 20.0f, 20.0f },
    highlightMetadata,
  };
  const rawlab::raw::RawImage masked = rawlab::pipeline::blendWithMask(smallOriginal, smallProcessed, mask, 1.0f);
  if(!nearlyEqual(masked.sample(0, 0), 10.0f)) return 1;
  if(!nearlyEqual(masked.sample(1, 0), 15.0f)) return 1;
  if(!nearlyEqual(masked.sample(0, 1), 20.0f)) return 1;
  if(!nearlyEqual(masked.sample(1, 1), 20.0f)) return 1;

  const rawlab::raw::RawImage maskedQuarter = rawlab::pipeline::blendWithMask(smallOriginal, smallProcessed, mask, 0.25f);
  if(!nearlyEqual(maskedQuarter.sample(1, 1), 12.5f)) return 1;

  stage = "mapped masks";
  const rawlab::raw::RawImage mappedMasked = rawlab::pipeline::blendWithMappedMask(image, processed, previewMask, 1.0f);
  if(!nearlyEqual(mappedMasked.sample(0, 0), image.sample(0, 0))) return 1;
  if(!nearlyEqual(mappedMasked.sample(2, 0), 5.5f)) return 1;
  if(!nearlyEqual(mappedMasked.sample(0, 2), 16.5f)) return 1;
  if(!nearlyEqual(mappedMasked.sample(3, 3), processed.sample(3, 3))) return 1;

  const rawlab::pipeline::Mask previewSplit = rawlab::pipeline::verticalSplitMask(2, 1);
  const rawlab::raw::RawImage mappedSplit = rawlab::pipeline::blendWithMappedMask(image, processed, previewSplit, 1.0f);
  if(!nearlyEqual(mappedSplit.sample(0, 0), image.sample(0, 0))) return 1;
  if(!nearlyEqual(mappedSplit.sample(1, 3), image.sample(1, 3))) return 1;
  if(!nearlyEqual(mappedSplit.sample(2, 0), processed.sample(2, 0))) return 1;
  if(!nearlyEqual(mappedSplit.sample(3, 3), processed.sample(3, 3))) return 1;

  stage = "preview quality";
  const rawlab::image::RawPreview preview = rawlab::image::renderRawPreview(image, 2);
  if(preview.image.width != 2 || preview.image.height != 2) return 1;
  if(preview.image.pixels.size() != 12) return 1;
  const rawlab::image::RawPreview fastPreview = rawlab::image::renderRawPreview(
    image, 4, rawlab::image::PreviewQuality::Fast);
  const rawlab::image::RawPreview defaultPreview = rawlab::image::renderRawPreview(image, 4);
  if(defaultPreview.image.pixels != fastPreview.image.pixels) return 1;
  const rawlab::image::RawPreview highQualityPreview = rawlab::image::renderRawPreview(
    image, 4, rawlab::image::PreviewQuality::High);
  if(highQualityPreview.image.width != fastPreview.image.width
     || highQualityPreview.image.height != fastPreview.image.height
      || highQualityPreview.image.pixels == fastPreview.image.pixels) return 1;
  const rawlab::raw::RawImage bggrPreviewSource = makeFlatPreviewImage(bggr);
  const std::vector<std::array<float, 3>> amazePixels = rawlab::image::demosaicAmaze(
    bggrPreviewSource, bggrPreviewSource.metadata().activeArea);
  if(amazePixels.size() != 16U * 16U) return 1;
  for(const auto &pixel : amazePixels)
    for(const float channel : pixel)
      if(!std::isfinite(channel) || std::fabs(channel - 0.4f) > 0.01f) return 1;
  // AMaZE must produce neutral camera RGB before the color matrix even when
  // the source field requires a non-neutral camera white balance.
  const std::array<float, 3> nonNeutralWb{ 0.55f, 1.0f, 0.32f };
  const rawlab::raw::CfaPattern grbg = makeGrbg();
  const rawlab::raw::CfaPattern gbrg = makeGbrg();
  for(const rawlab::raw::CfaPattern pattern : { rggb, bggr, grbg, gbrg })
  {
    const rawlab::raw::RawImage flat = makeFlatPreviewImage(pattern, nonNeutralWb);
    const std::vector<std::array<float, 3>> pixels = rawlab::image::demosaicAmaze(
      flat, flat.metadata().activeArea);
    for(const auto &pixel : pixels)
      for(const float channel : pixel)
        if(!std::isfinite(channel) || std::fabs(channel - 0.4f) > 0.01f) return 1;

    const rawlab::image::RawPreview preview = rawlab::image::renderRawPreview(
      flat, 8, rawlab::image::PreviewQuality::High);
    for(std::size_t i = 0; i < preview.image.pixels.size(); i += 3)
    {
      const int red = preview.image.pixels[i];
      const int green = preview.image.pixels[i + 1];
      const int blue = preview.image.pixels[i + 2];
      if(std::abs(red - green) > 1 || std::abs(green - blue) > 1) return 1;
    }
  }
  // A row-varying field exercises AMaZE's complete eight-row filter encoding.
  rawlab::raw::RawMetadata rowPatternMetadata = image.metadata();
  rowPatternMetadata.activeArea = { 0, 0, 32, 32 };
  std::vector<float> rowPatternSamples(32 * 32, 0.0f);
  for(int y = 0; y < 32; ++y)
    for(int x = 0; x < 32; ++x)
    {
      const int channel = rawlab::raw::colorAt(rggb, x, y) == rawlab::raw::CfaColor::Red ? 0
        : rawlab::raw::colorAt(rggb, x, y) == rawlab::raw::CfaColor::Green ? 1 : 2;
      rowPatternSamples[static_cast<std::size_t>(y * 32 + x)] =
        4.0f + static_cast<float>(channel) + 0.01f * static_cast<float>(y);
    }
  const rawlab::raw::RawImage rowPattern{32, 32, std::move(rowPatternSamples), rowPatternMetadata};
  const auto rowPatternPixels = rawlab::image::demosaicAmaze(rowPattern, rowPatternMetadata.activeArea);
  for(const auto &pixel : rowPatternPixels)
    if(!std::isfinite(pixel[0]) || !std::isfinite(pixel[1]) || !std::isfinite(pixel[2])) return 1;
  rawlab::raw::RawMetadata largePreviewMetadata = image.metadata();
  largePreviewMetadata.activeArea = {0, 0, 16, 8};
  largePreviewMetadata.whiteLevel = 128.0f;
  std::vector<float> largePreviewSamples(16 * 8);
  for(std::size_t index = 0; index < largePreviewSamples.size(); ++index)
  {
    largePreviewSamples[index] = static_cast<float>((index * 17) % 127);
  }
  const rawlab::raw::RawImage largePreviewSource{
    16, 8, std::move(largePreviewSamples), largePreviewMetadata,
  };
  const rawlab::image::RawPreview downsampledHigh = rawlab::image::renderRawPreview(
    largePreviewSource, 4, rawlab::image::PreviewQuality::High);
  if(downsampledHigh.image.width != 4 || downsampledHigh.image.height != 2) return 1;

  rawlab::raw::RawMetadata neutralMetadata = image.metadata();
  neutralMetadata.whiteLevel = 16.0f;
  const rawlab::raw::RawImage neutralImage{
    4, 4, std::vector<float>(16, 8.0f), neutralMetadata,
  };
  const rawlab::image::RawPreview neutralPreview = rawlab::image::renderRawPreview(neutralImage, 4);
  for(std::size_t i = 0; i < neutralPreview.image.pixels.size(); i += 3)
  {
    if(neutralPreview.image.pixels[i] != neutralPreview.image.pixels[i + 1]
       || neutralPreview.image.pixels[i] != neutralPreview.image.pixels[i + 2]) return 1;
  }

  rawlab::raw::RawMetadata rotatedMetadata = image.metadata();
  rotatedMetadata.activeArea = {0, 0, 4, 2};
  rotatedMetadata.flip = 6;
  const rawlab::raw::RawImage rotatedSource{
    4,
    2,
    std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f},
    rotatedMetadata,
  };
  const rawlab::image::RawPreview rotatedPreview = rawlab::image::renderRawPreview(rotatedSource, 4);
  if(rotatedPreview.image.width != 2 || rotatedPreview.image.height != 4) return 1;
  const auto [previewX, previewY] = rotatedPreview.geometry.rawToPreview(0.5f, 0.5f);
  const auto [mappedRawX, mappedRawY] = rotatedPreview.geometry.previewToRaw(previewX, previewY);
  if(!nearlyEqual(mappedRawX, 0.5f) || !nearlyEqual(mappedRawY, 0.5f)) return 1;

  const std::filesystem::path orientedDngPath = std::filesystem::temp_directory_path()
                                                 / "rawlab-oriented-test.dng";
  rawlab::dng::TiffDngWriter{}.writeCfaDng(orientedDngPath, rotatedSource);
  TIFF *orientedTiff = TIFFOpen(orientedDngPath.string().c_str(), "r");
  if(!orientedTiff) return 1;
  std::uint16_t orientation = 0;
  const int hasOrientation = TIFFGetField(orientedTiff, TIFFTAG_ORIENTATION, &orientation);
  TIFFClose(orientedTiff);
  std::filesystem::remove(orientedDngPath);
  if(hasOrientation != 1 || orientation != ORIENTATION_RIGHTTOP) return 1;

  stage = "DNG CFA phase";
  rawlab::raw::RawMetadata phaseMetadata = image.metadata();
  phaseMetadata.activeArea = {1, 1, 2, 2};
  phaseMetadata.blackLevel = {10.0f, 20.0f, 30.0f, 40.0f};
  phaseMetadata.colorMatrix1 = {
    0.7f, -0.1f, 0.2f,
    -0.2f, 1.1f, 0.1f,
    0.05f, -0.3f, 0.9f,
  };
  phaseMetadata.whiteLevel = 100.0f;
  const rawlab::raw::RawImage phaseSource{
    4, 4, std::vector<float>(16, 12.0f), phaseMetadata,
  };
  const std::filesystem::path phaseDngPath = std::filesystem::temp_directory_path()
                                               / "rawlab-phase-test.dng";
  rawlab::dng::TiffDngWriter{}.writeCfaDng(phaseDngPath, phaseSource);
  TIFF *phaseTiff = TIFFOpen(phaseDngPath.string().c_str(), "r");
  if(!phaseTiff) return 1;
  std::uint32_t patternCount = 0;
  std::uint8_t *writtenPattern = nullptr;
  std::uint32_t blackCount = 0;
  float *writtenBlack = nullptr;
  std::uint32_t matrixCount = 0;
  float *writtenMatrix = nullptr;
  const bool phaseTags = TIFFGetField(phaseTiff, TIFFTAG_CFAPATTERN,
                                      &patternCount, &writtenPattern) == 1
                          && TIFFGetField(phaseTiff, TIFFTAG_BLACKLEVEL,
                                          &blackCount, &writtenBlack) == 1
                          && TIFFGetField(phaseTiff, TIFFTAG_COLORMATRIX1,
                                          &matrixCount, &writtenMatrix) == 1;
  const bool phaseValues = phaseTags && patternCount == 4 && blackCount == 4 && matrixCount == 9
                           && writtenPattern[0] == 2 && writtenPattern[1] == 1
                           && writtenPattern[2] == 1 && writtenPattern[3] == 0
                           && nearlyEqual(writtenBlack[0], 40.0f)
                            && nearlyEqual(writtenBlack[1], 30.0f)
                            && nearlyEqual(writtenBlack[2], 20.0f)
                            && nearlyEqual(writtenBlack[3], 10.0f)
                            && nearlyEqual(writtenMatrix[0], 0.7f)
                            && nearlyEqual(writtenMatrix[8], 0.9f);
  TIFFClose(phaseTiff);
  std::filesystem::remove(phaseDngPath);
  if(!phaseValues) return 1;

  rawlab::raw::RawMetadata invalidColorMetadata = rotatedMetadata;
  invalidColorMetadata.colorMatrix1.fill(0.0f);
  const rawlab::raw::RawImage invalidColorImage{
    4, 2, std::vector<float>(8, 1.0f), invalidColorMetadata,
  };
  bool rejectedInvalidColor = false;
  const std::filesystem::path invalidDngPath = std::filesystem::temp_directory_path()
                                                 / "rawlab-invalid-color-test.dng";
  try { rawlab::dng::TiffDngWriter{}.writeCfaDng(invalidDngPath, invalidColorImage); }
  catch(const std::invalid_argument &) { rejectedInvalidColor = true; }
  if(!rejectedInvalidColor) return 1;

  stage = "layer composition";
  const rawlab::image::PreviewGeometry activeGeometry{{1, 1, 2, 2}, 2, 2, 0};
  const rawlab::pipeline::Mask activeMask{2, 2, std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f}};
  const rawlab::raw::RawImage activeBlend = rawlab::pipeline::blendWithPreviewMask(
    image, processed, activeMask, activeGeometry, 1.0f);
  if(!nearlyEqual(activeBlend.sample(1, 1), processed.sample(1, 1))) return 1;
  if(!nearlyEqual(activeBlend.sample(2, 1), image.sample(2, 1))) return 1;
  if(!nearlyEqual(activeBlend.sample(0, 0), image.sample(0, 0))) return 1;
  const rawlab::raw::RawImage invertedBlend = rawlab::pipeline::blendWithPreviewMask(
    image, processed, activeMask, activeGeometry, 1.0f, true);
  if(!nearlyEqual(invertedBlend.sample(1, 1), image.sample(1, 1))) return 1;
  if(!nearlyEqual(invertedBlend.sample(2, 1), processed.sample(2, 1))) return 1;

  const rawlab::pipeline::Mask fullMask = rawlab::pipeline::filledMask(2, 2, 1.0f);
  const rawlab::image::PreviewGeometry fullGeometry{{0, 0, 2, 2}, 2, 2, 0};
  const rawlab::raw::RawImage layerA{
    2, 2, std::vector<float>(4, 20.0f), highlightMetadata,
  };
  const rawlab::raw::RawImage layerB{
    2, 2, std::vector<float>(4, 30.0f), highlightMetadata,
  };
  const rawlab::raw::RawImage firstLayer = rawlab::pipeline::blendWithPreviewMask(
    smallOriginal, layerA, fullMask, fullGeometry, 0.5f);
  const rawlab::raw::RawImage secondLayer = rawlab::pipeline::blendWithPreviewMask(
    firstLayer, layerB, fullMask, fullGeometry, 0.5f);
  if(!nearlyEqual(secondLayer.sample(0, 0), 22.5f)) return 1;

  const rawlab::raw::RawImage globalLayer{
    2, 2, std::vector<float>(4, 15.0f), highlightMetadata,
  };
  const std::array localStack{
    rawlab::pipeline::LocalLayerInput{&layerA, &fullMask, 1.0f, false, true},
    rawlab::pipeline::LocalLayerInput{&layerB, &fullMask, 1.0f, false, true},
  };
  const rawlab::raw::RawImage stacked = rawlab::pipeline::composeDenoiseLayers(
    smallOriginal, {&globalLayer, 1.0f, true}, localStack, fullGeometry);
  if(!nearlyEqual(stacked.sample(0, 0), 30.0f)) return 1;
  const rawlab::raw::RawImage metadataResult = rawlab::pipeline::composeDenoiseLayers(
    image, {}, {}, preview.geometry);
  if(metadataResult.metadata().iso != image.metadata().iso
     || metadataResult.metadata().aperture != image.metadata().aperture
      || metadataResult.metadata().exposureTimeSeconds != image.metadata().exposureTimeSeconds
      || metadataResult.metadata().exposureBiasEv != image.metadata().exposureBiasEv
      || metadataResult.metadata().baselineExposureEv != image.metadata().baselineExposureEv
     || metadataResult.metadata().focalLengthMm != image.metadata().focalLengthMm
     || metadataResult.metadata().focalLength35mm != image.metadata().focalLength35mm
     || metadataResult.metadata().captureTimestamp != image.metadata().captureTimestamp
     || metadataResult.metadata().artist != image.metadata().artist
     || metadataResult.metadata().description != image.metadata().description
     || metadataResult.metadata().bodySerial != image.metadata().bodySerial
     || metadataResult.metadata().lensMake != image.metadata().lensMake
     || metadataResult.metadata().lensModel != image.metadata().lensModel
     || metadataResult.metadata().lensSerial != image.metadata().lensSerial
     || metadataResult.metadata().lensSpecification != image.metadata().lensSpecification) return 1;
  const rawlab::pipeline::Mask leftMask{2, 2, std::vector<float>{1.0f, 0.0f, 1.0f, 0.0f}};
  const std::array localLeft{
    rawlab::pipeline::LocalLayerInput{&layerB, &leftMask, 1.0f, false, true},
  };
  const rawlab::raw::RawImage globalWithLocal = rawlab::pipeline::composeDenoiseLayers(
    smallOriginal, {&globalLayer, 1.0f, true}, localLeft, fullGeometry);
  if(!nearlyEqual(globalWithLocal.sample(0, 0), 30.0f)) return 1;
  if(!nearlyEqual(globalWithLocal.sample(1, 0), 15.0f)) return 1;
  const std::array halfLocal{
    rawlab::pipeline::LocalLayerInput{&layerB, &leftMask, 0.5f, false, true},
  };
  const rawlab::raw::RawImage halfLocalResult = rawlab::pipeline::composeDenoiseLayers(
    smallOriginal, {&globalLayer, 1.0f, true}, halfLocal, fullGeometry);
  if(!nearlyEqual(halfLocalResult.sample(0, 0), 20.0f)) return 1;
  if(!nearlyEqual(halfLocalResult.sample(1, 0), 15.0f)) return 1;

  bool dimensionMismatchThrown = false;
  try
  {
    const rawlab::pipeline::Mask badMask{1, 1, std::vector<float>{ 1.0f }};
    (void)rawlab::pipeline::blendWithMask(smallOriginal, smallProcessed, badMask, 1.0f);
  }
  catch(const std::invalid_argument &)
  {
    dimensionMismatchThrown = true;
  }
  if(!dimensionMismatchThrown) return 1;

  const std::filesystem::path maskPath = std::filesystem::temp_directory_path() / "rawlab-mask-roundtrip.png";
  rawlab::image::writeMaskPng(maskPath, mask);
  const rawlab::pipeline::Mask loadedMask = rawlab::image::readMaskPng(maskPath);
  std::filesystem::remove(maskPath);
  if(loadedMask.width() != mask.width() || loadedMask.height() != mask.height()) return 1;
  if(!nearlyEqual(loadedMask.sampleNearest(0, 0), 0.0f)) return 1;
  if(std::fabs(loadedMask.sampleNearest(1, 0) - 0.5f) > (1.0f / 255.0f)) return 1;
  if(!nearlyEqual(loadedMask.sampleNearest(1, 1), 1.0f)) return 1;

  stage = nullptr;
  return 0;
}
