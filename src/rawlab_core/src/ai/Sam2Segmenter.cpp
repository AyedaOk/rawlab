#include "rawlab/ai/Sam2Segmenter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace rawlab::ai {
namespace {

constexpr int samInputSize = 1024;
constexpr int samMaskSize = 256;
constexpr float imageMean[3] = {123.675f, 116.28f, 103.53f};
constexpr float imageStd[3] = {58.395f, 57.12f, 57.375f};

const OnnxNamedFloatTensor &tensorNamed(const std::vector<OnnxNamedFloatTensor> &tensors,
                                        const std::string_view name)
{
  const auto found = std::find_if(tensors.begin(), tensors.end(), [&](const auto &tensor) {
    return tensor.name == name;
  });
  if(found == tensors.end()) throw std::runtime_error{"Missing ONNX output: " + std::string{name}};
  return *found;
}

float bilinear(const float v00, const float v01, const float v10, const float v11,
               const float fx, const float fy)
{
  return v00 * (1.0f - fx) * (1.0f - fy) + v01 * fx * (1.0f - fy)
         + v10 * (1.0f - fx) * fy + v11 * fx * fy;
}

}

Sam2Segmenter::Sam2Segmenter(const std::filesystem::path &encoderPath,
                             const std::filesystem::path &decoderPath)
  : encoder_{encoderPath}, decoder_{decoderPath}, previousMask_(samMaskSize * samMaskSize, 0.0f)
{
}

void Sam2Segmenter::encode(const image::RgbImage &image)
{
  if(image.width <= 0 || image.height <= 0
     || image.pixels.size() != static_cast<std::size_t>(image.width * image.height * 3))
  {
    throw std::invalid_argument{"SAM encoder requires a valid RGB image"};
  }

  imageWidth_ = image.width;
  imageHeight_ = image.height;
  scale_ = static_cast<float>(samInputSize) / static_cast<float>(std::max(image.width, image.height));
  const int resizedWidth = std::min(samInputSize, static_cast<int>(image.width * scale_ + 0.5f));
  const int resizedHeight = std::min(samInputSize, static_cast<int>(image.height * scale_ + 0.5f));
  std::vector<float> input(3 * samInputSize * samInputSize, 0.0f);

  for(int y = 0; y < resizedHeight; ++y)
  {
    const float sourceY = static_cast<float>(y) / scale_;
    const int y0 = std::min(static_cast<int>(sourceY), image.height - 1);
    const int y1 = std::min(y0 + 1, image.height - 1);
    const float fy = sourceY - static_cast<float>(y0);
    for(int x = 0; x < resizedWidth; ++x)
    {
      const float sourceX = static_cast<float>(x) / scale_;
      const int x0 = std::min(static_cast<int>(sourceX), image.width - 1);
      const int x1 = std::min(x0 + 1, image.width - 1);
      const float fx = sourceX - static_cast<float>(x0);
      for(int channel = 0; channel < 3; ++channel)
      {
        const auto sample = [&](const int sx, const int sy) {
          return static_cast<float>(image.pixels[
            (static_cast<std::size_t>(sy) * image.width + sx) * 3 + channel]);
        };
        const float value = bilinear(sample(x0, y0), sample(x1, y0),
                                     sample(x0, y1), sample(x1, y1), fx, fy);
        input[static_cast<std::size_t>(channel) * samInputSize * samInputSize
              + static_cast<std::size_t>(y) * samInputSize + x]
          = (value - imageMean[channel]) / imageStd[channel];
      }
    }
  }

  embeddings_ = encoder_.runFloat({OnnxNamedFloatTensor{
    "image", std::move(input), {1, 3, samInputSize, samInputSize},
  }});
  resetRefinement();
}

pipeline::Mask Sam2Segmenter::segment(const std::vector<SamPointPrompt> &points)
{
  if(embeddings_.empty()) throw std::runtime_error{"SAM image must be encoded before segmentation"};
  if(points.empty()) throw std::invalid_argument{"SAM segmentation requires at least one point"};

  const std::size_t pointCount = points.size() + 1;
  std::vector<float> coordinates(pointCount * 2, 0.0f);
  std::vector<float> labels(pointCount, -1.0f);
  for(std::size_t i = 0; i < points.size(); ++i)
  {
    if(points[i].x < 0.0f || points[i].y < 0.0f
       || points[i].x >= static_cast<float>(imageWidth_)
       || points[i].y >= static_cast<float>(imageHeight_))
    {
      throw std::invalid_argument{"SAM point is outside preview dimensions"};
    }
    coordinates[2 * i] = points[i].x * scale_;
    coordinates[2 * i + 1] = points[i].y * scale_;
    labels[i] = points[i].foreground ? 1.0f : 0.0f;
  }

  std::vector<OnnxNamedFloatTensor> inputs = embeddings_;
  inputs.push_back({"point_coords", std::move(coordinates),
                    {1, static_cast<std::int64_t>(pointCount), 2}});
  inputs.push_back({"point_labels", std::move(labels),
                    {1, static_cast<std::int64_t>(pointCount)}});
  inputs.push_back({"mask_input", previousMask_, {1, 1, samMaskSize, samMaskSize}});
  inputs.push_back({"has_mask_input", {hasPreviousMask_ ? 1.0f : 0.0f}, {1}});

  const std::vector<OnnxNamedFloatTensor> outputs = decoder_.runFloat(inputs);
  const OnnxNamedFloatTensor &masks = tensorNamed(outputs, "masks");
  const OnnxNamedFloatTensor &iou = tensorNamed(outputs, "iou_predictions");
  if(masks.shape.size() != 4 || masks.shape[0] != 1 || masks.shape[2] <= 0 || masks.shape[3] <= 0)
  {
    throw std::runtime_error{"SAM decoder returned an unexpected mask shape"};
  }

  const int maskCount = static_cast<int>(masks.shape[1]);
  const int maskHeight = static_cast<int>(masks.shape[2]);
  const int maskWidth = static_cast<int>(masks.shape[3]);
  if(maskCount <= 0 || iou.values.size() < static_cast<std::size_t>(maskCount))
  {
    throw std::runtime_error{"SAM decoder returned invalid IoU predictions"};
  }
  int best = 0;
  for(int i = 1; i < maskCount; ++i)
  {
    if(iou.values[static_cast<std::size_t>(i)] > iou.values[static_cast<std::size_t>(best)]) best = i;
  }

  const std::size_t planeSize = static_cast<std::size_t>(maskWidth) * maskHeight;
  const float *bestMask = masks.values.data() + static_cast<std::size_t>(best) * planeSize;
  if(maskWidth == samMaskSize && maskHeight == samMaskSize)
  {
    previousMask_.assign(bestMask, bestMask + planeSize);
    hasPreviousMask_ = true;
  }

  const int validWidth = std::max(1, std::min(maskWidth,
    static_cast<int>(imageWidth_ * scale_ * maskWidth / samInputSize + 0.5f)));
  const int validHeight = std::max(1, std::min(maskHeight,
    static_cast<int>(imageHeight_ * scale_ * maskHeight / samInputSize + 0.5f)));
  std::vector<float> result(static_cast<std::size_t>(imageWidth_) * imageHeight_);
  for(int y = 0; y < imageHeight_; ++y)
  {
    const float sy = imageHeight_ > 1
      ? static_cast<float>(y) * (validHeight - 1) / (imageHeight_ - 1) : 0.0f;
    const int y0 = std::min(static_cast<int>(sy), validHeight - 1);
    const int y1 = std::min(y0 + 1, validHeight - 1);
    const float fy = sy - y0;
    for(int x = 0; x < imageWidth_; ++x)
    {
      const float sx = imageWidth_ > 1
        ? static_cast<float>(x) * (validWidth - 1) / (imageWidth_ - 1) : 0.0f;
      const int x0 = std::min(static_cast<int>(sx), validWidth - 1);
      const int x1 = std::min(x0 + 1, validWidth - 1);
      const float fx = sx - x0;
      const float logit = bilinear(bestMask[static_cast<std::size_t>(y0) * maskWidth + x0],
                                   bestMask[static_cast<std::size_t>(y0) * maskWidth + x1],
                                   bestMask[static_cast<std::size_t>(y1) * maskWidth + x0],
                                   bestMask[static_cast<std::size_t>(y1) * maskWidth + x1], fx, fy);
      result[static_cast<std::size_t>(y) * imageWidth_ + x] = 1.0f / (1.0f + std::exp(-logit));
    }
  }
  return pipeline::Mask{imageWidth_, imageHeight_, std::move(result)};
}

void Sam2Segmenter::resetRefinement()
{
  std::fill(previousMask_.begin(), previousMask_.end(), 0.0f);
  hasPreviousMask_ = false;
}

}
