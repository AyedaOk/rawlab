#include "rawlab/core/Version.h"
#include "rawlab/ai/OnnxSession.h"
#include "rawlab/ai/RawForgeAdapter.h"
#include "rawlab/ai/Sam2Segmenter.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/pipeline/Mask.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/raw/BayerProcessing.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/raw/RawImage.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void printUsage()
{
  std::cout << "rawlab " << rawlab::core::version() << "\n"
            << "Usage:\n"
            << "  rawlab inspect <raw-file>\n"
            << "  rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]\n"
            << "  rawlab mask <preview.png> <mask.png> --encoder <encoder.onnx> --decoder <decoder.onnx> [--point x y] [--negative-point x y]\n"
            << "  rawlab inspect-onnx <model.onnx>\n"
            << "  rawlab test-rawnind-tile <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab test-mask-blend <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab test-preview-mask-blend <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab test-mask-split <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab test-preview-mask-split <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab raw-denoise <input.raw> <model.onnx> <output.dng> [--blend 0..1] [--clipped-highlights]\n"
            << "  rawlab rawforge-restore <input.raw> <model.onnx> <output.dng>\n"
            << "  rawlab export-cfa-dng <input.raw> <output.dng>\n"
            << "  rawlab write-synthetic-dng <output.dng>\n";
}

struct RawDenoiseOptions
{
  float blend = 1.0f;
  bool preserveClippedHighlights = false;
};

struct ProcessOptions
{
  std::filesystem::path denoiseModel;
  std::filesystem::path maskPath;
  float blend = 1.0f;
  bool blendSpecified = false;
  bool preserveClippedHighlights = false;
};

struct MaskOptions
{
  std::filesystem::path encoderPath;
  std::filesystem::path decoderPath;
  std::vector<rawlab::ai::SamPointPrompt> points;
};

MaskOptions parseMaskOptions(const int argc, char **argv, const int firstOptionIndex)
{
  MaskOptions options;
  for(int i = firstOptionIndex; i < argc; ++i)
  {
    const std::string_view option{argv[i]};
    if(option == "--encoder" || option == "--decoder")
    {
      if(i + 1 >= argc) throw std::runtime_error{std::string{option} + " requires a model path"};
      std::filesystem::path &path = option == "--encoder" ? options.encoderPath : options.decoderPath;
      if(!path.empty()) throw std::runtime_error{std::string{option} + " may only be specified once"};
      path = std::filesystem::path{argv[++i]};
    }
    else if(option == "--point" || option == "--negative-point")
    {
      if(i + 2 >= argc) throw std::runtime_error{std::string{option} + " requires x and y"};
      options.points.push_back({std::stof(argv[++i]), std::stof(argv[++i]), option == "--point"});
    }
    else
    {
      throw std::runtime_error{std::string{"Unknown mask option: "} + argv[i]};
    }
  }
  if(options.encoderPath.empty() || options.decoderPath.empty())
  {
    throw std::runtime_error{"mask requires --encoder and --decoder"};
  }
  if(options.points.empty()) throw std::runtime_error{"mask requires at least one point"};
  return options;
}

ProcessOptions parseProcessOptions(const int argc, char **argv, const int firstOptionIndex)
{
  ProcessOptions options;
  for(int i = firstOptionIndex; i < argc; ++i)
  {
    const std::string_view option{argv[i]};
    if(option == "--denoise")
    {
      if(i + 1 >= argc) throw std::runtime_error{"--denoise requires a model path"};
      if(!options.denoiseModel.empty()) throw std::runtime_error{"--denoise may only be specified once"};
      options.denoiseModel = std::filesystem::path{argv[++i]};
    }
    else if(option == "--mask")
    {
      if(i + 1 >= argc) throw std::runtime_error{"--mask requires a PNG path"};
      if(!options.maskPath.empty()) throw std::runtime_error{"--mask may only be specified once"};
      options.maskPath = std::filesystem::path{argv[++i]};
    }
    else if(option == "--blend")
    {
      if(i + 1 >= argc) throw std::runtime_error{"--blend requires a value"};
      options.blendSpecified = true;
      options.blend = std::stof(argv[++i]);
      if(options.blend < 0.0f || options.blend > 1.0f)
      {
        throw std::runtime_error{"--blend must be between 0 and 1"};
      }
    }
    else if(option == "--clipped-highlights")
    {
      options.preserveClippedHighlights = true;
    }
    else
    {
      throw std::runtime_error{std::string{"Unknown process option: "} + argv[i]};
    }
  }

  if(options.denoiseModel.empty()
     && (options.blendSpecified || options.preserveClippedHighlights || !options.maskPath.empty()))
  {
    throw std::runtime_error{"--mask, --blend, and --clipped-highlights require --denoise"};
  }
  return options;
}

RawDenoiseOptions parseRawDenoiseOptions(const int argc, char **argv, const int firstOptionIndex)
{
  RawDenoiseOptions options;
  for(int i = firstOptionIndex; i < argc; ++i)
  {
    if(std::string_view{argv[i]} == "--blend")
    {
      if(i + 1 >= argc) throw std::runtime_error{"--blend requires a value"};
      options.blend = std::stof(argv[++i]);
      if(options.blend < 0.0f || options.blend > 1.0f) throw std::runtime_error{"--blend must be between 0 and 1"};
    }
    else if(std::string_view{argv[i]} == "--clipped-highlights")
    {
      options.preserveClippedHighlights = true;
    }
    else
    {
      throw std::runtime_error{std::string{"Unknown raw-denoise option: "} + argv[i]};
    }
  }
  return options;
}

double meanOf(const std::vector<float> &values)
{
  double sum = 0.0;
  for(const float value : values) sum += value;
  return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

void matchGain(const std::vector<float> &packedInput, std::vector<float> &modelOutput)
{
  const double inputMean = meanOf(packedInput);
  const double outputMean = meanOf(modelOutput);
  const double threshold = 1e-6 * std::fabs(inputMean);
  const float gain = std::fabs(outputMean) > threshold
    ? static_cast<float>(inputMean / outputMean) : 1.0f;
  if(gain != 1.0f)
  {
    for(float &value : modelOutput) value *= gain;
  }
}

rawlab::raw::RawImage cropAlignedCfaTile(const rawlab::raw::RawImage &source,
                                         const int tileSize)
{
  int redX = 0;
  int redY = 0;
  if(!rawlab::raw::redOrigin(source.metadata().cfa, redX, redY))
  {
    throw std::runtime_error{"test-rawnind-tile requires a Bayer CFA source"};
  }

  const rawlab::raw::ActiveArea &active = source.metadata().activeArea;
  int startX = active.width > 0 ? active.left : 0;
  int startY = active.height > 0 ? active.top : 0;
  if((startX & 1) != redX) ++startX;
  if((startY & 1) != redY) ++startY;

  if(startX + tileSize > source.width() || startY + tileSize > source.height())
  {
    throw std::runtime_error{"Source raw is too small for a red-origin-aligned RawNIND tile"};
  }

  std::vector<float> samples(static_cast<std::size_t>(tileSize) * static_cast<std::size_t>(tileSize), 0.0f);
  for(int y = 0; y < tileSize; ++y)
  {
    for(int x = 0; x < tileSize; ++x)
    {
      samples[static_cast<std::size_t>(y) * static_cast<std::size_t>(tileSize)
              + static_cast<std::size_t>(x)] = source.sample(startX + x, startY + y);
    }
  }

  rawlab::raw::RawMetadata metadata = source.metadata();
  metadata.activeArea = { 0, 0, tileSize, tileSize };
  return rawlab::raw::RawImage{tileSize, tileSize, std::move(samples), metadata};
}

void printShape(const std::vector<std::int64_t> &shape)
{
  std::cout << '[';
  for(std::size_t i = 0; i < shape.size(); ++i)
  {
    if(i != 0) std::cout << ", ";
    std::cout << shape[i];
  }
  std::cout << ']';
}

void printOnnxModelInfo(const rawlab::ai::OnnxModelInfo &info)
{
  std::cout << "ONNX Runtime: " << info.runtimeVersion << '\n';

  std::cout << "Inputs:\n";
  for(const rawlab::ai::OnnxTensorInfo &input : info.inputs)
  {
    std::cout << "  " << input.name << ' ' << input.type << ' ';
    printShape(input.shape);
    std::cout << '\n';
  }

  std::cout << "Outputs:\n";
  for(const rawlab::ai::OnnxTensorInfo &output : info.outputs)
  {
    std::cout << "  " << output.name << ' ' << output.type << ' ';
    printShape(output.shape);
    std::cout << '\n';
  }
}

void printRawMaskProbe(const rawlab::pipeline::Mask &mask, const rawlab::raw::RawImage &source)
{
  const int y = source.height() / 2;
  std::cout << "Mask probe raw: left=" << mask.sampleNearest(0, y)
            << " center=" << mask.sampleNearest(source.width() / 2, y)
            << " right=" << mask.sampleNearest(source.width() - 1, y) << '\n';
}

void printMappedMaskProbe(const rawlab::pipeline::Mask &mask, const rawlab::raw::RawImage &source)
{
  const int y = source.height() / 2;
  std::cout << "Mask probe mapped: left=" << mask.sampleNearestMapped(0, y, source.width(), source.height())
            << " center=" << mask.sampleNearestMapped(source.width() / 2, y, source.width(), source.height())
            << " right=" << mask.sampleNearestMapped(source.width() - 1, y, source.width(), source.height()) << '\n';
}

void printDenoiseProgress(const int completed, const int total)
{
  std::cerr << "raw-denoise tile " << completed << " / " << total << '\n';
}

rawlab::raw::RawImage makeSyntheticDngImage()
{
  rawlab::raw::CfaPattern rggb;
  rggb.isBayer = true;
  rggb.cells[0][0] = rawlab::raw::CfaColor::Red;
  rggb.cells[0][1] = rawlab::raw::CfaColor::Green;
  rggb.cells[1][0] = rawlab::raw::CfaColor::Green;
  rggb.cells[1][1] = rawlab::raw::CfaColor::Blue;

  rawlab::raw::RawMetadata metadata;
  metadata.make = "RawLab";
  metadata.model = "Synthetic Bayer CFA";
  metadata.cfa = rggb;
  metadata.activeArea = { 0, 0, 64, 64 };
  metadata.blackLevel = { 512.0f, 512.0f, 512.0f, 512.0f };
  metadata.whiteLevel = 16383.0f;

  std::vector<float> samples(64 * 64, 0.0f);
  for(int y = 0; y < 64; ++y)
  {
    for(int x = 0; x < 64; ++x)
    {
      const rawlab::raw::CfaColor color = rawlab::raw::colorAt(rggb, x, y);
      const float base = color == rawlab::raw::CfaColor::Red ? 1200.0f
                       : color == rawlab::raw::CfaColor::Blue ? 1800.0f
                       : 1500.0f;
      samples[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)]
        = base + static_cast<float>(x * 64 + y * 32);
    }
  }

  return rawlab::raw::RawImage{64, 64, std::move(samples), metadata};
}

void printInspectInfo(const rawlab::raw::RawInspectInfo &info)
{
  std::cout << "File: " << info.path << '\n'
            << "Make: " << info.make << '\n'
            << "Model: " << info.model << '\n'
            << "Normalized make: " << info.normalizedMake << '\n'
            << "Normalized model: " << info.normalizedModel << '\n'
            << "ISO: " << (info.iso ? std::to_string(*info.iso) : "unavailable") << '\n'
            << "Aperture: " << (info.aperture ? std::to_string(*info.aperture) : "unavailable") << '\n'
            << "Exposure seconds: " << (info.exposureTimeSeconds
                                           ? std::to_string(*info.exposureTimeSeconds) : "unavailable") << '\n'
            << "Focal length mm: " << (info.focalLengthMm
                                         ? std::to_string(*info.focalLengthMm) : "unavailable") << '\n'
            << "Capture timestamp: " << (info.captureTimestamp
                                            ? std::to_string(*info.captureTimestamp) : "unavailable") << '\n'
            << "Raw size: " << info.rawWidth << " x " << info.rawHeight << '\n'
            << "Visible size: " << info.visibleWidth << " x " << info.visibleHeight << '\n'
            << "Active offset: left=" << info.activeLeft
            << " top=" << info.activeTop << '\n'
            << "LibRaw filters: 0x" << std::hex << info.filters << std::dec << '\n'
            << "CFA pattern: " << rawlab::raw::toString(info.cfa)
            << (info.cfa.isBayer ? "" : " (not standard Bayer)") << '\n'
            << "Red origin: x=" << info.redOriginX
            << " y=" << info.redOriginY << '\n'
            << "Black level: " << info.blackLevel << '\n'
            << "White level: " << info.whiteLevel << '\n'
            << "Raw image buffer: " << (info.hasRawImage ? "yes" : "no") << '\n'
            << "Color3 image buffer: " << (info.hasColor3Image ? "yes" : "no") << '\n'
            << "RawImage conversion: " << (info.rawImageConverted ? "yes" : "no") << '\n';

  if(info.rawImageConverted)
  {
    std::cout << "RawImage sample range: " << info.rawSampleMin
              << " .. " << info.rawSampleMax << '\n'
              << "First 2x2 CFA samples:\n";
    for(int y = 0; y < 2; ++y)
    {
      std::cout << "  ";
      for(int x = 0; x < 2; ++x)
      {
        std::cout << rawlab::raw::toString(rawlab::raw::colorAt(info.cfa, x, y))
                  << '=' << info.firstCfaSamples[y][x];
        if(x == 0) std::cout << "  ";
      }
      std::cout << '\n';
    }
  }
}

} // namespace

int main(int argc, char **argv)
{
  if(argc == 1)
  {
    printUsage();
    return 0;
  }

  if(argc == 3 && std::string_view{argv[1]} == "inspect")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      printInspectInfo(decoder.inspect(std::filesystem::path{argv[2]}));
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab inspect failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc >= 5 && std::string_view{argv[1]} == "mask")
  {
    try
    {
      const MaskOptions options = parseMaskOptions(argc, argv, 4);
      const rawlab::image::RgbImage preview = rawlab::image::readRgbPng(std::filesystem::path{argv[2]});
      rawlab::ai::Sam2Segmenter segmenter{options.encoderPath, options.decoderPath};
      std::cerr << "Encoding preview for SAM...\n";
      segmenter.encode(preview);
      std::cerr << "Decoding mask from " << options.points.size() << " point(s)...\n";
      rawlab::image::writeMaskPng(std::filesystem::path{argv[3]}, segmenter.segment(options.points));
      std::cout << "Wrote SAM mask PNG: " << argv[3]
                << " (" << preview.width << "x" << preview.height << ")\n";
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab mask failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc >= 4 && std::string_view{argv[1]} == "process")
  {
    try
    {
      const ProcessOptions options = parseProcessOptions(argc, argv, 4);
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});

      if(options.denoiseModel.empty())
      {
        writer.writeCfaDng(std::filesystem::path{argv[3]}, source);
        std::cout << "Processed Bayer CFA DNG without denoise: " << argv[3] << '\n';
      }
      else
      {
        const rawlab::ai::OnnxSession session{options.denoiseModel};
        const rawlab::pipeline::RawProcessingPipeline pipeline;
        rawlab::raw::RawImage output;
        if(options.maskPath.empty())
        {
          const rawlab::pipeline::RawDenoisePipelineOptions pipelineOptions{
            options.blend,
            options.preserveClippedHighlights,
          };
          output = pipeline.runRawNindDenoiseBlend(source, session, pipelineOptions,
                                                   printDenoiseProgress);
        }
        else
        {
          const rawlab::pipeline::Mask mask = rawlab::image::readMaskPng(options.maskPath);
          const rawlab::pipeline::RawMaskedDenoisePipelineOptions pipelineOptions{
            options.blend,
            options.preserveClippedHighlights,
            true,
          };
          output = pipeline.runRawNindDenoiseMaskedBlend(source, session, mask, pipelineOptions,
                                                         printDenoiseProgress);
        }
        writer.writeCfaDng(std::filesystem::path{argv[3]}, output);
        std::cout << "Processed RawNIND Bayer CFA DNG: " << argv[3]
                  << " (blend=" << options.blend
                  << ", mask=" << (options.maskPath.empty() ? "none" : options.maskPath.string())
                  << ", clipped-highlights="
                  << (options.preserveClippedHighlights ? "preserve" : "model")
                  << ")\n";
      }
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab process failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 3 && std::string_view{argv[1]} == "inspect-onnx")
  {
    try
    {
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[2]}};
      printOnnxModelInfo(session.modelInfo());
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab inspect-onnx failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 3 && std::string_view{argv[1]} == "write-synthetic-dng")
  {
    try
    {
      const rawlab::dng::TiffDngWriter writer;
      writer.writeCfaDng(std::filesystem::path{argv[2]}, makeSyntheticDngImage());
      std::cout << "Wrote synthetic Bayer CFA DNG: " << argv[2] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab write-synthetic-dng failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "test-rawnind-tile")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const rawlab::raw::RawImage tile = cropAlignedCfaTile(source, 1024);
      const rawlab::raw::PackedBayerImage packed = rawlab::raw::packBayerForceRggb(tile);
      if(packed.width != 512 || packed.height != 512)
      {
        throw std::runtime_error{"Packed RawNIND tile is not 512 x 512"};
      }

      rawlab::ai::OnnxFloatRunResult result = session.runSingleFloat(
        packed.planes,
        std::vector<std::int64_t>{1, 4, 512, 512});
      if(result.shape != std::vector<std::int64_t>{1, 3, 1024, 1024})
      {
        throw std::runtime_error{"RawNIND model returned an unexpected output shape"};
      }

      matchGain(packed.planes, result.values);
      rawlab::raw::RawMetadata metadata = tile.metadata();
      metadata.activeArea = { 0, 0, 1024, 1024 };
      rawlab::raw::RawImage restoredTile{
        1024,
        1024,
        rawlab::raw::remosaicCamRgbToCfa(tile, result.values, 1.0f),
        metadata,
      };

      writer.writeCfaDng(std::filesystem::path{argv[4]}, restoredTile);
      std::cout << "Wrote RawNIND single-tile Bayer CFA DNG: " << argv[4] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab test-rawnind-tile failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "test-mask-blend")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::pipeline::RawProcessingPipeline pipeline;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const rawlab::pipeline::Mask mask = rawlab::pipeline::horizontalGradientMask(source.width(), source.height());
      writer.writeCfaDng(std::filesystem::path{argv[4]},
                         pipeline.runRawNindDenoiseMaskedBlend(
                           source, session, mask, {1.0f, false, false}, printDenoiseProgress));
      std::cout << "Wrote synthetic masked RawNIND Bayer CFA DNG: " << argv[4] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab test-mask-blend failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "test-preview-mask-blend")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::pipeline::RawProcessingPipeline pipeline;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const int maskWidth = std::max(1, source.width() / 4);
      const int maskHeight = std::max(1, source.height() / 4);
      const rawlab::pipeline::Mask mask = rawlab::pipeline::horizontalGradientMask(maskWidth, maskHeight);
      writer.writeCfaDng(std::filesystem::path{argv[4]},
                         pipeline.runRawNindDenoiseMaskedBlend(
                           source, session, mask, {1.0f, false, true}, printDenoiseProgress));
      std::cout << "Wrote preview-mapped masked RawNIND Bayer CFA DNG: " << argv[4]
                << " (mask=" << maskWidth << "x" << maskHeight << ")\n";
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab test-preview-mask-blend failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "test-mask-split")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::pipeline::RawProcessingPipeline pipeline;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const rawlab::pipeline::Mask mask = rawlab::pipeline::verticalSplitMask(source.width(), source.height());
      printRawMaskProbe(mask, source);
      writer.writeCfaDng(std::filesystem::path{argv[4]},
                         pipeline.runRawNindDenoiseMaskedBlend(
                           source, session, mask, {1.0f, false, false}, printDenoiseProgress));
      std::cout << "Wrote split masked RawNIND Bayer CFA DNG: " << argv[4] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab test-mask-split failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "test-preview-mask-split")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::pipeline::RawProcessingPipeline pipeline;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const int maskWidth = std::max(2, source.width() / 4);
      const int maskHeight = std::max(1, source.height() / 4);
      const rawlab::pipeline::Mask mask = rawlab::pipeline::verticalSplitMask(maskWidth, maskHeight);
      printMappedMaskProbe(mask, source);
      writer.writeCfaDng(std::filesystem::path{argv[4]},
                         pipeline.runRawNindDenoiseMaskedBlend(
                           source, session, mask, {1.0f, false, true}, printDenoiseProgress));
      std::cout << "Wrote preview-mapped split masked RawNIND Bayer CFA DNG: " << argv[4]
                << " (mask=" << maskWidth << "x" << maskHeight << ")\n";
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab test-preview-mask-split failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc >= 5 && std::string_view{argv[1]} == "raw-denoise")
  {
    try
    {
      const RawDenoiseOptions options = parseRawDenoiseOptions(argc, argv, 5);
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const rawlab::dng::TiffDngWriter writer;
      const rawlab::pipeline::RawProcessingPipeline pipeline;

      const rawlab::raw::RawImage source = decoder.decodeRawImage(std::filesystem::path{argv[2]});
      const rawlab::pipeline::RawDenoisePipelineOptions pipelineOptions{
        options.blend,
        options.preserveClippedHighlights,
      };
      writer.writeCfaDng(std::filesystem::path{argv[4]},
                         pipeline.runRawNindDenoiseBlend(source, session, pipelineOptions, printDenoiseProgress));
      std::cout << "Wrote RawNIND Bayer CFA DNG: " << argv[4]
                << " (blend=" << options.blend
                << ", clipped-highlights="
                << (options.preserveClippedHighlights ? "preserve" : "model")
                << ")\n";
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab raw-denoise failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 4 && std::string_view{argv[1]} == "export-cfa-dng")
  {
    try
    {
      const rawlab::raw::LibRawDecoder decoder;
      const rawlab::dng::TiffDngWriter writer;
      writer.writeCfaDng(std::filesystem::path{argv[3]},
                         decoder.decodeRawImage(std::filesystem::path{argv[2]}));
      std::cout << "Wrote Bayer CFA DNG: " << argv[3] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab export-cfa-dng failed: " << e.what() << '\n';
      return 1;
    }
  }

  if(argc == 5 && std::string_view{argv[1]} == "rawforge-restore")
  {
    try
    {
      const rawlab::raw::RawImage source = rawlab::raw::LibRawDecoder{}.decodeRawImage(
        std::filesystem::path{argv[2]});
      const rawlab::ai::OnnxSession session{std::filesystem::path{argv[3]}};
      const float iso = static_cast<float>(source.metadata().iso.value_or(100.0));
      const rawlab::raw::RawImage restored = rawlab::ai::RawForgeAdapter{}.run(
        source, session, iso, false, printDenoiseProgress);
      rawlab::dng::TiffDngWriter{}.writeCfaDng(std::filesystem::path{argv[4]}, restored);
      std::cout << "Wrote RawForge Bayer CFA DNG: " << argv[4] << '\n';
      return 0;
    }
    catch(const std::exception &e)
    {
      std::cerr << "rawlab rawforge-restore failed: " << e.what() << '\n';
      return 1;
    }
  }

  printUsage();
  return 2;
}
