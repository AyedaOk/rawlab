#pragma once

#include "rawlab/raw/RawImage.h"

#include <functional>
#include <vector>

namespace rawlab::ai {

class OnnxSession;

class RawForgeAdapter
{
public:
  using ProgressCallback = std::function<void(int completed, int total)>;

  rawlab::raw::RawImage run(const rawlab::raw::RawImage &source,
                            const OnnxSession &session,
                            float iso,
                            bool capIso,
                            const ProgressCallback &progress = {}) const;

  static std::vector<float> demosaicToLinearRec2020(const rawlab::raw::RawImage &source);
  static rawlab::raw::RawImage remosaicFromLinearRec2020(
    const rawlab::raw::RawImage &source, const std::vector<float> &rec2020);
};

}
