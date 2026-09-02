#pragma once

#include "rawlab/raw/RawImage.h"

#include <array>
#include <vector>

namespace rawlab::image {

// Standalone adaptation of Darktable's AMaZE Bayer demosaic. Values are
// WB-adjusted normalized camera RGB in R, G, B order and are indexed over the
// active area. The CFA input is normalized after black/white scaling and
// camera white balance, matching Darktable's AMaZE input contract.
std::vector<std::array<float, 3>> demosaicAmaze(const raw::RawImage &source,
                                                const raw::ActiveArea &area);

}
