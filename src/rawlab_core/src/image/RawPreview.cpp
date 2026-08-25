#include "rawlab/image/RawPreview.h"

#include "rawlab/raw/RawInspectInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace rawlab::image {
namespace {

raw::ActiveArea boundedActiveArea(const raw::RawImage &source)
{
  raw::ActiveArea area = source.metadata().activeArea;
  if(area.width <= 0 || area.height <= 0) area = {0, 0, source.width(), source.height()};
  area.left = std::clamp(area.left, 0, source.width() - 1);
  area.top = std::clamp(area.top, 0, source.height() - 1);
  area.width = std::clamp(area.width, 1, source.width() - area.left);
  area.height = std::clamp(area.height, 1, source.height() - area.top);
  return area;
}

int colorIndex(const raw::CfaColor color)
{
  if(color == raw::CfaColor::Red) return 0;
  if(color == raw::CfaColor::Green) return 1;
  if(color == raw::CfaColor::Blue) return 2;
  throw std::runtime_error{"Cannot render a non-Bayer raw preview"};
}

float normalizedSample(const raw::RawImage &source, const raw::ActiveArea &area,
                       const int x, const int y)
{
  const int sx = std::clamp(x, area.left, area.left + area.width - 1);
  const int sy = std::clamp(y, area.top, area.top + area.height - 1);
  const float black = source.metadata().blackLevel[static_cast<std::size_t>(raw::siteIndex(sx, sy))];
  const float range = std::max(1.0f, source.metadata().whiteLevel - black);
  return std::clamp((source.sample(sx, sy) - black) / range, 0.0f, 1.5f);
}

float interpolateColor(const raw::RawImage &source,
                       const raw::ActiveArea &area,
                       const int x,
                       const int y,
                       const int wantedColor)
{
  if(colorIndex(raw::colorAt(source.metadata().cfa, x, y)) == wantedColor)
  {
    return normalizedSample(source, area, x, y);
  }

  for(int radius = 1; radius <= 2; ++radius)
  {
    float weightedSum = 0.0f;
    float weightSum = 0.0f;
    for(int dy = -radius; dy <= radius; ++dy)
    {
      for(int dx = -radius; dx <= radius; ++dx)
      {
        if(dx == 0 && dy == 0) continue;
        const int sx = std::clamp(x + dx, area.left, area.left + area.width - 1);
        const int sy = std::clamp(y + dy, area.top, area.top + area.height - 1);
        if(colorIndex(raw::colorAt(source.metadata().cfa, sx, sy)) != wantedColor) continue;
        const float weight = 1.0f / static_cast<float>(std::abs(dx) + std::abs(dy));
        weightedSum += normalizedSample(source, area, sx, sy) * weight;
        weightSum += weight;
      }
    }
    if(weightSum > 0.0f) return weightedSum / weightSum;
  }
  return 0.0f;
}

std::array<float, 3> highQualityCameraRgb(const raw::RawImage &source,
                                          const raw::ActiveArea &area,
                                          const int x, const int y)
{
  const auto sample = [&source, &area, x, y](const int dx, const int dy) {
    return normalizedSample(source, area, x + dx, y + dy);
  };
  const float center = sample(0, 0);
  const auto greenAtRedOrBlue = [&] {
    return (4.0f * center
            + 2.0f * (sample(-1, 0) + sample(1, 0) + sample(0, -1) + sample(0, 1))
            - sample(-2, 0) - sample(2, 0) - sample(0, -2) - sample(0, 2)) / 8.0f;
  };
  const auto oppositeAtRedOrBlue = [&] {
    return (6.0f * center
            + 2.0f * (sample(-1, -1) + sample(1, -1) + sample(-1, 1) + sample(1, 1))
            - 1.5f * (sample(-2, 0) + sample(2, 0) + sample(0, -2) + sample(0, 2))) / 8.0f;
  };
  const auto atGreen = [&](const bool horizontal) {
    const float nearAxis = horizontal
      ? sample(-1, 0) + sample(1, 0) : sample(0, -1) + sample(0, 1);
    const float farAxis = horizontal
      ? sample(-2, 0) + sample(2, 0) : sample(0, -2) + sample(0, 2);
    const float crossFar = horizontal
      ? sample(0, -2) + sample(0, 2) : sample(-2, 0) + sample(2, 0);
    const float diagonals = sample(-1, -1) + sample(1, -1)
                            + sample(-1, 1) + sample(1, 1);
    return (5.0f * center + 4.0f * nearAxis - farAxis - diagonals + 0.5f * crossFar) / 8.0f;
  };

  std::array<float, 3> camera{};
  const raw::CfaColor color = raw::colorAt(source.metadata().cfa, x, y);
  if(color == raw::CfaColor::Red)
  {
    camera = {center, greenAtRedOrBlue(), oppositeAtRedOrBlue()};
  }
  else if(color == raw::CfaColor::Blue)
  {
    camera = {oppositeAtRedOrBlue(), greenAtRedOrBlue(), center};
  }
  else
  {
    const bool redHorizontal = raw::colorAt(source.metadata().cfa, x - 1, y)
                               == raw::CfaColor::Red;
    camera = redHorizontal
      ? std::array<float, 3>{atGreen(true), center, atGreen(false)}
      : std::array<float, 3>{atGreen(false), center, atGreen(true)};
  }
  for(float &channel : camera) channel = std::clamp(channel, 0.0f, 1.5f);
  return camera;
}

std::array<float, 3> cameraToSrgb(const raw::RawMetadata &metadata,
                                  std::array<float, 3> camera)
{
  for(std::size_t i = 0; i < camera.size(); ++i)
  {
    camera[i] /= std::max(metadata.asShotNeutral[i], 0.0001f);
  }

  const auto &m = metadata.cameraToSrgb;
  std::array<float, 3> rgb{
    m[0] * camera[0] + m[1] * camera[1] + m[2] * camera[2],
    m[3] * camera[0] + m[4] * camera[1] + m[5] * camera[2],
    m[6] * camera[0] + m[7] * camera[1] + m[8] * camera[2],
  };

  // Some cameras expose no usable matrix. Keep the preview useful in that case.
  if(!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2])
     || std::max({rgb[0], rgb[1], rgb[2]}) <= 0.0f)
  {
    rgb = camera;
  }
  return rgb;
}

std::uint8_t displayByte(float value)
{
  value = std::clamp(value, 0.0f, 1.0f);
  value = value <= 0.0031308f ? 12.92f * value
                             : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(255.0f * std::clamp(value, 0.0f, 1.0f)));
}

template<typename Function>
void parallelRows(const int height, const std::size_t pixelCount, Function function)
{
  const unsigned available = std::max(1U, std::thread::hardware_concurrency());
  const int workers = pixelCount < 200'000
    ? 1 : std::min<int>({height, static_cast<int>(available), 4});
  if(workers == 1) { function(0, height, 0); return; }
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(workers));
  for(int worker = 0; worker < workers; ++worker)
  {
    const int begin = height * worker / workers;
    const int end = height * (worker + 1) / workers;
    threads.emplace_back([=, &function] { function(begin, end, worker); });
  }
  for(std::thread &thread : threads) thread.join();
}

} // namespace

std::pair<float, float> PreviewGeometry::rawToPreview(const float rawX, const float rawY) const
{
  if(rawArea.width <= 0 || rawArea.height <= 0 || previewWidth <= 0 || previewHeight <= 0)
  {
    throw std::runtime_error{"Invalid preview geometry"};
  }
  const float sx = std::clamp((rawX - static_cast<float>(rawArea.left))
                                / static_cast<float>(rawArea.width), 0.0f, 1.0f);
  const float sy = std::clamp((rawY - static_cast<float>(rawArea.top))
                                / static_cast<float>(rawArea.height), 0.0f, 1.0f);
  float u = sx;
  float v = sy;
  if(flip == 3) { u = 1.0f - sx; v = 1.0f - sy; }
  else if(flip == 5) { u = sy; v = 1.0f - sx; }
  else if(flip == 6) { u = 1.0f - sy; v = sx; }
  return {u * static_cast<float>(previewWidth), v * static_cast<float>(previewHeight)};
}

std::pair<float, float> PreviewGeometry::previewToRaw(const float previewX, const float previewY) const
{
  if(rawArea.width <= 0 || rawArea.height <= 0 || previewWidth <= 0 || previewHeight <= 0)
  {
    throw std::runtime_error{"Invalid preview geometry"};
  }
  const float u = std::clamp(previewX / static_cast<float>(previewWidth), 0.0f, 1.0f);
  const float v = std::clamp(previewY / static_cast<float>(previewHeight), 0.0f, 1.0f);
  float sx = u;
  float sy = v;
  if(flip == 3) { sx = 1.0f - u; sy = 1.0f - v; }
  else if(flip == 5) { sx = 1.0f - v; sy = u; }
  else if(flip == 6) { sx = v; sy = 1.0f - u; }
  return {
    static_cast<float>(rawArea.left) + sx * static_cast<float>(rawArea.width),
    static_cast<float>(rawArea.top) + sy * static_cast<float>(rawArea.height),
  };
}

RawPreview renderRawPreview(const raw::RawImage &source, const int maxDimension,
                            const PreviewQuality quality)
{
  if(maxDimension <= 0) throw std::invalid_argument{"Preview maximum dimension must be positive"};
  if(!source.metadata().cfa.isBayer) throw std::runtime_error{"Raw preview requires a Bayer CFA source"};
  colorIndex(raw::colorAt(source.metadata().cfa, 0, 0));
  colorIndex(raw::colorAt(source.metadata().cfa, 1, 0));
  colorIndex(raw::colorAt(source.metadata().cfa, 0, 1));
  colorIndex(raw::colorAt(source.metadata().cfa, 1, 1));

  const raw::ActiveArea area = boundedActiveArea(source);
  const bool rotated = source.metadata().flip == 5 || source.metadata().flip == 6;
  const int orientedWidth = rotated ? area.height : area.width;
  const int orientedHeight = rotated ? area.width : area.height;
  const float scale = std::min(1.0f, static_cast<float>(maxDimension)
                                      / static_cast<float>(std::max(orientedWidth, orientedHeight)));
  const int width = std::max(1, static_cast<int>(std::lround(orientedWidth * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(orientedHeight * scale)));
  const PreviewGeometry geometry{area, width, height, source.metadata().flip};

  std::vector<std::array<float, 3>> linear(static_cast<std::size_t>(width)
                                           * static_cast<std::size_t>(height));
  constexpr int maximumWorkers = 4;
  std::array<std::array<int, 1024>, maximumWorkers> workerHistograms{};
  parallelRows(height, linear.size(), [&](const int begin, const int end, const int worker) {
    auto &histogram = workerHistograms[static_cast<std::size_t>(worker)];
    for(int y = begin; y < end; ++y)
    {
      for(int x = 0; x < width; ++x)
      {
        const auto [rawXf, rawYf] = geometry.previewToRaw(
          static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
        const int rawX = std::clamp(static_cast<int>(rawXf), area.left, area.left + area.width - 1);
        const int rawY = std::clamp(static_cast<int>(rawYf), area.top, area.top + area.height - 1);
        const std::array<float, 3> camera = quality == PreviewQuality::High
          ? highQualityCameraRgb(source, area, rawX, rawY)
          : std::array<float, 3>{interpolateColor(source, area, rawX, rawY, 0),
                                 interpolateColor(source, area, rawX, rawY, 1),
                                 interpolateColor(source, area, rawX, rawY, 2)};
        const std::array<float, 3> rgb = cameraToSrgb(source.metadata(), camera);
        linear[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x)] = rgb;
        const float peak = std::clamp(std::max({rgb[0], rgb[1], rgb[2]}), 0.0f, 1.0f);
        ++histogram[static_cast<std::size_t>(std::lround(peak * 1023.0f))];
      }
    }
  });
  std::array<int, 1024> histogram{};
  for(const auto &workerHistogram : workerHistograms)
  {
    for(std::size_t bin = 0; bin < histogram.size(); ++bin) histogram[bin] += workerHistogram[bin];
  }

  const std::size_t target = linear.size() * 99 / 100;
  std::size_t count = 0;
  int percentileBin = 1023;
  for(int i = 0; i < 1024; ++i)
  {
    count += static_cast<std::size_t>(histogram[static_cast<std::size_t>(i)]);
    if(count >= target) { percentileBin = i; break; }
  }
  const float percentile = static_cast<float>(percentileBin) / 1023.0f;
  const float gain = percentile > 0.01f ? std::clamp(0.9f / percentile, 0.25f, 8.0f) : 1.0f;

  RgbImage preview;
  preview.width = width;
  preview.height = height;
  preview.pixels.resize(linear.size() * 3);
  parallelRows(height, linear.size(), [&](const int begin, const int end, int) {
    for(int y = begin; y < end; ++y)
    {
      for(int x = 0; x < width; ++x)
      {
        const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                              + static_cast<std::size_t>(x);
        preview.pixels[3 * i] = displayByte(linear[i][0] * gain);
        preview.pixels[3 * i + 1] = displayByte(linear[i][1] * gain);
        preview.pixels[3 * i + 2] = displayByte(linear[i][2] * gain);
      }
    }
  });
  return RawPreview{std::move(preview), geometry};
}

}
