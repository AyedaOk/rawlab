#pragma once

#include "rawlab/pipeline/RawProcessingPipeline.h"

namespace rawlab::pipeline::detail {

// Keep tile processing testable without loading a model or ONNX Runtime.
using RawNindInference = std::function<ai::OnnxFloatRunResult(const std::vector<float> &)>;

raw::RawImage runRawNindDenoiseTiles(
  const raw::RawImage &source,
  const RawNindInference &infer,
  const RawProcessingPipeline::ProgressCallback &progress = {});

}
