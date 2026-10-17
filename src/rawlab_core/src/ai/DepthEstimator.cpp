#include "rawlab/ai/DepthEstimator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rawlab::ai {
namespace {

// Catmull-Rom bicubic, with a widened filter when downsampling (Pillow's RGB contract).
double cubic(double x)
{
  x = std::abs(x);
  if(x < 1) return ((1.5 * x - 2.5) * x) * x + 1;
  if(x < 2) return ((-.5 * x + 2.5) * x - 4) * x + 2;
  return 0;
}

std::vector<float> resizeAxis(const std::vector<float> &input, int width, int height,
                              int target, bool horizontal)
{
  const int oldSize = horizontal ? width : height;
  const int lines = horizontal ? height : width;
  const int outputWidth = horizontal ? target : width;
  const double scale = static_cast<double>(oldSize) / target;
  const double filterScale = std::max(1.0, scale);
  std::vector<float> output(static_cast<std::size_t>(target) * lines * 3);
  for(int position = 0; position < target; ++position)
  {
    const double center = (position + .5) * scale;
    const int first = std::max(0, static_cast<int>(std::floor(center - 2 * filterScale)));
    const int last = std::min(oldSize - 1, static_cast<int>(std::ceil(center + 2 * filterScale)));
    std::vector<double> weights(static_cast<std::size_t>(last - first + 1));
    double sum = 0;
    for(int i = first; i <= last; ++i) sum += weights[i - first] = cubic((i + .5 - center) / filterScale);
    for(int line = 0; line < lines; ++line) for(int channel = 0; channel < 3; ++channel)
    {
      double value = 0;
      for(int i = first; i <= last; ++i)
      {
        const auto index = static_cast<std::size_t>(horizontal ? line * width + i : i * width + line);
        value += input[index * 3 + channel] * weights[i - first];
      }
      const auto index = static_cast<std::size_t>(horizontal ? line * outputWidth + position : position * width + line);
      output[index * 3 + channel] = static_cast<float>(std::clamp(std::round(value / sum), 0.0, 255.0));
    }
  }
  return output;
}

}

DepthEstimator::DepthEstimator(const std::filesystem::path &path, const OnnxSessionOptions &options)
  : session_{path, options}
{
  const auto &info = session_.modelInfo();
  if(info.inputs.size() != 1 || info.outputs.size() != 1
     || info.inputs[0].name != "pixel_values" || info.inputs[0].shape.size() != 4
     || info.outputs[0].name != "predicted_depth")
    throw std::runtime_error{"Expected a Depth Anything V2 ONNX model (pixel_values → predicted_depth)"};
}

pipeline::Mask DepthEstimator::estimate(const image::RgbImage &image) const
{
  if(image.width <= 0 || image.height <= 0 || image.width > 4096 || image.height > 4096
     || image.pixels.size() != static_cast<std::size_t>(image.width) * image.height * 3)
    throw std::invalid_argument{"Depth estimation requires a valid RGB preview"};
  // DPTImageProcessor: resize minimally towards 518, preserve aspect, round to multiples of 14.
  const double sx = 518.0 / image.width, sy = 518.0 / image.height;
  const double scale = std::abs(1 - sx) < std::abs(1 - sy) ? sx : sy;
  const int width = std::max(14, static_cast<int>(std::nearbyint(image.width * scale / 14)) * 14);
  const int height = std::max(14, static_cast<int>(std::nearbyint(image.height * scale / 14)) * 14);
  if(width > 2048 || height > 2048) throw std::invalid_argument{"Depth input aspect ratio exceeds the working size"};
  std::vector<float> pixels(image.pixels.begin(), image.pixels.end());
  pixels = resizeAxis(pixels, image.width, image.height, width, true);
  pixels = resizeAxis(pixels, width, image.height, height, false);
  constexpr float mean[] = {.485f, .456f, .406f}, deviation[] = {.229f, .224f, .225f};
  std::vector<float> input(pixels.size());
  const auto area = static_cast<std::size_t>(width) * height;
  for(std::size_t i = 0; i < area; ++i) for(int c = 0; c < 3; ++c)
    input[c * area + i] = (pixels[i * 3 + c] / 255.0f - mean[c]) / deviation[c];
  auto outputs = session_.runFloat({{"pixel_values", std::move(input), {1, 3, height, width}}});
  auto &result = outputs.front();
  const auto &shape = result.shape;
  if((shape.size() != 3 && shape.size() != 4) || shape[0] != 1
     || (shape.size() == 4 && shape[1] != 1))
    throw std::runtime_error{"Depth model returned an unexpected tensor shape"};
  const auto h = shape[shape.size() - 2], w = shape.back();
  if(w <= 0 || h <= 0 || w > 2048 || h > 2048 || result.values.size() != static_cast<std::size_t>(w * h))
    throw std::runtime_error{"Depth model returned invalid dimensions"};
  for(const auto value : result.values)
    if(!std::isfinite(value)) throw std::runtime_error{"Depth model returned non-finite values"};
  const auto [minimum, maximum] = std::minmax_element(result.values.begin(), result.values.end());
  const float low = *minimum, high = *maximum, span = high - low;
  if(!std::isfinite(span)) throw std::runtime_error{"Depth model returned an invalid depth range"};
  // The model predicts inverse depth. Store near-to-far values for the mask controls.
  for(auto &value : result.values) value = span > 1e-6f ? (high - value) / span : .5f;
  return {static_cast<int>(w), static_cast<int>(h), std::move(result.values)};
}

}
