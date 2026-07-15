#pragma once

#include "rawlab/ai/OnnxSession.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/raw/RawImage.h"

#include <functional>

namespace rawlab::pipeline {

struct RawDenoisePipelineOptions
{
  float blend = 1.0f;
  bool preserveClippedHighlights = false;
};

struct RawMaskedDenoisePipelineOptions
{
  float blend = 1.0f;
  bool preserveClippedHighlights = false;
  bool mapMaskToRaw = true;
};

class RawProcessingPipeline
{
public:
  using ProgressCallback = std::function<void(int completed, int total)>;

  raw::RawImage runRawNindDenoise(const raw::RawImage &source,
                                  const ai::OnnxSession &session,
                                  const ProgressCallback &progress = {}) const;

  raw::RawImage runRawNindDenoiseBlend(const raw::RawImage &source,
                                       const ai::OnnxSession &session,
                                       const RawDenoisePipelineOptions &options,
                                       const ProgressCallback &progress = {}) const;

  raw::RawImage runRawNindDenoiseMaskedBlend(const raw::RawImage &source,
                                             const ai::OnnxSession &session,
                                             const Mask &mask,
                                             const RawMaskedDenoisePipelineOptions &options,
                                             const ProgressCallback &progress = {}) const;
};

}
