#pragma once
#include "ProcessingSession.h"

// Deterministic model output exercises production composition without ONNX or downloaded weights.
struct ProcessingSessionTest
{
  static void seed(ProcessingSession &session, const QString &path, const ApplicationSettingsData &settings)
  {
    const auto source = session.source(path);
    auto samples = source->samples();
    for(int y = 0; y < source->height(); ++y) for(int x = 0; x < source->width(); ++x)
    {
      const int other = x + 2 < source->width() ? x + 2 : x - 2;
      samples[y * source->width() + x] = .75f * source->sample(x, y) + .25f * source->sample(other, y);
    }
    auto denoised = std::make_shared<rawlab::raw::RawImage>(source->width(), source->height(), std::move(samples), source->metadata());
    for(const auto &model : settings.denoiseModels)
    {
      const auto key = session.sourceKey_ + ':' + ProcessingSession::fingerprint(model.path) + ':' + model.adapter
        + ':' + settings.onnxProvider + ':' + QString::number(settings.onnxDeviceId) + ':' + settings.onnxRuntimeLibrary;
      session.cacheImage(key, denoised);
    }
  }

  static rawlab::image::LinearRawPreview exportReference(const rawlab::raw::RawImage &source)
  {
    const auto rgb = rawlab::image::renderLinearRgb(source);
    const auto geometry = rawlab::image::rawPreviewGeometry(source, std::max(source.width(), source.height()));
    rawlab::image::LinearRawPreview result{geometry.previewWidth, geometry.previewHeight, rgb.pixels, geometry};
    for(int y = 0; y < result.height; ++y) for(int x = 0; x < result.width; ++x)
    {
      const auto p = geometry.previewToRaw(x + .5f, y + .5f);
      const int sx = static_cast<int>(p.first) - geometry.rawArea.left;
      const int sy = static_cast<int>(p.second) - geometry.rawArea.top;
      std::copy_n(rgb.pixels.begin() + (sy * rgb.width + sx) * 3, 3, result.pixels.begin() + (y * result.width + x) * 3);
    }
    return result;
  }
};
