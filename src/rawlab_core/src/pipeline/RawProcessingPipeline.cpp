#include "rawlab/pipeline/RawProcessingPipeline.h"

#include "RawNindDenoise.h"
#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/raw/BayerProcessing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rawlab::pipeline {
namespace {

int channelIndex(const raw::CfaColor color)
{
  switch(color)
  {
    case raw::CfaColor::Red: return 0;
    case raw::CfaColor::Green: return 1;
    case raw::CfaColor::Blue: return 2;
    case raw::CfaColor::Unknown: break;
  }
  throw std::runtime_error{"Unsupported CFA color"};
}

int mirrorInRange(int value, const int low, const int high)
{
  if(high <= low) return low;
  while(value < low || value >= high)
  {
    if(value < low) value = low + (low - value);
    // Reflect around the last sample without changing Bayer row/column parity.
    if(value >= high) value = high - 2 - (value - high);
  }
  return value;
}

float cfaBlackLevel(const raw::RawImage &image, const int x, const int y)
{
  return image.metadata().blackLevel[static_cast<std::size_t>(raw::siteIndex(x, y))];
}

void recoverRawNindGain(const std::vector<float> &packedInput,
                        std::vector<float> &modelOutput,
                        const int packedTileSize,
                        const raw::ActiveArea &validPackedArea)
{
  if(!std::all_of(modelOutput.begin(), modelOutput.end(), [](const float value) {
       return std::isfinite(value);
     }))
    throw std::runtime_error{"RawNIND gain recovery: nonfinite model output"};

  const int sensorTileSize = 2 * packedTileSize;
  const std::size_t packedPlane = static_cast<std::size_t>(packedTileSize) * packedTileSize;
  const std::size_t modelPlane = static_cast<std::size_t>(sensorTileSize) * sensorTileSize;
  constexpr int channels[]{0, 1, 1, 2};
  double numerator = 0.0;
  double denominator = 0.0;
  std::size_t validSamples = 0;
  // Packing aligns the tile origin to red. Compare R/G1/G2/B at the same
  // sensor sites; interpolated RGB values and reflected padding cannot set gain.
  for(int k = 0; k < 4; ++k)
  {
    for(int py = validPackedArea.top; py < validPackedArea.top + validPackedArea.height; ++py)
      for(int px = validPackedArea.left; px < validPackedArea.left + validPackedArea.width; ++px)
      {
        const float source = packedInput[k * packedPlane + py * packedTileSize + px];
        if(!std::isfinite(source) || source <= 0.001f || source >= 0.999f) continue;
        const int modelX = 2 * px + (k & 1);
        const int modelY = 2 * py + (k >> 1);
        const double predicted = modelOutput[channels[k] * modelPlane + modelY * sensorTileSize + modelX];
        numerator += predicted * static_cast<double>(source);
        denominator += predicted * predicted;
        ++validSamples;
      }
  }
  const std::size_t realSamples = 4 * static_cast<std::size_t>(validPackedArea.width)
                                   * validPackedArea.height;
  if(realSamples == 0 || validSamples < (realSamples + 9) / 10)
    throw std::runtime_error{"RawNIND gain recovery: too few valid CFA samples (need 10% of real tile area)"};
  if(denominator == 0.0)
    throw std::runtime_error{"RawNIND gain recovery: model output has zero energy"};
  const double gain = numerator / denominator;
  if(!std::isfinite(gain) || gain == 0.0)
    throw std::runtime_error{"RawNIND gain recovery: zero or nonfinite gain"};

  // RawNIND can emit either sign and widely varying scales. Keep the scalar and
  // multiplication in double; this recovery belongs only to this denoiser.
  for(float &value : modelOutput)
  {
    const double scaled = static_cast<double>(value) * gain;
    if(!std::isfinite(scaled) || std::fabs(scaled) > std::numeric_limits<float>::max())
      throw std::runtime_error{"RawNIND gain recovery: scaled model output overflow"};
    value = static_cast<float>(scaled);
  }
}

std::vector<float> packRawNindTile(const raw::RawImage &image,
                                   const int sensorOriginX,
                                   const int sensorOriginY,
                                   const int mirrorLeft,
                                   const int mirrorTop,
                                   const int mirrorRight,
                                   const int mirrorBottom,
                                   const int packedTileSize)
{
  const std::size_t planeSize = static_cast<std::size_t>(packedTileSize)
                                * static_cast<std::size_t>(packedTileSize);
  std::vector<float> tile(4 * planeSize, 0.0f);

  for(int py = 0; py < packedTileSize; ++py)
  {
    const int y0 = sensorOriginY + 2 * py;
    for(int px = 0; px < packedTileSize; ++px)
    {
      const int x0 = sensorOriginX + 2 * px;
      for(int k = 0; k < 4; ++k)
      {
        const int dx = k & 1;
        const int dy = (k >> 1) & 1;
        const int x = mirrorInRange(x0 + dx, mirrorLeft, mirrorRight);
        const int y = mirrorInRange(y0 + dy, mirrorTop, mirrorBottom);
        const float normalized = raw::normalizeRawSample(
          image.sample(x, y),
          cfaBlackLevel(image, x, y),
          image.metadata().whiteLevel);
        tile[static_cast<std::size_t>(k) * planeSize
             + static_cast<std::size_t>(py) * static_cast<std::size_t>(packedTileSize)
             + static_cast<std::size_t>(px)] = normalized;
      }
    }
  }

  return tile;
}

float seamWeight(const int pos,
                 const int coreStart,
                 const int coreEnd,
                 const int overlap,
                 const bool hasBefore,
                 const bool hasAfter)
{
  if(hasBefore && pos < coreStart + overlap)
  {
    return std::clamp((static_cast<float>(pos - (coreStart - overlap)) + 0.5f)
                        / static_cast<float>(2 * overlap),
                      0.0f,
                      1.0f);
  }
  if(hasAfter && pos >= coreEnd - overlap)
  {
    return std::clamp((static_cast<float>((coreEnd + overlap) - pos) - 0.5f)
                        / static_cast<float>(2 * overlap),
                      0.0f,
                      1.0f);
  }
  return 1.0f;
}

} // namespace

raw::RawImage RawProcessingPipeline::runNormalSharpen(const raw::RawImage &source,
                                                      const float amount,
                                                      const ProgressCallback &progress) const
{
  if(!source.metadata().cfa.isBayer) throw std::invalid_argument{"normal sharpening requires a Bayer CFA source"};
  const float strength = std::clamp(amount, 0.0f, 1.0f);
  std::vector<float> output = source.samples();
  const int total = source.width() * source.height();
  int completed = 0;
  const int reportEvery = std::max(1, total / 100);
  for(int y = 0; y < source.height(); ++y)
  {
    for(int x = 0; x < source.width(); ++x)
    {
      const float center = source.sample(x, y);
      float sum = 0.0f;
      int count = 0;
      for(const auto &[dx, dy] : {std::pair<int, int>{-2, 0}, {2, 0}, {0, -2}, {0, 2}})
      {
        const int nx = x + dx;
        const int ny = y + dy;
        if(nx < 0 || ny < 0 || nx >= source.width() || ny >= source.height()) continue;
        if(raw::colorAt(source.metadata().cfa, nx, ny) != raw::colorAt(source.metadata().cfa, x, y)) continue;
        sum += source.sample(nx, ny);
        ++count;
      }
      const float average = count == 0 ? center : sum / static_cast<float>(count);
      const float black = cfaBlackLevel(source, x, y);
      const float value = std::clamp(center + strength * (center - average),
                                    black, source.metadata().whiteLevel);
      output[static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width())
             + static_cast<std::size_t>(x)] = value;
       ++completed;
       if(progress && (completed == total || completed % reportEvery == 0)) progress(completed, total);
    }
  }
  return raw::RawImage{source.width(), source.height(), std::move(output), source.metadata()};
}

raw::RawImage RawProcessingPipeline::runRawNindDenoise(const raw::RawImage &source,
                                                       const ai::OnnxSession &session,
                                                       const ProgressCallback &progress) const
{
  return detail::runRawNindDenoiseTiles(source, [&session](const std::vector<float> &input) {
    return session.runSingleFloat(input, {1, 4, 512, 512});
  }, progress);
}

raw::RawImage detail::runRawNindDenoiseTiles(
  const raw::RawImage &source,
  const RawNindInference &infer,
  const RawProcessingPipeline::ProgressCallback &progress)
{
  constexpr int packedTileSize = 512;
  constexpr int packedOverlap = 32;
  constexpr int packedStep = packedTileSize - 2 * packedOverlap;
  constexpr int sensorTileSize = 2 * packedTileSize;
  constexpr int sensorOverlap = 2 * packedOverlap;

  int redX = 0;
  int redY = 0;
  if(!raw::redOrigin(source.metadata().cfa, redX, redY))
  {
    throw std::runtime_error{"raw-denoise requires a Bayer CFA source"};
  }

  const raw::ActiveArea &active = source.metadata().activeArea;
  int workLeft = active.width > 0 ? active.left : 0;
  int workTop = active.height > 0 ? active.top : 0;
  const int activeRight = workLeft + (active.width > 0 ? active.width : source.width());
  const int activeBottom = workTop + (active.height > 0 ? active.height : source.height());

  if((workLeft & 1) != redX) ++workLeft;
  if((workTop & 1) != redY) ++workTop;

  const int packedWidth = (activeRight - workLeft) / 2;
  const int packedHeight = (activeBottom - workTop) / 2;
  if(packedWidth <= 0 || packedHeight <= 0)
  {
    throw std::runtime_error{"raw-denoise has no Bayer-aligned working area"};
  }
  const int workRight = workLeft + 2 * packedWidth;
  const int workBottom = workTop + 2 * packedHeight;

  const int cols = (packedWidth + packedStep - 1) / packedStep;
  const int rows = (packedHeight + packedStep - 1) / packedStep;
  const int totalTiles = rows * cols;
  int tileIndex = 0;

  std::vector<float> accum(source.samples().size(), 0.0f);
  std::vector<float> weights(source.samples().size(), 0.0f);

  for(int ty = 0; ty < rows; ++ty)
  {
    const int pyBase = ty * packedStep;
    const int pyEnd = std::min(pyBase + packedStep, packedHeight);
    const bool hasTop = ty > 0;
    const bool hasBottom = ty < rows - 1;

    for(int tx = 0; tx < cols; ++tx)
    {
      const int pxBase = tx * packedStep;
      const int pxEnd = std::min(pxBase + packedStep, packedWidth);
      const bool hasLeft = tx > 0;
      const bool hasRight = tx < cols - 1;

      const int sensorCoreX0 = workLeft + 2 * pxBase;
      const int sensorCoreY0 = workTop + 2 * pyBase;
      const int sensorCoreX1 = workLeft + 2 * pxEnd;
      const int sensorCoreY1 = workTop + 2 * pyEnd;
      const int sensorOriginX = workLeft + 2 * (pxBase - packedOverlap);
      const int sensorOriginY = workTop + 2 * (pyBase - packedOverlap);

      std::vector<float> tileInput = packRawNindTile(source,
                                                     sensorOriginX,
                                                     sensorOriginY,
                                                     workLeft,
                                                     workTop,
                                                     workRight,
                                                     workBottom,
                                                     packedTileSize);

      ai::OnnxFloatRunResult result = infer(tileInput);
      const std::size_t modelPlaneSize = static_cast<std::size_t>(sensorTileSize)
                                         * static_cast<std::size_t>(sensorTileSize);
      if(result.shape != std::vector<std::int64_t>{1, 3, sensorTileSize, sensorTileSize}
         || result.values.size() != 3 * modelPlaneSize)
      {
        throw std::runtime_error{"RawNIND model returned an unexpected output shape"};
      }

      const int validLeft = std::max(0, packedOverlap - pxBase);
      const int validTop = std::max(0, packedOverlap - pyBase);
      const int validRight = std::min(packedTileSize, packedWidth + packedOverlap - pxBase);
      const int validBottom = std::min(packedTileSize, packedHeight + packedOverlap - pyBase);
      recoverRawNindGain(tileInput, result.values, packedTileSize,
                        {validLeft, validTop, validRight - validLeft, validBottom - validTop});

      const int writeX0 = hasLeft ? sensorCoreX0 - sensorOverlap : sensorCoreX0;
      const int writeY0 = hasTop ? sensorCoreY0 - sensorOverlap : sensorCoreY0;
      const int writeX1 = std::min(workRight, hasRight ? sensorCoreX1 + sensorOverlap : sensorCoreX1);
      const int writeY1 = std::min(workBottom, hasBottom ? sensorCoreY1 + sensorOverlap : sensorCoreY1);

      for(int y = writeY0; y < writeY1; ++y)
      {
        const int modelY = y - sensorOriginY;
        if(modelY < 0 || modelY >= sensorTileSize) continue;
        const float wy = seamWeight(y, sensorCoreY0, sensorCoreY1, sensorOverlap, hasTop, hasBottom);
        for(int x = writeX0; x < writeX1; ++x)
        {
          const int modelX = x - sensorOriginX;
          if(modelX < 0 || modelX >= sensorTileSize) continue;
          const float wx = seamWeight(x, sensorCoreX0, sensorCoreX1, sensorOverlap, hasLeft, hasRight);
          const float weight = wx * wy;
          if(weight <= 0.0f) continue;

          const int channel = channelIndex(raw::colorAt(source.metadata().cfa, x, y));
          const float modelValue = result.values[static_cast<std::size_t>(channel) * modelPlaneSize
                                                 + static_cast<std::size_t>(modelY) * sensorTileSize
                                                 + static_cast<std::size_t>(modelX)];
          const float rawValue = raw::denormalizeRawSample(
            modelValue,
            cfaBlackLevel(source, x, y),
            source.metadata().whiteLevel);

          const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width())
                                  + static_cast<std::size_t>(x);
          accum[idx] += rawValue * weight;
          if(!std::isfinite(accum[idx]))
            throw std::runtime_error{"RawNIND gain recovery: CFA composition overflow"};
          weights[idx] += weight;
        }
      }

      ++tileIndex;
      if(progress) progress(tileIndex, totalTiles);
    }
  }

  std::vector<float> output = source.samples();
  for(std::size_t i = 0; i < output.size(); ++i)
  {
    if(weights[i] > 0.0f) output[i] = accum[i] / weights[i];
  }

  return raw::RawImage{source.width(), source.height(), std::move(output), source.metadata()};
}

raw::RawImage RawProcessingPipeline::runRawNindDenoiseBlend(
  const raw::RawImage &source,
  const ai::OnnxSession &session,
  const RawDenoisePipelineOptions &options,
  const ProgressCallback &progress) const
{
  raw::RawImage denoised = runRawNindDenoise(source, session, progress);
  if(options.preserveClippedHighlights)
  {
    denoised = raw::preserveClippedHighlights(source, denoised);
  }
  return raw::blendWithOriginal(source, denoised, options.blend);
}

raw::RawImage RawProcessingPipeline::runRawNindDenoiseMaskedBlend(
  const raw::RawImage &source,
  const ai::OnnxSession &session,
  const Mask &mask,
  const RawMaskedDenoisePipelineOptions &options,
  const ProgressCallback &progress) const
{
  raw::RawImage denoised = runRawNindDenoise(source, session, progress);
  if(options.preserveClippedHighlights)
  {
    denoised = raw::preserveClippedHighlights(source, denoised);
  }
  return options.mapMaskToRaw
    ? blendWithMappedMask(source, denoised, mask, options.blend)
    : blendWithMask(source, denoised, mask, options.blend);
}

}
