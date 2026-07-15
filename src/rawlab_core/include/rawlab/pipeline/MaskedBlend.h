#pragma once

#include "rawlab/pipeline/Mask.h"
#include "rawlab/raw/RawImage.h"

namespace rawlab::pipeline {

raw::RawImage blendWithMask(const raw::RawImage &original,
                            const raw::RawImage &processed,
                            const Mask &mask,
                            float blendAmount);

raw::RawImage blendWithMappedMask(const raw::RawImage &original,
                                  const raw::RawImage &processed,
                                  const Mask &mask,
                                  float blendAmount);

}
