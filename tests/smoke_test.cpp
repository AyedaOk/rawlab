#include "rawlab/core/Version.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

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

rawlab::raw::RawImage makeSyntheticRggbImage()
{
  rawlab::raw::RawMetadata metadata;
  metadata.cfa = makeRggb();
  metadata.activeArea = { 0, 0, 4, 4 };
  metadata.blackLevel = { 0.0f, 0.0f, 0.0f, 0.0f };
  metadata.whiteLevel = 16.0f;
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

} // namespace

int main()
{
  if(rawlab::core::version() != std::string_view{"0.1.0"}) return 1;

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

  const rawlab::raw::RawImage image = makeSyntheticRggbImage();
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

  return 0;
}
