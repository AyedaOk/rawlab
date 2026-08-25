#pragma once

#include "rawlab/image/RawPreview.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/raw/RawImage.h"

#include <span>

namespace rawlab::pipeline {

struct GlobalLayerInput
{
  const raw::RawImage *processed = nullptr;
  float blend = 1.0f;
  bool enabled = true;
};

struct LocalLayerInput
{
  const raw::RawImage *processed = nullptr;
  const Mask *mask = nullptr;
  float blend = 1.0f;
  bool inverted = false;
  bool enabled = true;
};

// Local inputs are applied in the supplied order. Pass bottommost first so the
// topmost local layer is applied last and wins full-strength overlaps.
raw::RawImage composeDenoiseLayers(const raw::RawImage &source,
                                   const GlobalLayerInput &global,
                                   std::span<const LocalLayerInput> localsBottomToTop,
                                   const image::PreviewGeometry &geometry);

}
