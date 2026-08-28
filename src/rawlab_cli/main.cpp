#include "rawlab/ai/OnnxSession.h"
#include "rawlab/ai/Sam2Segmenter.h"
#include "rawlab/core/Version.h"
#include "rawlab/dng/DngWriter.h"
#include "rawlab/image/PngImage.h"
#include "rawlab/pipeline/RawProcessingPipeline.h"
#include "rawlab/raw/LibRawDecoder.h"
#include "rawlab/raw/RawInspectInfo.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void usage()
{
  std::cout << "rawlab " << rawlab::core::version() << "\n"
            << "Usage:\n"
            << "  rawlab inspect <raw-file>\n"
            << "  rawlab process <input.raw> <output.dng> [--denoise <model.onnx>] [--mask <mask.png>] [--blend 0..1] [--clipped-highlights]\n"
            << "  rawlab mask <preview.png> <mask.png> --encoder <encoder.onnx> --decoder <decoder.onnx> [--point x y] [--negative-point x y]\n";
}

struct ProcessOptions
{
  std::filesystem::path denoise;
  std::filesystem::path mask;
  float blend = 1.0f;
  bool blendSpecified = false;
  bool clippedHighlights = false;
};

struct MaskOptions
{
  std::filesystem::path encoder;
  std::filesystem::path decoder;
  std::vector<rawlab::ai::SamPointPrompt> points;
};

void progress(const int completed, const int total)
{
  std::cerr << "Denoising " << completed << '/' << total << '\n';
}

ProcessOptions parseProcessOptions(const int argc, char **argv)
{
  ProcessOptions options;
  for(int i = 4; i < argc; ++i)
  {
    const std::string_view argument{argv[i]};
    if(argument == "--denoise" || argument == "--mask")
    {
      if(i + 1 >= argc) throw std::runtime_error{std::string{argument} + " requires a path"};
      auto &path = argument == "--denoise" ? options.denoise : options.mask;
      if(!path.empty()) throw std::runtime_error{std::string{argument} + " may only be specified once"};
      path = std::filesystem::path{argv[++i]};
    }
    else if(argument == "--blend")
    {
      if(i + 1 >= argc) throw std::runtime_error{"--blend requires a value"};
      options.blendSpecified = true;
      options.blend = std::stof(argv[++i]);
      if(options.blend < 0.0f || options.blend > 1.0f)
        throw std::runtime_error{"--blend must be between 0 and 1"};
    }
    else if(argument == "--clipped-highlights") options.clippedHighlights = true;
    else throw std::runtime_error{std::string{"Unknown process option: "} + argv[i]};
  }
  if(options.denoise.empty()
     && (options.blendSpecified || options.clippedHighlights || !options.mask.empty()))
    throw std::runtime_error{"--mask, --blend, and --clipped-highlights require --denoise"};
  return options;
}

MaskOptions parseMaskOptions(const int argc, char **argv)
{
  MaskOptions options;
  for(int i = 4; i < argc; ++i)
  {
    const std::string_view argument{argv[i]};
    if(argument == "--encoder" || argument == "--decoder")
    {
      if(i + 1 >= argc) throw std::runtime_error{std::string{argument} + " requires a path"};
      auto &path = argument == "--encoder" ? options.encoder : options.decoder;
      if(!path.empty()) throw std::runtime_error{std::string{argument} + " may only be specified once"};
      path = std::filesystem::path{argv[++i]};
    }
    else if(argument == "--point" || argument == "--negative-point")
    {
      if(i + 2 >= argc) throw std::runtime_error{std::string{argument} + " requires x and y"};
      options.points.push_back({std::stof(argv[++i]), std::stof(argv[++i]), argument == "--point"});
    }
    else throw std::runtime_error{std::string{"Unknown mask option: "} + argv[i]};
  }
  if(options.encoder.empty() || options.decoder.empty() || options.points.empty())
    throw std::runtime_error{"mask requires encoder, decoder, and at least one point"};
  return options;
}

void printInspect(const rawlab::raw::RawInspectInfo &info)
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

int main(const int argc, char **argv)
{
  if(argc == 1) { usage(); return 0; }
  try
  {
    const std::string_view command{argv[1]};
    if(command == "inspect" && argc == 3)
    {
      printInspect(rawlab::raw::LibRawDecoder{}.inspect(std::filesystem::path{argv[2]}));
      return 0;
    }
    if(command == "mask" && argc >= 5)
    {
      const MaskOptions options = parseMaskOptions(argc, argv);
      const auto preview = rawlab::image::readRgbPng(std::filesystem::path{argv[2]});
      rawlab::ai::Sam2Segmenter segmenter{options.encoder, options.decoder};
      segmenter.encode(preview);
      rawlab::image::writeMaskPng(std::filesystem::path{argv[3]}, segmenter.segment(options.points));
      std::cout << "Wrote SAM mask PNG: " << argv[3] << '\n';
      return 0;
    }
    if(command == "process" && argc >= 4)
    {
      const ProcessOptions options = parseProcessOptions(argc, argv);
      const auto source = rawlab::raw::LibRawDecoder{}.decodeRawImage(std::filesystem::path{argv[2]});
      rawlab::raw::RawImage output = source;
      if(!options.denoise.empty())
      {
        const rawlab::ai::OnnxSession session{options.denoise};
        const rawlab::pipeline::RawProcessingPipeline pipeline;
        if(options.mask.empty())
          output = pipeline.runRawNindDenoiseBlend(source, session,
            {options.blend, options.clippedHighlights}, progress);
        else
          output = pipeline.runRawNindDenoiseMaskedBlend(source, session,
            rawlab::image::readMaskPng(options.mask),
            {options.blend, options.clippedHighlights, true}, progress);
      }
      rawlab::dng::TiffDngWriter{}.writeCfaDng(std::filesystem::path{argv[3]}, output);
      std::cout << "Wrote Bayer CFA DNG: " << argv[3] << '\n';
      return 0;
    }
    usage();
    return 2;
  }
  catch(const std::exception &error)
  {
    std::cerr << "rawlab failed: " << error.what() << '\n';
    return 1;
  }
}
