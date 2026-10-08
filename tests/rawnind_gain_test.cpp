#include "pipeline/RawNindDenoise.h"
#include "rawlab/raw/BayerProcessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr int packedSize = 512;
constexpr int sensorSize = 2 * packedSize;
constexpr std::size_t packedPlane = packedSize * packedSize;
constexpr std::size_t rgbPlane = sensorSize * sensorSize;
constexpr std::array<int, 4> channels{0, 1, 1, 2};

void require(const bool condition, const std::string_view message)
{
  if(!condition) throw std::runtime_error{std::string{message}};
}

rawlab::raw::RawImage makeSource(const int redX = 0, const int redY = 0,
                                const bool boundaries = false)
{
  using rawlab::raw::CfaColor;
  const int width = boundaries ? 963 : 1024;
  const int height = boundaries ? 941 : 1024;
  rawlab::raw::RawMetadata metadata;
  metadata.cfa.isBayer = true;
  for(int y = 0; y < 2; ++y)
    for(int x = 0; x < 2; ++x)
      metadata.cfa.cells[y][x] = x == redX && y == redY ? CfaColor::Red
        : x != redX && y != redY ? CfaColor::Blue : CfaColor::Green;
  metadata.activeArea = boundaries ? rawlab::raw::ActiveArea{3, 5, 947, 925}
                                   : rawlab::raw::ActiveArea{0, 0, width, height};
  if(boundaries)
  {
    metadata.blackLevel = {10.0f, 20.0f, 30.0f, 40.0f};
    metadata.whiteLevel = 4095.0f;
  }
  std::vector<float> samples(static_cast<std::size_t>(width) * height);
  for(int y = 0; y < height; ++y)
    for(int x = 0; x < width; ++x)
    {
      const auto color = rawlab::raw::colorAt(metadata.cfa, x, y);
      float value = color == CfaColor::Red ? 0.1f : color == CfaColor::Green ? 0.2f : 0.05f;
      if(boundaries) value += 0.00001f * x + 0.00002f * y;
      const float black = metadata.blackLevel[rawlab::raw::siteIndex(x, y)];
      samples[static_cast<std::size_t>(y) * width + x] =
        rawlab::raw::denormalizeRawSample(value, black, metadata.whiteLevel);
    }
  return {width, height, std::move(samples), metadata};
}

// A perfect prediction at CFA sites, with RGB interpolation that is irrelevant
// to the gain fit. G1 and G2 retain their own source samples.
rawlab::ai::OnnxFloatRunResult predict(const std::vector<float> &input, const float scale)
{
  rawlab::ai::OnnxFloatRunResult result;
  result.shape = {1, 3, sensorSize, sensorSize};
  result.values.resize(3 * rgbPlane);
  for(int y = 0; y < sensorSize; ++y)
    for(int x = 0; x < sensorSize; ++x)
    {
      const std::size_t packed = static_cast<std::size_t>(y / 2) * packedSize + x / 2;
      const std::size_t pixel = static_cast<std::size_t>(y) * sensorSize + x;
      result.values[pixel] = scale * input[packed];
      result.values[rgbPlane + pixel] = scale * input[(1 + (y & 1)) * packedPlane + packed];
      result.values[2 * rgbPlane + pixel] = scale * input[3 * packedPlane + packed];
    }
  return result;
}

void expectSource(const rawlab::raw::RawImage &source, const rawlab::raw::RawImage &result,
                  const char *label)
{
  require(source.samples().size() == result.samples().size(), "Output size changed");
  const float tolerance = source.metadata().whiteLevel * 1e-6f;
  for(std::size_t i = 0; i < source.samples().size(); ++i)
    if(!std::isfinite(result.samples()[i])
       || std::fabs(result.samples()[i] - source.samples()[i]) >= tolerance)
      throw std::runtime_error{std::string{label} + ": CFA sample " + std::to_string(i)
        + " expected " + std::to_string(source.samples()[i])
        + ", got " + std::to_string(result.samples()[i])};
}

void expectFailure(const rawlab::raw::RawImage &source,
                   const rawlab::pipeline::detail::RawNindInference &infer,
                   const char *message)
{
  try
  {
    (void)rawlab::pipeline::detail::runRawNindDenoiseTiles(source, infer);
  }
  catch(const std::runtime_error &error)
  {
    require(std::string{error.what()}.find(message) != std::string::npos,
            std::string{"Unexpected failure: "} + error.what());
    return;
  }
  throw std::runtime_error{std::string{"Expected failure: "} + message};
}

} // namespace

int main()
{
  using rawlab::pipeline::detail::runRawNindDenoiseTiles;
  try
  {
    // A flat field with distinct R/G1/G2/B levels must retain each plane's
    // colour through padding, including corners and repeated reflections.
    constexpr std::array<float, 4> levels{0.1f, 0.2f, 0.3f, 0.05f};
    for(int redY = 0; redY < 2; ++redY)
      for(int redX = 0; redX < 2; ++redX)
        for(const int size : {2, 14, 128})
        {
          auto metadata = makeSource(redX, redY, true).metadata();
          metadata.activeArea = {3, 5, size + 1, size + 1};
          const int width = size + 8, height = size + 10;
          rawlab::raw::RawImage image{width, height,
            std::vector<float>(static_cast<std::size_t>(width) * height, 0.0f), metadata};
          const int left = redX == 0 ? 4 : 3;
          const int top = redY == 0 ? 6 : 5;
          for(int y = top; y < top + size; ++y)
            for(int x = left; x < left + size; ++x)
            {
              const int site = 2 * ((y - top) & 1) + ((x - left) & 1);
              image.setSample(x, y, rawlab::raw::denormalizeRawSample(levels[site],
                metadata.blackLevel[rawlab::raw::siteIndex(x, y)], metadata.whiteLevel));
            }
          const auto result = runRawNindDenoiseTiles(image, [&](const auto &input) {
            for(int site = 0; site < 4; ++site)
              for(std::size_t i = 0; i < packedPlane; ++i)
                require(std::fabs(input[site * packedPlane + i] - levels[site]) < 1e-7f,
                        "Mirrored input changed Bayer colour (R/G1/G2/B)");
            return predict(input, -2.0f);
          });
          expectSource(image, result, "Mirrored CFA colours and active borders");
        }

    const auto source = makeSource();
    for(const float scale : {1.0f, 2.0f, -3.0f, 1e-20f, -1e20f})
    {
      const auto result = runRawNindDenoiseTiles(source, [scale](const auto &input) {
        return predict(input, scale);
      });
      expectSource(source, result, "Unequal-channel signed gain");
    }

    // Four CFA phases, nonzero active offsets, odd edges, and four overlapping
    // tiles. The bottom-right tile contains far less than 10% real image data.
    for(int redY = 0; redY < 2; ++redY)
      for(int redX = 0; redX < 2; ++redX)
      {
        const auto image = makeSource(redX, redY, true);
        const int left = redX == 0 ? 4 : 3;
        const int top = redY == 0 ? 6 : 5;
        const int right = left + 946;
        const int bottom = top + 924;
        int tile = 0;
        int progress = 0;
        const auto result = runRawNindDenoiseTiles(image, [&](const auto &input) {
          const int originX = left + (tile % 2) * 896 - 64;
          const int originY = top + (tile / 2) * 896 - 64;
          const float scale = tile++ % 2 == 0 ? 2.0f : -3.0f;
          auto prediction = predict(input, scale);
          for(int y = 0; y < sensorSize; ++y)
            for(int x = 0; x < sensorSize; ++x)
            {
              const int sx = originX + x, sy = originY + y;
              const std::size_t pixel = static_cast<std::size_t>(y) * sensorSize + x;
              if(sx < left || sx >= right || sy < top || sy >= bottom)
              {
                // Padding and non-CFA RGB values must not influence exposure.
                for(int channel = 0; channel < 3; ++channel)
                  prediction.values[channel * rgbPlane + pixel] = -50.0f;
                continue;
              }
              const int site = ((y & 1) << 1) | (x & 1);
              const float normalized = rawlab::raw::normalizeRawSample(
                image.sample(sx, sy), image.metadata().blackLevel[rawlab::raw::siteIndex(sx, sy)],
                image.metadata().whiteLevel);
              const auto packed = site * packedPlane + (y / 2) * packedSize + x / 2;
              require(std::fabs(input[packed] - normalized) < 1e-7f, "Packed CFA phase mismatch");
              for(int channel = 0; channel < 3; ++channel)
                if(channel != channels[site]) prediction.values[channel * rgbPlane + pixel] = 30.0f;
            }
          return prediction;
        }, [&](const int done, const int total) {
          require(done == ++progress && total == 4, "Unexpected tile progress");
        });
        require(tile == 4 && progress == 4, "Missing boundary tiles");
        expectSource(image, result, "Phase/padding/overlap");
      }

    auto extremes = source;
    for(std::size_t i = 0; i < extremes.samples().size(); i += 4)
    {
      extremes.samples()[i] = 0.0005f;
      extremes.samples()[i + 1] = 0.9995f;
    }
    const auto filtered = runRawNindDenoiseTiles(extremes, [](const auto &input) {
      auto prediction = predict(input, -2.0f);
      for(int py = 0; py < packedSize; ++py)
        for(int px = 0; px < packedSize; ++px)
          for(int site = 0; site < 4; ++site)
          {
            const float value = input[site * packedPlane + py * packedSize + px];
            if(value > 0.001f && value < 0.999f) continue;
            const auto pixel = (2 * py + (site >> 1)) * sensorSize + 2 * px + (site & 1);
            prediction.values[channels[site] * rgbPlane + pixel] = -1.6f;
          }
      return prediction;
    });
    auto expected = extremes;
    for(float &value : expected.samples())
      if(value <= 0.001f || value >= 0.999f) value = 0.8f;
    expectSource(expected, filtered, "Black/saturation exclusion");

    auto unsupported = source;
    for(const float value : {0.0f, 1.0f})
    {
      std::fill(unsupported.samples().begin(), unsupported.samples().end(), value);
      expectFailure(unsupported, [](const auto &input) { return predict(input, 1.0f); }, "valid CFA");
    }
    std::fill(unsupported.samples().begin(), unsupported.samples().end(), 0.0f);
    std::fill_n(unsupported.samples().begin(), 100, 0.1f);
    expectFailure(unsupported, [](const auto &input) { return predict(input, 1.0f); }, "valid CFA");
    expectFailure(source, [](const auto &input) { return predict(input, 0.0f); }, "energy");
    expectFailure(source, [](const auto &input) {
      auto prediction = predict(input, 1.0f);
      for(std::size_t i = 0; i < prediction.values.size(); ++i)
        prediction.values[i] = (i & 2) == 0 ? -1.0f : 1.0f;
      return prediction;
    }, "zero or nonfinite gain");

    auto nonfiniteSource = source;
    nonfiniteSource.samples()[0] = std::numeric_limits<float>::quiet_NaN();
    const auto repaired = runRawNindDenoiseTiles(nonfiniteSource, [](const auto &input) {
      auto prediction = predict(input, 1.0f);
      constexpr float rgb[]{0.1f, 0.2f, 0.05f};
      for(int channel = 0; channel < 3; ++channel)
        std::fill_n(prediction.values.begin() + channel * rgbPlane, rgbPlane, rgb[channel]);
      return prediction;
    });
    expectSource(source, repaired, "Nonfinite source exclusion");

    for(const float value : {std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity()})
      for(const std::size_t index : {std::size_t{0}, std::size_t{1}, 2 * rgbPlane + 65 * sensorSize + 65})
        expectFailure(source, [=](const auto &input) {
          auto prediction = predict(input, 1.0f);
          prediction.values[index] = value;
          return prediction;
        }, "nonfinite");

    expectFailure(source, [](const auto &input) {
      auto prediction = predict(input, 1e-20f);
      prediction.values[1] = std::numeric_limits<float>::max();
      return prediction;
    }, "overflow");
    auto rawOverflow = makeSource(0, 0, true);
    rawOverflow.setSample(4, 6, rawOverflow.metadata().blackLevel[0]);
    expectFailure(rawOverflow, [](const auto &input) {
      auto prediction = predict(input, 1.0f);
      prediction.values[64 * sensorSize + 64] = std::numeric_limits<float>::max() / 2.0f;
      return prediction;
    }, "CFA composition overflow");
    expectFailure(source, [](const auto &input) {
      auto prediction = predict(input, 1.0f);
      prediction.values.pop_back();
      return prediction;
    }, "shape");
    std::cout << "RawNIND gain: scale, phase, padding, overlap, exclusions and invalid-output checks passed\n";
  }
  catch(const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
