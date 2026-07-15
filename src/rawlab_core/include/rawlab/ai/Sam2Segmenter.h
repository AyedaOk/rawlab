#pragma once

#include "rawlab/ai/OnnxSession.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/pipeline/Mask.h"

#include <filesystem>
#include <vector>

namespace rawlab::ai {

struct SamPointPrompt
{
  float x = 0.0f;
  float y = 0.0f;
  bool foreground = true;
};

class Sam2Segmenter
{
public:
  Sam2Segmenter(const std::filesystem::path &encoderPath,
                const std::filesystem::path &decoderPath);

  void encode(const image::RgbImage &image);
  pipeline::Mask segment(const std::vector<SamPointPrompt> &points);
  void resetRefinement();

private:
  OnnxSession encoder_;
  OnnxSession decoder_;
  std::vector<OnnxNamedFloatTensor> embeddings_;
  std::vector<float> previousMask_;
  int imageWidth_ = 0;
  int imageHeight_ = 0;
  float scale_ = 1.0f;
  bool hasPreviousMask_ = false;
};

}
