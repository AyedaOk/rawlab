#include "rawlab/pipeline/RawProcessingPipeline.h"

#include "rawlab/pipeline/MaskedBlend.h"
#include "rawlab/raw/BayerProcessing.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
    if(value >= high) value = high - 1 - (value - high);
  }
  return value;
}

float cfaBlackLevel(const raw::RawImage &image, const int x, const int y)
{
  return image.metadata().blackLevel[static_cast<std::size_t>(raw::siteIndex(x, y))];
}

double meanOf(const std::vector<float> &values)
{
  double sum = 0.0;
  for(const float value : values) sum += value;
  return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

void matchGain(const std::vector<float> &packedInput, std::vector<float> &modelOutput)
{
  const double inputMean = meanOf(packedInput);
  const double outputMean = meanOf(modelOutput);
  const double threshold = 1e-6 * std::fabs(inputMean);
  const float gain = std::fabs(outputMean) > threshold
    ? static_cast<float>(inputMean / outputMean) : 1.0f;
  if(gain != 1.0f)
  {
    for(float &value : modelOutput) value *= gain;
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
      if(progress) progress(++completed, total);
    }
  }
  return raw::RawImage{source.width(), source.height(), std::move(output), source.metadata()};
}

raw::RawImage RawProcessingPipeline::runRawNindDenoise(const raw::RawImage &source,
                                                       const ai::OnnxSession &session,
                                                       const ProgressCallback &progress) const
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
                                                     workLeft + 2 * packedWidth,
                                                     workTop + 2 * packedHeight,
                                                     packedTileSize);

      ai::OnnxFloatRunResult result = session.runSingleFloat(
        tileInput,
        std::vector<std::int64_t>{1, 4, packedTileSize, packedTileSize});
      if(result.shape != std::vector<std::int64_t>{1, 3, sensorTileSize, sensorTileSize})
      {
        throw std::runtime_error{"RawNIND model returned an unexpected output shape"};
      }

      matchGain(tileInput, result.values);

      const std::size_t modelPlaneSize = static_cast<std::size_t>(sensorTileSize)
                                         * static_cast<std::size_t>(sensorTileSize);
      const int writeX0 = hasLeft ? sensorCoreX0 - sensorOverlap : sensorCoreX0;
      const int writeY0 = hasTop ? sensorCoreY0 - sensorOverlap : sensorCoreY0;
      const int writeX1 = hasRight ? sensorCoreX1 + sensorOverlap : sensorCoreX1;
      const int writeY1 = hasBottom ? sensorCoreY1 + sensorOverlap : sensorCoreY1;

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
