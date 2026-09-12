#include "rawlab/ai/RawForgeAdapter.h"

#include "rawlab/ai/OnnxSession.h"
#include "rawlab/raw/BayerProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rawlab::ai {
namespace {

constexpr int tileSize = 256;
constexpr int overlap = 64;
constexpr int stride = tileSize - overlap;

int colorIndex(const rawlab::raw::CfaColor color)
{
  switch(color)
  {
    case rawlab::raw::CfaColor::Red: return 0;
    case rawlab::raw::CfaColor::Green: return 1;
    case rawlab::raw::CfaColor::Blue: return 2;
    case rawlab::raw::CfaColor::Unknown: break;
  }
  throw std::invalid_argument{"RawForge requires a Bayer CFA"};
}

std::array<float, 9> invert(const std::array<float, 9> &m)
{
  const float determinant = m[0] * (m[4] * m[8] - m[5] * m[7])
                            - m[1] * (m[3] * m[8] - m[5] * m[6])
                            + m[2] * (m[3] * m[7] - m[4] * m[6]);
  if(std::fabs(determinant) < 1e-10f) throw std::invalid_argument{"Camera color matrix is singular"};
  const float d = 1.0f / determinant;
  return {
    (m[4] * m[8] - m[5] * m[7]) * d, (m[2] * m[7] - m[1] * m[8]) * d,
    (m[1] * m[5] - m[2] * m[4]) * d, (m[5] * m[6] - m[3] * m[8]) * d,
    (m[0] * m[8] - m[2] * m[6]) * d, (m[2] * m[3] - m[0] * m[5]) * d,
    (m[3] * m[7] - m[4] * m[6]) * d, (m[1] * m[6] - m[0] * m[7]) * d,
    (m[0] * m[4] - m[1] * m[3]) * d,
  };
}

std::array<float, 3> multiply(const std::array<float, 9> &m, const std::array<float, 3> &v)
{
  return {
    m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
    m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
    m[6] * v[0] + m[7] * v[1] + m[8] * v[2],
  };
}

std::array<float, 9> multiply(const std::array<float, 9> &a, const std::array<float, 9> &b)
{
  std::array<float, 9> result{};
  for(int row = 0; row < 3; ++row)
    for(int column = 0; column < 3; ++column)
      for(int k = 0; k < 3; ++k)
        result[static_cast<std::size_t>(row * 3 + column)]
          += a[static_cast<std::size_t>(row * 3 + k)]
             * b[static_cast<std::size_t>(k * 3 + column)];
  return result;
}

float convolve5(const std::vector<float> &mosaic, const int width, const int height,
                 const int x, const int y, const std::array<float, 25> &kernel)
{
  // Reflect across the edge without repeating the edge pixel, preserving Bayer
  // parity. Clamping substitutes a different CFA color outside the image.
  const auto mirror = [](int coordinate, int extent) {
    const int period = 2 * (extent - 1);
    coordinate = std::abs(coordinate) % period;
    return coordinate < extent ? coordinate : period - coordinate;
  };
  float result = 0.0f;
  for(int ky = -2; ky <= 2; ++ky)
    for(int kx = -2; kx <= 2; ++kx)
    {
      const int sx = mirror(x + kx, width);
      const int sy = mirror(y + ky, height);
      result += mosaic[static_cast<std::size_t>(sy) * width + sx]
                * kernel[static_cast<std::size_t>((ky + 2) * 5 + kx + 2)];
    }
  return result;
}

const std::array<float, 25> greenAtRedBlue = {
  0, 0, -1.0f / 8, 0, 0,
  0, 0, 2.0f / 8, 0, 0,
  -1.0f / 8, 2.0f / 8, 4.0f / 8, 2.0f / 8, -1.0f / 8,
  0, 0, 2.0f / 8, 0, 0,
  0, 0, -1.0f / 8, 0, 0,
};

const std::array<float, 25> horizontalAtGreen = {
  0, 0, 0.5f / 8, 0, 0,
  0, -1.0f / 8, 0, -1.0f / 8, 0,
  -1.0f / 8, 4.0f / 8, 5.0f / 8, 4.0f / 8, -1.0f / 8,
  0, -1.0f / 8, 0, -1.0f / 8, 0,
  0, 0, 0.5f / 8, 0, 0,
};

const std::array<float, 25> verticalAtGreen = {
  0, 0, -1.0f / 8, 0, 0,
  0, -1.0f / 8, 4.0f / 8, -1.0f / 8, 0,
  0.5f / 8, 0, 5.0f / 8, 0, 0.5f / 8,
  0, -1.0f / 8, 4.0f / 8, -1.0f / 8, 0,
  0, 0, -1.0f / 8, 0, 0,
};

const std::array<float, 25> redAtBlue = {
  0, 0, -1.5f / 8, 0, 0,
  0, 2.0f / 8, 0, 2.0f / 8, 0,
  -1.5f / 8, 0, 6.0f / 8, 0, -1.5f / 8,
  0, 2.0f / 8, 0, 2.0f / 8, 0,
  0, 0, -1.5f / 8, 0, 0,
};

const std::array<float, 9> xyzToRec2020 = {
  1.7166512f, -0.3556708f, -0.2533663f,
  -0.6666844f, 1.6164812f, 0.0157685f,
  0.0176399f, -0.0427706f, 0.9421031f,
};

std::vector<int> tileStarts(const int size)
{
  if(size <= tileSize) return {0};
  std::vector<int> starts;
  for(int start = 0; start < size - tileSize; start += stride) starts.push_back(start);
  if(starts.empty() || starts.back() != size - tileSize) starts.push_back(size - tileSize);
  return starts;
}

float tileWeight(const int coordinate, const int start, const int extent)
{
  float weight = 1.0f;
  if(start > 0 && coordinate < overlap)
    weight = std::min(weight, static_cast<float>(coordinate + 1) / overlap);
  if(start + tileSize < extent && coordinate >= tileSize - overlap)
    weight = std::min(weight, static_cast<float>(tileSize - coordinate) / overlap);
  return weight;
}

} // namespace

std::vector<float> RawForgeAdapter::demosaicToLinearRec2020(const rawlab::raw::RawImage &source)
{
  if(!source.metadata().cfa.isBayer) throw std::invalid_argument{"RawForge requires a Bayer CFA"};
  const int width = source.width();
  const int height = source.height();
  if(width < 2 || height < 2) throw std::invalid_argument{"RawForge image is too small"};
  const std::size_t planeSize = static_cast<std::size_t>(width) * height;
  std::vector<float> normalizedMosaic(planeSize);
  const auto normalized = [&source](const int x, const int y) {
    const int site = rawlab::raw::siteIndex(x, y);
    return rawlab::raw::normalizeRawSample(source.sample(x, y),
      source.metadata().blackLevel[static_cast<std::size_t>(site)], source.metadata().whiteLevel);
  };
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
      normalizedMosaic[static_cast<std::size_t>(y) * width + x] = normalized(x, y);

  const std::array<float, 9> cameraToRec2020 = multiply(
    xyzToRec2020, source.metadata().cameraToXyz);
  std::vector<float> rec2020Mosaic(planeSize);
  for(int y = 0; y < height; ++y)
  {
    for(int x = 0; x < width; ++x)
    {
      const int blockX = std::min(x & ~1, width - 2);
      const int blockY = std::min(y & ~1, height - 2);
      float red = 0.0f;
      float blue = 0.0f;
      float greenSum = 0.0f;
      int greenCount = 0;
      for(int dy = 0; dy < 2; ++dy)
        for(int dx = 0; dx < 2; ++dx)
        {
          const int sx = blockX + dx;
          const int sy = blockY + dy;
          const float value = normalizedMosaic[static_cast<std::size_t>(sy) * width + sx];
          switch(rawlab::raw::colorAt(source.metadata().cfa, sx, sy))
          {
            case rawlab::raw::CfaColor::Red: red = value; break;
            case rawlab::raw::CfaColor::Green: greenSum += value; ++greenCount; break;
            case rawlab::raw::CfaColor::Blue: blue = value; break;
            case rawlab::raw::CfaColor::Unknown: break;
          }
        }
      if(greenCount != 2) throw std::invalid_argument{"Invalid Bayer CFA block"};
      const auto color = rawlab::raw::colorAt(source.metadata().cfa, x, y);
      const float green = color == rawlab::raw::CfaColor::Green
        ? normalizedMosaic[static_cast<std::size_t>(y) * width + x]
        : greenSum * 0.5f;
      const auto transformed = multiply(cameraToRec2020, std::array<float, 3>{red, green, blue});
      rec2020Mosaic[static_cast<std::size_t>(y) * width + x]
        = transformed[static_cast<std::size_t>(colorIndex(color))];
    }
  }

  std::vector<float> rec2020(3 * planeSize);
  for(int y = 0; y < height; ++y)
  {
    for(int x = 0; x < width; ++x)
    {
      const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
      const auto siteColor = rawlab::raw::colorAt(source.metadata().cfa, x, y);
      for(int channel = 0; channel < 3; ++channel)
      {
        const auto targetColor = channel == 0 ? rawlab::raw::CfaColor::Red
          : channel == 1 ? rawlab::raw::CfaColor::Green : rawlab::raw::CfaColor::Blue;
        float value = 0.0f;
        if(siteColor == targetColor) value = rec2020Mosaic[pixel];
        else if(siteColor != rawlab::raw::CfaColor::Green
                && targetColor == rawlab::raw::CfaColor::Green)
          value = convolve5(rec2020Mosaic, width, height, x, y, greenAtRedBlue);
        else if(siteColor == rawlab::raw::CfaColor::Green)
        {
          const bool targetIsHorizontal = (x > 0 && rawlab::raw::colorAt(
            source.metadata().cfa, x - 1, y) == targetColor)
            || (x + 1 < width && rawlab::raw::colorAt(
              source.metadata().cfa, x + 1, y) == targetColor);
          value = convolve5(rec2020Mosaic, width, height, x, y,
                            targetIsHorizontal ? horizontalAtGreen : verticalAtGreen);
        }
        else value = convolve5(rec2020Mosaic, width, height, x, y, redAtBlue);
        rec2020[static_cast<std::size_t>(channel) * planeSize + pixel]
          = std::clamp(value, 0.0f, 1.0f);
      }
    }
  }
  return rec2020;
}

rawlab::raw::RawImage RawForgeAdapter::remosaicFromLinearRec2020(
  const rawlab::raw::RawImage &source, const std::vector<float> &rec2020)
{
  const std::size_t planeSize = static_cast<std::size_t>(source.width()) * source.height();
  if(rec2020.size() != 3 * planeSize) throw std::invalid_argument{"RawForge RGB size mismatch"};
  const std::array<float, 9> rec2020ToCamera = invert(multiply(
    xyzToRec2020, source.metadata().cameraToXyz));
  std::vector<float> cameraRgb(3 * planeSize);
  for(std::size_t pixel = 0; pixel < planeSize; ++pixel)
  {
    const auto converted = multiply(rec2020ToCamera, std::array<float, 3>{
      rec2020[pixel], rec2020[planeSize + pixel], rec2020[2 * planeSize + pixel]});
    for(int channel = 0; channel < 3; ++channel)
      cameraRgb[static_cast<std::size_t>(channel) * planeSize + pixel]
        = std::clamp(converted[static_cast<std::size_t>(channel)], 0.0f, 1.0f);
  }
  return {source.width(), source.height(),
          rawlab::raw::remosaicCamRgbToCfa(source, cameraRgb, 1.0f), source.metadata()};
}

rawlab::raw::RawImage RawForgeAdapter::run(const rawlab::raw::RawImage &source,
                                           const OnnxSession &session,
                                           const float iso,
                                           const bool capIso,
                                           const ProgressCallback &progress) const
{
  const auto &info = session.modelInfo();
  const auto inputInfo = std::find_if(info.inputs.begin(), info.inputs.end(), [](const OnnxTensorInfo &item) {
    return item.name == "input" && item.type == "tensor(float16)";
  });
  const auto condition = std::find_if(info.inputs.begin(), info.inputs.end(), [](const OnnxTensorInfo &item) {
    return item.name == "cond" && item.type == "tensor(float16)";
  });
  if(info.inputs.size() != 2 || info.outputs.size() != 1
     || inputInfo == info.inputs.end() || condition == info.inputs.end()
     || info.outputs[0].name != "output" || info.outputs[0].type != "tensor(float16)")
    throw std::invalid_argument{"Unexpected RawForge ONNX contract"};

  const auto &active = source.metadata().activeArea;
  const int cropLeft = std::clamp(active.width > 0 ? active.left : 0, 0,
                                  std::max(0, source.width() - 2));
  const int cropTop = std::clamp(active.height > 0 ? active.top : 0, 0,
                                 std::max(0, source.height() - 2));
  int cropWidth = std::min(active.width > 0 ? active.width : source.width(),
                           source.width() - cropLeft);
  int cropHeight = std::min(active.height > 0 ? active.height : source.height(),
                            source.height() - cropTop);
  cropWidth -= cropWidth % 2;
  cropHeight -= cropHeight % 2;
  if(cropWidth < 2 || cropHeight < 2) throw std::invalid_argument{"RawForge active area is too small"};

  rawlab::raw::RawMetadata cropMetadata = source.metadata();
  cropMetadata.activeArea = {0, 0, cropWidth, cropHeight};
  for(int y = 0; y < 2; ++y)
    for(int x = 0; x < 2; ++x)
    {
      cropMetadata.cfa.cells[y][x] = rawlab::raw::colorAt(
        source.metadata().cfa, cropLeft + x, cropTop + y);
      cropMetadata.blackLevel[static_cast<std::size_t>(y * 2 + x)]
        = source.metadata().blackLevel[static_cast<std::size_t>(
          rawlab::raw::siteIndex(cropLeft + x, cropTop + y))];
    }
  std::vector<float> cropSamples(static_cast<std::size_t>(cropWidth) * cropHeight);
  for(int y = 0; y < cropHeight; ++y)
    for(int x = 0; x < cropWidth; ++x)
      cropSamples[static_cast<std::size_t>(y) * cropWidth + x]
        = source.sample(cropLeft + x, cropTop + y);
  const rawlab::raw::RawImage workingSource{
    cropWidth, cropHeight, std::move(cropSamples), std::move(cropMetadata)};

  const int width = workingSource.width();
  const int height = workingSource.height();
  const std::size_t planeSize = static_cast<std::size_t>(width) * height;
  const std::vector<float> input = demosaicToLinearRec2020(workingSource);
  std::vector<float> output(3 * planeSize, 0.0f);
  std::vector<float> weights(planeSize, 0.0f);
  const std::vector<int> startsX = tileStarts(width);
  const std::vector<int> startsY = tileStarts(height);
  const int total = static_cast<int>(startsX.size() * startsY.size());
  int completed = 0;

  for(const int startY : startsY)
  {
    for(const int startX : startsX)
    {
      std::vector<float> tile(3 * tileSize * tileSize);
      for(int channel = 0; channel < 3; ++channel)
        for(int y = 0; y < tileSize; ++y)
          for(int x = 0; x < tileSize; ++x)
          {
            const int sourceX = std::clamp(startX + x, 0, width - 1);
            const int sourceY = std::clamp(startY + y, 0, height - 1);
            tile[static_cast<std::size_t>(channel * tileSize * tileSize + y * tileSize + x)]
              = input[static_cast<std::size_t>(channel) * planeSize
                      + static_cast<std::size_t>(sourceY) * width + sourceX];
          }
      const auto result = session.runFloat({
        {"input", std::move(tile), {1, 3, tileSize, tileSize}},
        {"cond", {std::clamp(iso, 0.0f, capIso ? 65535.0f
                                                : std::numeric_limits<float>::max()) / 6400.0f}, {1, 1}},
      });
      if(result.size() != 1 || result.front().values.size() != 3 * tileSize * tileSize)
        throw std::runtime_error{"Unexpected RawForge output shape"};
      for(int y = 0; y < tileSize && startY + y < height; ++y)
        for(int x = 0; x < tileSize && startX + x < width; ++x)
        {
          const std::size_t pixel = static_cast<std::size_t>(startY + y) * width + startX + x;
          const float weight = tileWeight(x, startX, width) * tileWeight(y, startY, height);
          weights[pixel] += weight;
          for(int channel = 0; channel < 3; ++channel)
            output[static_cast<std::size_t>(channel) * planeSize + pixel]
              += result.front().values[static_cast<std::size_t>(channel * tileSize * tileSize
                                                                 + y * tileSize + x)] * weight;
        }
      if(progress) progress(++completed, total);
    }
  }
  for(std::size_t pixel = 0; pixel < planeSize; ++pixel)
    for(int channel = 0; channel < 3; ++channel)
      output[static_cast<std::size_t>(channel) * planeSize + pixel] /= weights[pixel];
  const rawlab::raw::RawImage processedCrop = remosaicFromLinearRec2020(workingSource, output);
  std::vector<float> samples = source.samples();
  for(int y = 0; y < cropHeight; ++y)
    for(int x = 0; x < cropWidth; ++x)
      samples[static_cast<std::size_t>(cropTop + y) * source.width() + cropLeft + x]
        = processedCrop.sample(x, y);
  return {source.width(), source.height(), std::move(samples), source.metadata()};
}

}
