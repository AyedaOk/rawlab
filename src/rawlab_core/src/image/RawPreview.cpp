#include "rawlab/image/RawPreview.h"

#include "rawlab/image/AmazeDemosaic.h"

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

// Adapted from darktable's PPG Bayer demosaic (src/iop/demosaicing/ppg.c).
// Copyright (C) 2010-2026 darktable developers. Distributed under the GPLv3.
// This preview-only adaptation keeps rawlab's CFA and color-management path.

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

using CameraRgb = std::array<float, 3>;

[[maybe_unused]] std::vector<CameraRgb> demosaicPpg(const raw::RawImage &source,
                                   const raw::ActiveArea &area)
{
  const int width = area.width;
  const int height = area.height;
  std::vector<float> input(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  std::vector<CameraRgb> output(input.size());
  const auto index = [width](const int x, const int y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
           + static_cast<std::size_t>(x);
  };
  const auto cfa = [&source, &area](const int x, const int y) {
    return raw::colorAt(source.metadata().cfa, area.left + x, area.top + y);
  };
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
      input[index(x, y)] = normalizedSample(source, area, area.left + x, area.top + y);

  // This is the border pass from Darktable PPG. It makes the later 4/6-star
  // interpolation safe without inventing a second CFA pattern at the edge.
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
    {
      float sum[3] = {};
      int count[3] = {};
      for(int dy = -1; dy <= 1; ++dy)
        for(int dx = -1; dx <= 1; ++dx)
        {
          const int nx = x + dx;
          const int ny = y + dy;
          if(nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
          const int channel = colorIndex(cfa(nx, ny));
          sum[channel] += input[index(nx, ny)];
          ++count[channel];
        }
      const int own = colorIndex(cfa(x, y));
      for(int channel = 0; channel < 3; ++channel)
        output[index(x, y)][static_cast<std::size_t>(channel)] =
          channel == own ? input[index(x, y)] : (count[channel] ? sum[channel] / count[channel] : 0.0f);
    }

  // PPG stage 1: edge-directed green at red/blue sites, while preserving the
  // measured primary channel. The 3-pixel guard is the PPG kernel radius.
  for(int y = 3; y < height - 3; ++y)
    for(int x = 3; x < width - 3; ++x)
    {
      const int channel = colorIndex(cfa(x, y));
      if(channel != 0 && channel != 2) continue;
      const float pc = input[index(x, y)];
      const auto v = [&](const int dx, const int dy) { return input[index(x + dx, y + dy)]; };
      const float guessX = (v(-1, 0) + pc + v(1, 0)) * 2.0f - v(-2, 0) - v(2, 0);
      const float diffX = (std::fabs(v(-2, 0) - pc) + std::fabs(v(2, 0) - pc)
                          + std::fabs(v(-1, 0) - v(1, 0))) * 3.0f
                         + (std::fabs(v(-3, 0) - v(-1, 0))
                            + std::fabs(v(3, 0) - v(1, 0))) * 2.0f;
      const float guessY = (v(0, -1) + pc + v(0, 1)) * 2.0f - v(0, -2) - v(0, 2);
      const float diffY = (std::fabs(v(0, -2) - pc) + std::fabs(v(0, 2) - pc)
                          + std::fabs(v(0, -1) - v(0, 1))) * 3.0f
                         + (std::fabs(v(0, -3) - v(0, -1))
                            + std::fabs(v(0, 3) - v(0, 1))) * 2.0f;
      const bool useY = diffX > diffY;
      const float guess = (useY ? guessY : guessX) * 0.25f;
      const float lo = std::min(useY ? v(0, -1) : v(-1, 0), useY ? v(0, 1) : v(1, 0));
      const float hi = std::max(useY ? v(0, -1) : v(-1, 0), useY ? v(0, 1) : v(1, 0));
      output[index(x, y)][1] = std::clamp(guess, lo, hi);
    }

  // PPG stage 2: color-difference interpolation from the completed green
  // buffer. This is deliberately separate from stage 1; doing it per output
  // pixel was the source of the old low-resolution color bias.
  for(int y = 1; y < height - 1; ++y)
    for(int x = 1; x < width - 1; ++x)
    {
      const int channel = colorIndex(cfa(x, y));
      auto &pixel = output[index(x, y)];
      if(channel == 1)
      {
        const bool redNext = colorIndex(cfa(x + 1, y)) == 0;
        const auto &top = output[index(x, y - 1)];
        const auto &bottom = output[index(x, y + 1)];
        const auto &left = output[index(x - 1, y)];
        const auto &right = output[index(x + 1, y)];
        if(redNext)
        {
          pixel[2] = (top[2] + bottom[2] + 2.0f * pixel[1] - top[1] - bottom[1]) * 0.5f;
          pixel[0] = (left[0] + right[0] + 2.0f * pixel[1] - left[1] - right[1]) * 0.5f;
        }
        else
        {
          pixel[0] = (top[0] + bottom[0] + 2.0f * pixel[1] - top[1] - bottom[1]) * 0.5f;
          pixel[2] = (left[2] + right[2] + 2.0f * pixel[1] - left[1] - right[1]) * 0.5f;
        }
      }
      else
      {
        const auto &tl = output[index(x - 1, y - 1)];
        const auto &tr = output[index(x + 1, y - 1)];
        const auto &bl = output[index(x - 1, y + 1)];
        const auto &br = output[index(x + 1, y + 1)];
        const int missing = channel == 0 ? 2 : 0;
        const float d1 = std::fabs(tl[missing] - br[missing])
                         + std::fabs(tl[1] - pixel[1]) + std::fabs(br[1] - pixel[1]);
        const float d2 = std::fabs(tr[missing] - bl[missing])
                         + std::fabs(tr[1] - pixel[1]) + std::fabs(bl[1] - pixel[1]);
        const float g1 = tl[missing] + br[missing] + 2.0f * pixel[1] - tl[1] - br[1];
        const float g2 = tr[missing] + bl[missing] + 2.0f * pixel[1] - tr[1] - bl[1];
        pixel[static_cast<std::size_t>(missing)] =
          d1 > d2 ? g2 * 0.5f : d1 < d2 ? g1 * 0.5f : (g1 + g2) * 0.25f;
      }
    }
  for(auto &pixel : output) for(float &channel : pixel) channel = std::max(0.0f, channel);
  return output;
}

CameraRgb demosaicHalfSize(const raw::RawImage &source,
                           const raw::ActiveArea &area,
                           const int x, const int y)
{
  int left = x - ((x - area.left) & 1);
  int top = y - ((y - area.top) & 1);
  left = std::clamp(left, area.left, area.left + area.width - 2);
  top = std::clamp(top, area.top, area.top + area.height - 2);

  CameraRgb result{};
  for(int dy = 0; dy < 2; ++dy)
    for(int dx = 0; dx < 2; ++dx)
    {
      const int channel = colorIndex(raw::colorAt(source.metadata().cfa, left + dx, top + dy));
      result[static_cast<std::size_t>(channel)] +=
        normalizedSample(source, area, left + dx, top + dy);
    }
  result[1] *= 0.5f;
  return result;
}

std::array<float, 3> cameraToSrgb(const raw::RawMetadata &metadata,
                                   std::array<float, 3> camera,
                                   const bool alreadyWhiteBalanced)
{
  if(!alreadyWhiteBalanced)
    for(std::size_t i = 0; i < camera.size(); ++i)
      camera[i] /= std::max(metadata.asShotNeutral[i], 0.0001f);

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
  // Roll values above white gently below display white instead of hard-clipping.
  if(value > 1.0f) value = 1.0f - 0.08f * (value - 1.0f) / value;
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
  const std::vector<CameraRgb> amaze = quality == PreviewQuality::High
    ? demosaicAmaze(source, area) : std::vector<CameraRgb>{};
  std::vector<std::array<float, 3>> linear(static_cast<std::size_t>(width)
                                           * static_cast<std::size_t>(height));
  parallelRows(height, linear.size(), [&](const int begin, const int end, const int worker) {
    (void)worker;
    for(int y = begin; y < end; ++y)
    {
      for(int x = 0; x < width; ++x)
      {
        if(quality == PreviewQuality::High)
        {
          // Integrate the whole pixel footprint in linear camera RGB. Four
          // point samples miss fine patterns when heavily reducing a RAW.
          const auto p0 = geometry.previewToRaw(static_cast<float>(x), static_cast<float>(y));
          const auto p1 = geometry.previewToRaw(static_cast<float>(x + 1), static_cast<float>(y + 1));
          const float left = std::max(static_cast<float>(area.left), std::min(p0.first, p1.first));
          const float right = std::min(static_cast<float>(area.left + area.width), std::max(p0.first, p1.first));
          const float top = std::max(static_cast<float>(area.top), std::min(p0.second, p1.second));
          const float bottom = std::min(static_cast<float>(area.top + area.height), std::max(p0.second, p1.second));
          std::array<float, 3> camera{};
          float total = 0.0f;
          for(int sy = static_cast<int>(std::floor(top)); sy < static_cast<int>(std::ceil(bottom)); ++sy)
            for(int sx = static_cast<int>(std::floor(left)); sx < static_cast<int>(std::ceil(right)); ++sx)
            {
              const float weight = (std::min(right, static_cast<float>(sx + 1)) - std::max(left, static_cast<float>(sx)))
                * (std::min(bottom, static_cast<float>(sy + 1)) - std::max(top, static_cast<float>(sy)));
              const auto &pixel = amaze[static_cast<std::size_t>(sy - area.top) * area.width + sx - area.left];
              for(std::size_t c = 0; c < 3; ++c) camera[c] += pixel[c] * weight;
              total += weight;
            }
          for(float &c : camera) c /= std::max(total, 1e-12f);
          linear[static_cast<std::size_t>(y) * width + x] = cameraToSrgb(source.metadata(), camera, true);
          continue;
        }
        const int samplesPerAxis = 1;
        std::array<float, 3> rgb{};
        for(int sampleY = 0; sampleY < samplesPerAxis; ++sampleY)
        {
          for(int sampleX = 0; sampleX < samplesPerAxis; ++sampleX)
          {
            const float offsetX = (static_cast<float>(sampleX) + 0.5f)
                                  / static_cast<float>(samplesPerAxis);
            const float offsetY = (static_cast<float>(sampleY) + 0.5f)
                                  / static_cast<float>(samplesPerAxis);
            const auto [rawXf, rawYf] = geometry.previewToRaw(
              static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY);
            const int rawX = std::clamp(static_cast<int>(rawXf), area.left, area.left + area.width - 1);
            const int rawY = std::clamp(static_cast<int>(rawYf), area.top, area.top + area.height - 1);
            const std::array<float, 3> camera = quality == PreviewQuality::High
              ? amaze[static_cast<std::size_t>(rawY - area.top) * static_cast<std::size_t>(area.width)
                      + static_cast<std::size_t>(rawX - area.left)]
              : demosaicHalfSize(source, area, rawX, rawY);
            const std::array<float, 3> sample = cameraToSrgb(
              source.metadata(), camera, quality == PreviewQuality::High);
            for(std::size_t channel = 0; channel < rgb.size(); ++channel)
              rgb[channel] += sample[channel];
          }
        }
        const float sampleScale = 1.0f / static_cast<float>(samplesPerAxis * samplesPerAxis);
        for(float &channel : rgb) channel *= sampleScale;
        linear[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
               + static_cast<std::size_t>(x)] = rgb;
      }
    }
  });

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
        std::array<float, 3> rgb = linear[i];
        if(quality == PreviewQuality::Fast && x > 0 && y > 0 && x + 1 < width && y + 1 < height)
        {
          const auto luma = [&linear, width](const int px, const int py) {
            const auto &pixel = linear[static_cast<std::size_t>(py) * static_cast<std::size_t>(width)
                                       + static_cast<std::size_t>(px)];
            return 0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2];
          };
          const float detail = luma(x, y) - (luma(x - 1, y) + luma(x + 1, y)
                                            + luma(x, y - 1) + luma(x, y + 1)) * 0.25f;
          const float amount = quality == PreviewQuality::High ? 0.08f : 0.04f;
          for(float &channel : rgb) channel = std::max(0.0f, channel + amount * detail);
        }
        preview.pixels[3 * i] = displayByte(rgb[0]);
        preview.pixels[3 * i + 1] = displayByte(rgb[1]);
        preview.pixels[3 * i + 2] = displayByte(rgb[2]);
      }
    }
  });
  return RawPreview{std::move(preview), geometry};
}


}
